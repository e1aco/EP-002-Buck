/*****************************************************************************
 * @文件: drv_pwm.c
 * @作者: cl
 * @日期: 2026-11-06
 * @版本: v2.1
 * @说明: PWM 驱动（PA8=TIM1_CH1→LIN#、PA7=TIM3_CH2→HIN）
 *   单输入模式（DRV_PWM_SINGLE_INPUT=1）：两路同相同宽，死区由 EG2131 内部互锁产生
 *   双通道模式（=0）：内脉冲两端各缩 DT，死区由 CCR2 内缩产生
 * @平台: STM32F103C8T6
 * @依赖: Core 的 tim.c（htim1 已配 PSC=0/ARR=3599 → 20kHz）；TIM3 在本层配置
 * @注意: 两路都直接由 MCU 推挽输出（快沿），不依赖 BDTR/反相器
 ****************************************************************************/
#include "drv_pwm.h"
#include "drv_adc.h"
#include "tim.h"
#include "main.h"

/* ==== 私有变量 ==== */

/* TIM3 句柄：第二路 PWM（PA7/TIM3_CH2，内脉冲 → EG2131 HIN）
 * 不进 Core/tim.c，因为 CubeMX 未配置 TIM3；与 .ioc 的差异在 .cl/require.md 引脚表登记 */
static TIM_HandleTypeDef s_htim3;

/* 上一次下发的 CCR1（外脉冲宽度），用于内脉冲的安全限幅 */
static uint16_t s_ccr1_prev = 0U;

/* 待补写的内脉冲（0 = 无）：大步长上调时先按旧外脉冲算，等 CCR1 提交后再补
 * @来源 2026-11-04 实测：单次 SetDutyCount 上调时内脉冲停在上一次的值，
 *        开环档位实际只有上一档的内脉冲（CCR1=1520 时 CCR2 仍是 540） */
static volatile uint16_t s_inner_pending;
static uint32_t s_inner_due_ms;

/* 补写延迟：TIM1 RCR=9 → CCR1 影子寄存器每 10 个 PWM 周期（500µs）装一次 */
#define DRV_PWM_INNER_DEFER_MS    2U

/* 当前生效的占空比下限：默认正常运行下限，启动期由 DRV_PWM_SetDutyMin 放开到 2×DT */
static uint16_t s_duty_min = DRV_PWM_DUTY_CNT_MIN;

/* ==== 私有函数 ==== */

/**
 * @说明: 配置 TIM3 为 TIM1 的从定时器（触发模式），输出内脉冲到 PA7
 * @注意: 必须在 TIM1 已初始化后调用（ARR 取自 TIM1）；TIM3 时钟由 BSP_PWM_InitPins 使能
 * @来源 memory PWM_HIN_PIN=PA7/TIM3_CH2 推导：TIM3_CH2 默认复用落 PA7，无需重映射
 *        PWM_INNER_CCR2=CCR1−2×DEADTIME：外脉冲 [DT,CCR1)、内脉冲 [DT,CCR1−DT)
 * @来源 通用知识-待复核 RM0008 从模式=触发(110)：首个 TRGI 启动计数，此后自由运行
 */
static void DRV_PWM_Tim3Init(void)
{
  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_SlaveConfigTypeDef sSlaveConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim1);

  s_htim3.Instance = TIM3;
  s_htim3.Init.Prescaler = 0;
  s_htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  s_htim3.Init.Period = arr;                       /* 与 TIM1 同 ARR → 同频 20kHz */
  s_htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  s_htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&s_htim3) != HAL_OK)
  {
    Error_Handler();
  }

  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&s_htim3, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&s_htim3) != HAL_OK)
  {
    Error_Handler();
  }

  /* 从模式 = 触发模式（ITR0 = TIM1_TRGO）：首次 TRGO 启动计数，此后自由运行；
   * 两定时器同源同频同 ARR，启动对齐后不会漂移，无需每周期重同步 */
  sSlaveConfig.SlaveMode = TIM_SLAVEMODE_TRIGGER;
  sSlaveConfig.InputTrigger = TIM_TS_ITR0;
  if (HAL_TIM_SlaveConfigSynchro(&s_htim3, &sSlaveConfig) != HAL_OK)
  {
    Error_Handler();
  }

  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&s_htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }

  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0U;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&s_htim3, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }

  /* 相位预置：双通道从 (ARR+1−DT) 起算 → 首次溢出比 TIM1 晚 DT，形成内脉冲上升沿；
   * 单输入（DT=0）算出 3600 = ARR+1 → 回绕取 0 → 与外脉冲同相 */
  {
    uint32_t phase = (arr + 1U) - DRV_PWM_DEADTIME_CNT;
    if (phase > arr)
    {
      phase = 0U;
    }
    __HAL_TIM_SET_COUNTER(&s_htim3, phase);
  }
}

/* ==== 接口函数 ==== */

/**
 * @说明: 启动 PWM（PA8→LIN#、PA7→HIN；单输入时两路同相，死区交 EG2131 内部）
 * @注意: 启动顺序 = TIM3 先（等触发）、TIM1 后，最后给 TIM1 一次 UG 产生 TRGO 启动 TIM3；
 *        PA7 走 TIM3_CH2，因此**不能**再开 TIM1_CH1N（部分重映射会抢占 PA7）
 * @来源 memory PWM_LIN_PIN=PA8/TIM1_CH1、PWM_HIN_PIN=PA7/TIM3_CH2、
 *        PWM_INNER_CCR2=CCR1−2×DEADTIME（内脉冲两端各缩 DT）
 *        TIM1_PWM_FREQ=20kHz 推导：72MHz/((0+1)*(3599+1))=20kHz
 */
void DRV_PWM_Init(void)
{
  TIM_OC_InitTypeDef sConfigOC = {0};

  DRV_PWM_Tim3Init();

  /* 通道 1：PWM1、高有效；初值 0 = 上电停波态（用户 2026-11-06：启动时输出必须关闭）
   * CCR1=0 且 CCR2=0 → HIN=0/LIN#=0 → 仅下管开、上管关，输出 0V（安全态）
   * 预置 50% 会让 MOE 打开到 APP_CONTROL_Init 之间出现一次 ~12V 的短暂导通
   * @来源 memory PWM_STOP_MUST_ZERO（停机必须 CCR1=CCR2=0） */
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0U;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_TIM_PWM_Start(&s_htim3, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }

  /* 强制一次更新事件：把 CCR1 预置值装进影子寄存器，同时产生首个 TRGO 启动 TIM3
   * @来源 联网-待复核 RM0008 p.353 CR1.URS/TIMx_EGR.UG：置 UG 立即重装计数器并产生 UEV */
  htim1.Instance->EGR = TIM_EGR_UG;
}

/**
 * @说明: 设置占空比（TIM1 CCR1 + TIM3 CCR2；单输入时两者相同，双通道时 CCR2=CCR1−2×DT）
 * @输入: count 外脉冲宽度（CCR1 计数），越界截断到 [s_duty_min, DRV_PWM_DUTY_CNT_MAX]，
 *        s_duty_min 默认 DRV_PWM_DUTY_CNT_MIN，启动期可由 DRV_PWM_SetDutyMin 放开到 2×DT
 * @注意: 内脉冲 = CCR1 − 2×DT，必须始终窄于外脉冲，否则 EG2131 出现 (HIN=1,LIN#=0) 非法态；
 *        CCR1 要等 TIM1 UEV(≤0.5ms) 提交、CCR2 在 TIM3 UEV(≤50µs) 就提交，
 *        故"大步长上调"时内脉冲先按旧值算，等下一拍再跟上（安全方向，仅滞后一拍）
 * @来源 memory PWM_INNER_CCR2=CCR1−2×DEADTIME、EG2131 真值表：仅 (1,1)→HO、(0,0)→LO
 */
void DRV_PWM_SetDutyCount(uint16_t count)
{
  uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim1);
  uint32_t dt2 = 2U * DRV_PWM_DEADTIME_CNT;
  uint16_t prev = s_ccr1_prev;
  uint32_t inner;

  if ((uint32_t)count > arr)
  {
    count = (uint16_t)arr;
  }
  if (count > DRV_PWM_DUTY_CNT_MAX)
  {
    count = DRV_PWM_DUTY_CNT_MAX;
  }
  if (count < s_duty_min)
  {
    count = s_duty_min;
  }
  /* 兜底：内脉冲 = count − 2×DT 必须 ≥ 0，否则 uint16 下溢会把内脉冲算成超宽 */
  if ((uint32_t)count < dt2)
  {
    count = (uint16_t)dt2;
  }

  /* 安全限幅：新值比旧值大且步长 > 2×DT 时，内脉冲先按旧值算（防内脉冲暂时宽于外脉冲）
   * 目标值存进 pending，由 DRV_PWM_Loop 在 CCR1 提交后补写——否则单次调用永远停在旧值
   * 单输入（dt2=0）不走此分支：内脉冲 = 外脉冲，立即同相下发 */
  if ((dt2 > 0U) && (count > prev) && ((uint32_t)(count - prev) > dt2) && ((uint32_t)prev > dt2))
  {
    inner = (uint32_t)prev - dt2;
    s_inner_pending = (uint16_t)((uint32_t)count - dt2);
    s_inner_due_ms = HAL_GetTick() + DRV_PWM_INNER_DEFER_MS;
  }
  else
  {
    inner = (uint32_t)count - dt2;
    s_inner_pending = 0U;
  }

  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, count);
  __HAL_TIM_SET_COMPARE(&s_htim3, TIM_CHANNEL_2, (uint16_t)inner);
  s_ccr1_prev = count;

  DRV_ADC_TrigFollowPwm();
}

/**
 * @说明: 设置占空比下限（启动期放开地板到 2×DT，让软启动能从 0V 建压）
 * @输入: count 期望下限，内部夹到 [DRV_PWM_DUTY_CNT_BOOT_MIN, DRV_PWM_DUTY_CNT_MIN]
 * @注意: 只影响 SetDutyCount 的截断；DRV_PWM_Stop 仍是 CCR1=CCR2=0 的真停波
 * @来源 memory PWM_DT：地板低于 2×DT 时 CCR2 = CCR1−2×DT 会下溢
 */
void DRV_PWM_SetDutyMin(uint16_t count)
{
#if (DRV_PWM_DUTY_CNT_BOOT_MIN > 0U)
  if (count < DRV_PWM_DUTY_CNT_BOOT_MIN)
  {
    count = DRV_PWM_DUTY_CNT_BOOT_MIN;
  }
#endif
  if (count > DRV_PWM_DUTY_CNT_MIN)
  {
    count = DRV_PWM_DUTY_CNT_MIN;
  }
  s_duty_min = count;
}

/**
 * @说明: 停波：两路都置 0（上管关、下管常通），绕过 DUTY_CNT_MIN 下限截断
 * @注意: 这是故障/失联的安全态；下限 DRV_PWM_DUTY_CNT_MIN 只约束**正常运行**的占空比范围
 * @来源 memory EG2131 真值表：HIN=0、LIN#=0 → LO=1/HO=0（下管续流，上管关断）
 */
void DRV_PWM_Stop(void)
{
  s_inner_pending = 0U;
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0U);
  __HAL_TIM_SET_COMPARE(&s_htim3, TIM_CHANNEL_2, 0U);
  s_ccr1_prev = 0U;

  DRV_ADC_TrigFollowPwm();
}

/**
 * @说明: 主循环调用：把安全限幅时挂起的内脉冲补写下去
 * @注意: 只在 pending 非 0 且延迟到点时才写；闭环 2kHz 连续调用时 pending 每拍被覆盖，
 *        由下一次 SetDutyCount 的"旧值"分支自然跟上（等价于滞后一拍），本函数不介入
 */
void DRV_PWM_Loop(void)
{
  uint16_t pending = s_inner_pending;

  if (pending == 0U)
  {
    return;
  }
  if ((int32_t)(HAL_GetTick() - s_inner_due_ms) < 0)
  {
    return;
  }

  __HAL_TIM_SET_COMPARE(&s_htim3, TIM_CHANNEL_2, pending);
  s_inner_pending = 0U;
}
