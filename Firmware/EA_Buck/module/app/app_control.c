/*****************************************************************************
 * @文件: app_control.c
 * @作者: cl
 * @日期: 2026-11-13
 * @版本: v2.0
 * @说明: Buck 主循环双环控制、DWT 实际 dt、软启动与快速保护
 * @平台: STM32F103C8T6
 * @依赖: algo_pid, bsp_adc_scale, drv_adc, drv_tim, stm32f1xx
 ****************************************************************************/
#include "app_control.h"
#include "stm32f1xx.h"
#include "algo_pid.h"
#include "app_telemetry.h"
#include "bsp_adc_scale.h"
#include "drv_adc.h"
#include "drv_tim.h"

/* ==== 模块常量 ==== */
/* 要求 PWM 占空比 ≤80%
 * @来源 memory CTRL_DUTY_MAX=0.80 你 2026-11-13 */
#define APP_CONTROL_DUTY_MAX           0.8f

/* TIM1 ARR=3599，1 个计数对应 1/3600 占空比
 * @来源 memory TIM1_ARR=3599 推导：20kHz/3600 */
#define APP_CONTROL_CCR_PER_DUTY       3600.0f

/* 占空比 <10% 时电流采样窗口不足，读数不可信
 * @来源 memory CTRL_CURRENT_VALID_DUTY=0.10 实测 2026-11-13 */
#define APP_CONTROL_CURRENT_VALID_DUTY 0.10f

/* 电流内环给定上限 3A，正常进入恒流限幅
 * @来源 memory CTRL_ILIMIT_A=3.0 你 2026-11-13 */
#define APP_CONTROL_ILIMIT_A           3.0f

/* 3A 恒流，3.3A 作为故障跳闸阈值
 * @来源 memory CTRL_OC_TRIP_MA=3300 推导：3A+10% */
#define APP_CONTROL_OC_TRIP_MA         3300

/* 10kHz 连续 3 点 = 300µs 去抖
 * @来源 memory CTRL_OC_COUNT=3 推导：10kHz×3=300µs */
#define APP_CONTROL_OC_COUNT           3U

/* 12V 的 112.5% 作为过压保护点
 * @来源 memory CTRL_OVP_MV=13500 推导：12V+12.5%=13.5V */
#define APP_CONTROL_OVP_MV             13500

/* 10kHz 连续 3 点 = 300µs 去抖
 * @来源 memory CTRL_OVP_COUNT=3 推导：10kHz×3=300µs */
#define APP_CONTROL_OVP_COUNT          3U

/* 输出电压下限
 * @来源 memory CTRL_VREF_MIN_MV=3300 你 2026-11-13 */
#define APP_CONTROL_VREF_MIN_MV        3300U

/* 输出电压上限
 * @来源 memory CTRL_VREF_MAX_MV=12000 你 2026-11-13 */
#define APP_CONTROL_VREF_MAX_MV        12000U

/* 上电默认 3.3V
 * @来源 memory CTRL_VREF_DEFAULT_MV=3300 你 默认3.3V 2026-11-13 */
#define APP_CONTROL_VREF_DEFAULT_MV    3300U

/* 10kHz 每 100µs 24mV = 240mV/ms
 * @来源 memory CTRL_VREF_SLEW=240mV/ms 推导：12V/50ms */
#define APP_CONTROL_VREF_SLEW_MV       24U

/* 期望控制周期 1/10kHz = 100µs；实际 dt 由 DWT 测量
 * @来源 memory CTRL_ADC_STEP_US=100 推导：1/10kHz
 * @来源 memory CTRL_LOOP_HZ=10000 推导：ADC 10kHz/1 */
#define APP_CONTROL_ADC_STEP_US        100U

/* DWT CYCCNT 计数频率等于 HCLK
 * @来源 memory CLOCK_HCLK=72MHz 推导：DWT 1 计数 = 1/72MHz */
#define APP_CONTROL_DWT_MHZ            72U

/* 主循环阻塞超过 10 个控制周期时，PID 不再使用异常大的 dt
 * @来源 本设计保护：10 × 100µs */
#define APP_CONTROL_DT_MAX_US          1000U

/* 启动阶段电压环：V→D，fc≈38Hz
 * @来源 memory CTRL_START_KP=0.002 duty/V 推导：Kp×Vin×Q<1
 * @来源 memory CTRL_START_KI=10.0 duty/(V·s) 推导：Ki×24/(2πfc)=1 */
#define APP_CONTROL_START_KP           0.002f
#define APP_CONTROL_START_KI           10.0f

/* 电压外环：V→A，fc≈50Hz
 * @来源 memory CTRL_VOLT_KP=0.050 A/V 推导：Kp=ωc×C/√2
 * @来源 memory CTRL_VOLT_KI=15.0 A/(V·s) 推导：Ki=Kp×ωc */
#define APP_CONTROL_VOLT_KP            0.05f
#define APP_CONTROL_VOLT_KI            15.0f

/* 电流内环：A→D，fc≈500Hz
 * @来源 memory CTRL_CURR_KP=0.020 duty/A 推导：Kp=ωc×L/(√2×Vin)
 * @来源 memory CTRL_CURR_KI=64.0 duty/(A·s) 推导：Ki=Kp×ωc */
#define APP_CONTROL_CURR_KP            0.02f
#define APP_CONTROL_CURR_KI            64.0f

/* 遥测上报增益：放大 1000 倍转整数，避免 RTT 侧浮点格式化
 * @来源 memory CTRL_START_KP=0.002 / CTRL_START_KI=10.0 推导：×1000
 * @来源 memory CTRL_VOLT_KP=0.050 / CTRL_VOLT_KI=15.0 推导：×1000 */
#define APP_CONTROL_START_KP_MILLI     ((uint16_t)(APP_CONTROL_START_KP * 1000.0f))
#define APP_CONTROL_START_KI_MILLI     ((uint16_t)(APP_CONTROL_START_KI * 1000.0f))
#define APP_CONTROL_VOLT_KP_MILLI      ((uint16_t)(APP_CONTROL_VOLT_KP * 1000.0f))
#define APP_CONTROL_VOLT_KI_MILLI      ((uint16_t)(APP_CONTROL_VOLT_KI * 1000.0f))

/* ==== 模块变量 ==== */
static algo_pid_t s_pid_start;
static algo_pid_t s_pid_volt;
static algo_pid_t s_pid_curr;
static uint64_t s_now_us;
static uint32_t s_last_cycles;
static uint16_t s_last_ccr;
static volatile uint8_t s_adc_pending;
static volatile uint32_t s_overrun;
static uint32_t s_vref_mv;
static uint32_t s_vref_target_mv;
static uint8_t s_cascade;
static volatile uint8_t s_fault;
static volatile uint8_t s_ready;
static volatile uint8_t s_output_on;
static uint8_t s_oc_count;
static uint8_t s_ovp_count;

/* ==== 内部函数 ==== */
/**
 * @说明: 按给定参数初始化一个 PID 实例
 * @输入: pid 实例；kp/ki 增益；upper/lower 输出限幅
 * @输出: 无
 */
static void app_control_init_pid(algo_pid_t *pid, float kp, float ki,
                                 float upper, float lower)
{
  algo_pid_init_t init = {0};

  init.kp = kp;
  init.ki = ki;
  init.kd = 0.0f;
  init.setpoint = 0.0f;
  init.output_upper_limit = upper;
  init.output_lower_limit = lower;
  init.default_output = 0.0f;

  ALGO_PID_Init(pid, &init);
}

/**
 * @说明: 锁存故障并立即关闭 PWM
 * @输出: 无
 * @注意: 故障锁存后只能重新调用 APP_CONTROL_Init 清除
 */
static void app_control_fault(void)
{
  s_fault = 1U;
  DRV_TIM_Stop();
}

/**
 * @说明: 组装并推送一帧 RTT 遥测
 * @输入: vout_mv 输出电压；iout_ma 输出电流；ccr 当前比较值
 * @输出: 无
 * @注意: 故障分支也要调用，便于抓到停机瞬间的电流/电压
 */
static void app_control_report(int32_t vout_mv, int32_t iout_ma, uint16_t ccr)
{
  app_telemetry_frame_t frame;

  frame.t_us = (uint32_t)s_now_us;
  /* 上报目标设定值而非软启动后的给定值：关输出时也能看到档位，且阶跃判据以目标为准 */
  frame.sp_mv = (int32_t)s_vref_target_mv;
  frame.pv_mv = vout_mv;
  frame.ccr = ccr;
  frame.iout_ma = iout_ma;

  if (s_cascade == 0U)
  {
    frame.kp_milli = APP_CONTROL_START_KP_MILLI;
    frame.ki_milli = APP_CONTROL_START_KI_MILLI;
  }
  else
  {
    frame.kp_milli = APP_CONTROL_VOLT_KP_MILLI;
    frame.ki_milli = APP_CONTROL_VOLT_KI_MILLI;
  }

  frame.cascade = s_cascade;
  frame.fault = s_fault;
  frame.output = s_output_on;

  APP_TELEMETRY_Push(&frame);
}

/**
 * @说明: 读取 DWT 周期计数并换算为距上次调用的微秒数
 * @输出: 实际经过时间，单位 us
 * @注意: CYCCNT 为 32 位，调用间隔需远小于 59.6s 回绕周期
 */
static uint32_t app_control_elapsed_us(void)
{
  uint32_t now_cycles = DWT->CYCCNT;
  uint32_t delta_cycles = now_cycles - s_last_cycles;

  s_last_cycles = now_cycles;

  return (delta_cycles + (APP_CONTROL_DWT_MHZ / 2U)) / APP_CONTROL_DWT_MHZ;
}

/* ==== 接口函数 ==== */
/**
 * @说明: 初始化电压环、电流环和软启动状态
 * @输入: 无
 * @输出: 无
 * @注意: 必须在 APP_ADC_Init 前调用，确保控制状态已就绪
 * @来源 memory CTRL_VOLT_KP=0.050 A/V 推导：fc≈50Hz
 * @来源 memory CTRL_CURR_KP=0.020 duty/A 推导：fc≈500Hz
 */
void APP_CONTROL_Init(void)
{
  app_control_init_pid(&s_pid_start, APP_CONTROL_START_KP, APP_CONTROL_START_KI,
                       APP_CONTROL_DUTY_MAX, 0.0f);
  app_control_init_pid(&s_pid_volt, APP_CONTROL_VOLT_KP, APP_CONTROL_VOLT_KI,
                       APP_CONTROL_ILIMIT_A, 0.0f);
  app_control_init_pid(&s_pid_curr, APP_CONTROL_CURR_KP, APP_CONTROL_CURR_KI,
                       APP_CONTROL_DUTY_MAX, 0.0f);

  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  s_last_cycles = 0U;
  s_adc_pending = 0U;
  s_overrun = 0U;

  s_now_us = 0U;
  s_vref_mv = 0U;
  s_vref_target_mv = APP_CONTROL_VREF_DEFAULT_MV;
  s_cascade = 0U;
  s_fault = 0U;
  s_output_on = 0U;
  s_oc_count = 0U;
  s_ovp_count = 0U;

  DRV_TIM_SetDuty(0U);
  s_ready = 1U;
}

/**
 * @说明: 设置输出电压目标值，超范围时钳位到 3.3V–12V
 * @输入: voltage 目标电压，单位 V
 * @输出: 无
 * @注意: 目标值经软启动斜坡后才进入电压环
 */
void APP_CONTROL_SetVoltage(float voltage)
{
  if (voltage < 3.3f)
  {
    voltage = 3.3f;
  }
  else if (voltage > 12.0f)
  {
    voltage = 12.0f;
  }

  s_vref_target_mv = (uint32_t)(voltage * 1000.0f + 0.5f);
}

/**
 * @说明: 查询故障锁存状态
 * @输出: 1=有故障，0=正常
 */
uint8_t APP_CONTROL_IsFault(void)
{
  return s_fault;
}

/**
 * @说明: 开启或关闭电压输出
 * @输入: on 1=开启，0=关闭
 * @输出: 无
 * @注意: 关闭时占空比立即归 0、软启动与级联状态复位，重新开启从 0V 软启动
 * @注意: 关闭不改变设定值 s_vref_target_mv，重新开启仍输出原设定电压
 */
void APP_CONTROL_SetOutput(uint8_t on)
{
  if (on == 0U)
  {
    s_output_on = 0U;
    s_vref_mv = 0U;
    s_cascade = 0U;
    ALGO_PID_Reset(&s_pid_start);
    ALGO_PID_Reset(&s_pid_volt);
    ALGO_PID_Reset(&s_pid_curr);
    s_last_ccr = 0U;
    DRV_TIM_SetDuty(0U);
  }
  else if (s_output_on == 0U)
  {
    /* 从 0V 重新软启动，避免直接跳到设定值 */
    s_vref_mv = 0U;
    s_output_on = 1U;
  }
  else
  {
    /* 已处于开启状态，无需处理 */
  }
}

/**
 * @说明: 查询输出使能状态
 * @输出: 1=输出开启，0=输出关闭
 */
uint8_t APP_CONTROL_GetOutput(void)
{
  return s_output_on;
}

/**
 * @说明: ADC 完成后在中断内执行快速过压/过流保护
 * @输入: g_drv_adc_frame 已由 DRV_ADC_ConvCpltIsr 更新
 * @输出: 无，故障时锁存并关闭 PWM
 * @注意: 必须在 DRV_ADC_ConvCpltIsr 之后调用
 */
void APP_CONTROL_ProtectIsr(void)
{
  uint32_t raw_i;
  uint32_t raw_v;
  int32_t iout_ma;
  int32_t vout_mv;

  if ((s_ready == 0U) || (s_fault != 0U))
  {
    return;
  }

  raw_i = g_drv_adc_frame[DRV_ADC_IDX_I];
  raw_v = g_drv_adc_frame[DRV_ADC_IDX_V];
  iout_ma = (int32_t)(raw_i * BSP_ADC_I_MA_NUM / BSP_ADC_I_MA_DEN);
  vout_mv = (int32_t)(raw_v * BSP_ADC_V_MV_NUM / BSP_ADC_V_MV_DEN);

  /* 过压保护：连续 3 次超过 13.5V 才锁存故障 */
  if (vout_mv > APP_CONTROL_OVP_MV)
  {
    if (s_ovp_count < APP_CONTROL_OVP_COUNT)
    {
      s_ovp_count++;
    }

    if (s_ovp_count >= APP_CONTROL_OVP_COUNT)
    {
      app_control_fault();
      return;
    }
  }
  else
  {
    s_ovp_count = 0U;
  }

  /* 过流保护：恒流给定 3A，超过 3.3A 连续 3 次才跳闸 */
  if (iout_ma >= APP_CONTROL_OC_TRIP_MA)
  {
    if (s_oc_count < APP_CONTROL_OC_COUNT)
    {
      s_oc_count++;
    }

    if (s_oc_count >= APP_CONTROL_OC_COUNT)
    {
      app_control_fault();
      return;
    }
  }
  else
  {
    s_oc_count = 0U;
  }
}

/**
 * @说明: 主循环控制入口，执行软启动和电压/电流双环
 * @输入: g_drv_adc_frame 已由 DRV_ADC_ConvCpltIsr 更新
 * @输出: 无，更新 TIM1 CCR 与故障状态
 * @注意: 由 APP_CONTROL_Task 在主循环按 DMA 事件调用
 * @注意: 启动阶段用电压环，占空比≥10% 后切电流内环
 * @注意: 电流采样在占空比 <10% 时无效，因此内环启用前必须用电压环启动
 * @来源 memory CTRL_LOOP_HZ=10000 推导：ADC 10kHz/1
 * @来源 memory ADC_V_SCALE=4.4233mV/码 实测 2026-11-04
 * @来源 memory ADC_I_SCALE=1.6589mA/码 实测 2026-11-04
 */
void APP_CONTROL_Update(void)
{
  uint16_t raw_i;
  uint16_t raw_v;
  int32_t iout_ma;
  int32_t vout_mv;
  float iout;
  float vout;
  float iref;
  float duty;
  uint16_t ccr;
  uint32_t dt_us;
  uint32_t slew_mv;

  /* 初始化完成前不运行，避免访问未初始化状态 */
  if (s_ready == 0U)
  {
    return;
  }

  /* 使用 DWT 实际经过时间，异常阻塞时限制 PID 看到的最大 dt */
  dt_us = app_control_elapsed_us();
  if (dt_us > APP_CONTROL_DT_MAX_US)
  {
    dt_us = APP_CONTROL_DT_MAX_US;
  }
  if (dt_us == 0U)
  {
    dt_us = 1U;
  }
  s_now_us += dt_us;

  /* 读取本周期电流/电压采样并换算为 A/V */
  DRV_ADC_GetLatestFrame(&raw_i, &raw_v);
  iout_ma = (int32_t)(raw_i * BSP_ADC_I_MA_NUM / BSP_ADC_I_MA_DEN);
  vout_mv = (int32_t)(raw_v * BSP_ADC_V_MV_NUM / BSP_ADC_V_MV_DEN);
  iout = (float)iout_ma * 0.001f;
  vout = (float)vout_mv * 0.001f;

  /* 故障锁存后保持 PWM 关闭，直到重新初始化；仍推一帧遥测便于定位停机瞬间 */
  if (s_fault != 0U)
  {
    app_control_report(vout_mv, iout_ma, s_last_ccr);
    return;
  }

  /* 输出关闭：占空比保持 0，仍推遥测便于观察输出电压回落 */
  if (s_output_on == 0U)
  {
    if (s_last_ccr != 0U)
    {
      s_last_ccr = 0U;
      DRV_TIM_SetDuty(0U);
    }

    app_control_report(vout_mv, iout_ma, 0U);
    return;
  }

  /* 目标电压软启动：按实际 dt 保持 240mV/ms 斜坡 */
  slew_mv = (APP_CONTROL_VREF_SLEW_MV * dt_us + 50U) / 100U;
  if (slew_mv == 0U)
  {
    slew_mv = 1U;
  }

  if (s_vref_mv < s_vref_target_mv)
  {
    s_vref_mv += slew_mv;
    if (s_vref_mv > s_vref_target_mv)
    {
      s_vref_mv = s_vref_target_mv;
    }
  }
  else if (s_vref_mv > s_vref_target_mv)
  {
    if ((s_vref_mv - s_vref_target_mv) > slew_mv)
    {
      s_vref_mv -= slew_mv;
    }
    else
    {
      s_vref_mv = s_vref_target_mv;
    }
  }

  if (s_cascade == 0U)
  {
    /* 启动阶段：电压环直接输出占空比 */
    ALGO_PID_SetSetpoint(&s_pid_start, (float)s_vref_mv * 0.001f);
    duty = ALGO_PID_Compute1(&s_pid_start, vout, s_now_us);

    /* 占空比达到 10% 后电流采样有效，准备切入电流内环 */
    if (duty >= APP_CONTROL_CURRENT_VALID_DUTY)
    {
      s_cascade = 1U;

      /* 无扰切换：外环积分器预置为实测电流，内环预置为当前占空比 */
      ALGO_PID_SetSetpoint(&s_pid_volt, (float)s_vref_mv * 0.001f);
      s_pid_volt.i_term = iout;
      s_pid_volt.last_error = (float)s_vref_mv * 0.001f - vout;
      s_pid_volt.last_time_us = s_now_us;
      s_pid_curr.i_term = duty;
      s_pid_curr.last_time_us = s_now_us;
    }
  }
  else
  {
    /* 正常阶段：电压外环给 0–3A 电流给定，电流内环给占空比 */
    ALGO_PID_SetSetpoint(&s_pid_volt, (float)s_vref_mv * 0.001f);
    iref = ALGO_PID_Compute1(&s_pid_volt, vout, s_now_us);
    ALGO_PID_SetSetpoint(&s_pid_curr, iref);
    duty = ALGO_PID_Compute1(&s_pid_curr, iout, s_now_us);
  }

  /* 两个 PID 的输出限幅已各自生效，这里只做 duty→CCR 换算 */
  ccr = (uint16_t)(duty * APP_CONTROL_CCR_PER_DUTY + 0.5f);

  /* 写入 CCR */
  s_last_ccr = ccr;
  DRV_TIM_SetDuty(ccr);

  app_control_report(vout_mv, iout_ma, ccr);
}

/**
 * @说明: DMA 完成中断内标记一帧新数据，并统计主循环来不及消费的次数
 * @输出: 无
 * @注意: 只置标志与计数，不在中断内运行 PID
 */
void APP_CONTROL_OnAdcIsr(void)
{
  /* 上一帧还没被主循环消费就来了新帧，记一次丢帧 */
  if (s_adc_pending != 0U)
  {
    s_overrun++;
  }

  s_adc_pending = 1U;
}

/**
 * @说明: 主循环控制任务，有新帧时运行一次控制
 * @输出: 无
 * @注意: 无新帧时立即返回；积压多帧时只处理最新一帧
 */
void APP_CONTROL_Task(void)
{
  if ((s_ready == 0U) || (s_adc_pending == 0U))
  {
    return;
  }

  s_adc_pending = 0U;
  APP_CONTROL_Update();
}
