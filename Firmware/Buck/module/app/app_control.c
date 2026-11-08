/*****************************************************************************
 * @文件: app_control.c
 * @作者: cl
 * @日期: 2026-11-06
 * @版本: v1.4
 * @说明: 闭环控制（级联双环：电压外环 → 电流内环；前馈 + 限幅 + 抗积分饱和 + 软启动
 *        + 过压/失联保护 + 按键输出开关 + 5V/12V 档位切换）
 * @平台: STM32F103C8T6
 * @依赖: drv_pwm（执行器 CCR1）、drv_adc（传感器 raw_v/raw_i）
 * @注意: 控制步进挂在 ADC 完成中断（2kHz），与采样严格同拍；主循环只做失联保护
 ****************************************************************************/
#include "app_control.h"
#include "config.h"
#include "drv_adc.h"
#include "drv_pwm.h"
#include "bsp_adc_scale.h"
#include "main.h"
#include "service_dbg.h"

/* ==== 标度与限幅（全部来自 .cl/memory，改这里之前先改 memory） ==== */

/* 输出电压/电流换算：板参单源 = bsp_adc_scale.h（app_telemetry 同一份，勿各写一套）
 * Vout[mV] = raw_v × 4423/1000、Iout[mA] = raw_i × 1659/1000
 * @来源 memory ADC_CAL_V/ADC_CAL_I（2026-11-04 四点实测）：见 bsp_adc_scale.h */
#define APP_CTRL_MV_NUM            BSP_ADC_V_MV_NUM
#define APP_CTRL_MV_DEN            BSP_ADC_V_MV_DEN

/* 执行器前馈换算：CCR1 = 0.15264×Vout[mV] + OFFSET(DT)
 * 2026-11-07 闭环扫档重标（10Ω，SP=5/7/9/11/12V，2ms 采样、取后 80ms 稳态均值）：
 *   实测 CCR1 = 1229.9/1534.7/1839.8/2145.1/2298.8，线性拟合残差 ≤0.6 码（≤4mV）
 *   → 标定点 DT=220 时 OFFSET_REF = 466.5（含 2×DT=440 与损耗项 26.5）
 * 与理论式 150×V/1000+440 差 +33…+59 码（IL、开关管压降）；与旧 155×V/1000+235
 * 差 −220…−250 码 —— 旧值按轻载 DCM 四点拟合，10Ω 下偏低 → 前馈失效
 * 有效占空比 = (CCR1−2×DT)/3600，故 DT 变化时同电压所需 CCR1 平移 2×(220−DT)：
 *   OFFSET = 466.5 − 2×(220 − DRV_PWM_DEADTIME_CNT)
 *   例：DT=220 → 466.5；DT=150 → 326.5；DT=100 → 226.5；DT=36 → 98.5；DT=0（单输入）→ 26.5
 * @来源 memory DUTY_MAP_CAL（2026-11-07 闭环重标，标定点 DT=220）
 */
#define APP_CTRL_CCR_PER_MV        0.15264f
#define APP_CTRL_CCR_OFFSET_REF_DT 220.0f
#define APP_CTRL_CCR_OFFSET_REF    466.5f
#define APP_CTRL_CCR_DT_DELTA      ((float)DRV_PWM_DEADTIME_CNT - APP_CTRL_CCR_OFFSET_REF_DT)
#define APP_CTRL_CCR_OFFSET        (APP_CTRL_CCR_OFFSET_REF + 2.0f * APP_CTRL_CCR_DT_DELTA)

/* 输出限幅：按同一映射取 16.73V 对应 CCR1（随 DT 平移），守住用户 18V 硬约束
 * 为什么取 16.73V 而不是 18V：超过 18V 后 ADC 分压饱和（满量程 18V）→ 反馈失效、
 * 引脚进钳位；且轻载 DCM 下同一 CCR1 电压更高
 * 注意：CCR1 上限只是结构性兜底，**真正保证 ≤18V 的是基于实测电压的 OVP（17000mV）**
 * @来源 memory DUTY_MAP_CAL + 用户要求「任何情况 Vout<=18V」
 *        推导：CCR1_MAX = 0.15264×16730 + OFFSET（DT=220→3020；DT=0→2580） */
#define APP_CTRL_CCR1_MIN          ((float)DRV_PWM_DUTY_CNT_MIN)   /* = 15% + 2×DT，随 DT 自动 */
#define APP_CTRL_CCR1_MAX          (APP_CTRL_CCR_PER_MV * 16730.0f + APP_CTRL_CCR_OFFSET)

/* 启动期占空比地板（只对升压斜坡开放）：2×DT 计数 → 内脉冲=0（上管全关）
 * 不能再低：CCR1 < 2×DT 时 CCR2 = CCR1−2×DT 会 uint16 下溢，内脉冲宽于外脉冲
 * → EG2131 进 (HIN=1,LIN#=0) 非法态（直通风险）
 * 为什么放开：正常运行下限（15%+2×DT）开环已能推出 ~4.8V，软启动被地板顶住
 * （实测 0→5V 峰 +8.98%、0→12V 前 50ms 卡 3.35V）
 * @来源 memory PWM_DT 推导：地板 = 2×DT；DRV_PWM_DUTY_CNT_BOOT_MIN
 */
#define APP_CTRL_CCR1_BOOT_MIN     ((float)DRV_PWM_DUTY_CNT_BOOT_MIN)

/* 增益初值（保守，靠调参收敛）
 * 环路增益 = Kp × dVout/dCCR1 = 0.1 × 6.55mV ≈ 0.66（<1，先保证不振荡）
 * 积分时间 Ti = Kp×Ts/Ki = 0.1×0.5ms/0.0025 = 20ms（远慢于 2kHz，避免与采样拍频）
 * @来源 memory DUTY_MAP_CAL 推导：dCCR1/dVout = 152.6 counts/V = 0.153 counts/mV */
#if (CL_CTRL_MODE == CL_CTRL_MODE_VOLT_ONLY)
#define APP_CTRL_KP_V              0.10f
#define APP_CTRL_KI_V              0.0025f
#else
/* 双环：外环输出单位改为「电流参考 mA」，不再是 CCR1 计数
 * 被控对象：Iout→Vout ≈ R，最轻载 R=10Ω（单环实测负载）→ dVout/dIout = 10 mV/mA
 * Kp：最轻载环路增益取 0.2（<1）→ Kp = 0.2/10 = 0.02 mA/mV
 * Ti：10ms（约内环带宽 100Hz 的 1/10）→ Ki = Kp×Ts/Ti = 0.02×0.5ms/10ms = 0.001/步
 * @来源 memory IOUT_MAX=3A、VOUT_MIN=5V → R_min=1.67Ω；单环带载实测 10Ω 负载（state 已完成任务） */
/* 实测整定（2026-11-06 2ms RTT 阶跃，10Ω/220µF）：
 * 0.01/0.0005 稳态好但收尾 ~600ms（慢在 PI 尾巴）；
 * 提到 0.03/0.002 后 5→12V 50ms 进 ±1%、峰 +0.99%，12→5V ~130ms、无下冲 */
#define APP_CTRL_KP_V              0.03f
#define APP_CTRL_KI_V              0.002f
#endif

/* 目标电压与保护 */
/* 两档目标：按键 Key2 在 5V / 12V 之间切换（用户 2026-11-06）
 * 上电默认 5V：用户要求「每次开机默认是 5V 输出」，且上电即输出（软启动到 5V）
 * @来源 你（2026-11-06 按键功能需求）；输出规格 5–12V 见 .cl/require.md */
#define APP_CTRL_VREF_MV_5V        5000U
#define APP_CTRL_VREF_MV_12V       12000U
#define APP_CTRL_VREF_MV_DEF       APP_CTRL_VREF_MV_5V   /* 上电默认档位 = 5V */
#define APP_CTRL_OVP_MV            17000U   /* 过压阈值（实测电压）：18V 硬约束留 1V 裕量 */
#define APP_CTRL_OVP_HOLDOFF_MS    500U     /* 触发后保持下限 500ms，再重新软启动 */
#define APP_CTRL_SOFTSTART_STEPS   500U     /* 斜坡步数上限（升压按「实际前进」计数，降压按拍计数） */
/* 参考电压步长 120mV/步；升压段由输出跟随门控（见 APP_CTRL_RAMP_LAG_MV），
 * 不再是固定 240V/s —— 固定斜率会跑到输出前面，误差全甩给外环积分
 * 降压靠参考限速 + 电流参考限速共同承担，避免输出先塌再被抬起来
 * @来源 memory TIM4_TRIG_FREQ=2kHz 推导：12000mV/100 步=120mV/步 */
#define APP_CTRL_VREF_SLEW_UP_MV   120U
#define APP_CTRL_VREF_SLEW_DN_MV   120U

/* 升压斜坡门控：vref 只在 Vout 跟上（滞后 ≤ 本值）时才继续前进
 * 为什么：固定 240V/s 斜坡在 21ms 就把 5V 走完，而 Vout 此时仅 3.9V，
 * 之后只能靠外环积分追赶 → 实测 3.35V 平台 + ~140ms 尾巴（2026-11-07）
 * 取 800mV：够大的误差让外环积分爬得动（原 0→5V 216ms / 0→12V 400ms），
 * 又小到不会把输出推过目标（实测两颗都是单调整定、无过冲）
 * 启动段爬升速度另由 APP_CTRL_BOOT_KI_MULT 放大（见该宏），门控仍守住 800mV 上限
 * 试过的方案（2026-11-07 实测）：400mV→290/660ms（太慢）；
 *   按负载线预置外环积分→106/98ms 但过冲 +5.6%/+2.0%（电流环在快斜坡下会超调）；
 *   允许滞后按剩余距离缩放→尾部过慢（12V 626ms）
 * @来源 memory CTRL_BOOT_DIP；10Ω 实测 5V/12V 启动波形 */
#define APP_CTRL_RAMP_LAG_MV       800U
/* 斜坡时间上限：输出跟不上时（无输入/重载）超时直接跳到目标，
 * 等价于旧固定斜坡的行为；否则 vref 会永远停在当前值（门控死锁）
 * @来源 推演：no-input 时 Vout 恒 0，vref 会卡在 ~400mV */
#define APP_CTRL_RAMP_TIMEOUT_MS   1200U

/* 启动段外环积分倍率：只在「输出距目标 > BOOT_BOOST_BAND_MV」时生效
 * 为什么：启动电流爬升率 = KI_V × err × f，err 被门控钳在 lag(800mV) →
 *   di/dt = 0.002 × 0.8 × 2000 = 3.2A/s；0.5A 要 156ms、1.2A 要 375ms，
 *   实测边沿 112/294ms、调节 216/400ms 正是这条常数在限速（与 220µF 无关）
 * 取 4 倍（2026-11-07 实测扫参，10Ω/220µF）：
 *   0→5V 边沿 32ms / 调节 72ms / 峰 +0.74%（瞬态无过冲，此为主升段后纹波）
 *   0→12V 76ms / 106ms / +0.33%
 *   5 倍更快（26/62ms）但 5V 峰在 +0.7%~+1.5% 间跳动，贴死 1% 门限；
 *   6 倍 0→5V 过冲 +1.90%（44ms 处）明确破线 → 取 4 倍留裕量
 * 仍低于 iref 限速 12mA/步 × 2kHz = 24A/s（mult ≤ 7.5 不撞），
 *   峰值电流受 IREF_MAX=3300mA 兜底
 * 换档升压（s_i_up_lim>0，外环冻结）不启用——那条路本来就不靠积分爬电流
 * @来源 memory CTRL_BOOT_5V/12V、CTRL_RAMP_LAG=800mV（2026-11-07 实测） */
#define APP_CTRL_BOOT_KI_MULT      4.0f
#define APP_CTRL_BOOT_KI_MULT_LIMIT 10.0f
/* 提速生效区间：输出还差目标 > 本值时才放大积分
 * 不能只在「vref 未到顶」时生效——vref 走完到 5V 时 vout 仅 4.5V，
 * 剩下 0.5V 全靠外环指数尾巴（实测 40ms 冲到 4.5V，之后 90ms 才收敛）
 * 取 50mV（大于 2ms 纹波 ~70mV 的一半，避免稳态来回切换） */
#define APP_CTRL_BOOT_BOOST_BAND_MV 50U


/* 电流参考限速：5mA/步 @2kHz = 10A/s（0→3.3A 用 330ms；12V↔5V 的 0.7A 用 70ms）
 * 直接跳变会把内环打到 CCR1 下限，12V→5V 瞬时塌到 3.35V（下冲 33%，2026-11-06 实测）
 * @来源 memory ADC_CAL_I/IOUT_MAX；2026-11-06 RTT 阶跃实测 */
#define APP_CTRL_IREF_SLEW_MA   12.0f

/* 降压「恒流过渡」：外环在降压瞬间会把 iref 打到 0，输出沿 L/C 自由塌陷。
 * 改为按 R=Vout/Iout 估出新档位稳态电流，过渡期只由内环维持该电流，
 * 输出沿负载线降到目标附近再交还外环。
 * @来源 2026-11-06 RTT 阶跃实测：12V→5V 下冲 15%/过冲 11%
 * @注意: 只对电阻性负载成立；非电阻负载靠 500ms 超时退回常规外环 */
#define APP_CTRL_TRANS_BAND_MV  500U
#define APP_CTRL_TRANS_TIMEOUT_MS  500U
#define APP_CTRL_LOST_MS           5U       /* ADC 停更超时 */

/* 无输入/输出短路检测（主循环做，毫秒级足够）：
 * 判据 = 已在驱动（CCR1>=下限）但 Vout 仍 < 800mV 持续 50ms → 输出升不起来
 * 动作 = 停波 + 200ms 后用一个低目标（3V，实际 ~4.8V @CCR1_MIN）探测，
 *        探测到电压响应才恢复 12V 闭环 —— 避免上电瞬间占空比已在 75%
 * @来源 2026-11-04 实测：断电时 Vout=0、参考 12V → PI 把 CCR1 顶到上限 3020 */
#define APP_CTRL_NOIN_MV           800U     /* 判定「输出没起来」的电压门限 */
#define APP_CTRL_NOIN_MS           50U      /* 持续时长 */
#define APP_CTRL_RETRY_MS          200U     /* 关断后重试间隔 */
#define APP_CTRL_PROBE_MV          3000U    /* 探测用低目标（CCR1 会被压到下限） */
/* 重试次数上限：连着这么多次探测都升不压 → 判空载/无输入，直接关机等按键。
 * 计数 = 每次「进入停机态」+1；探测成功 / 按键开关机时清零。
 * @来源 你（2026-11-08）：空载时 Vout≈0 会无限 200ms 重启，要求有限次后关机并显示 NO LOAD */
#define APP_CTRL_NOIN_RETRY_MAX    3U
/* 过压重试上限：空载时 PI 会把输出顶到 17V 触发 OVP，500ms 后自动重启，
 * 无负载时形成无限「冲压 → OVP → 停 500ms」循环（屏上 Vout 一直显示 0）。
 * 判据 = 5s 窗口内 OVP 次数 > 上限；窗口内没再触发就重新计数。
 * @来源 你（2026-11-08）：空载时不要无限重启，重试几次后关机并显示 NO LOAD */
#define APP_CTRL_OVP_RETRY_MAX     3U
#define APP_CTRL_OVP_RETRY_WIN_MS  5000U

/* 在线改增益的安全上下限（约 10× 推导初值；防误发大值直接振荡）
 * @来源 details/pid.md「apply 是高风险动作」+ 本文件推导初值（外环 0.02/0.001、内环 0.15/0.05） */
#define APP_CTRL_KP_V_LIMIT        0.50f
#define APP_CTRL_KI_V_LIMIT        0.05f
#define APP_CTRL_KP_I_LIMIT        2.00f
#define APP_CTRL_KI_I_LIMIT        0.50f

#if (CL_CTRL_MODE == CL_CTRL_MODE_VOLT_CURR)
/* 内环电流增益：输入误差 mA → 输出 CCR1 计数
 * 被控对象：CCR1→Iout ≈ Vin/(3600×R)；最重载 R=5V/3A=1.67Ω
 *        → 24V/(3600×1.67Ω) = 4.0 mA/计数
 * Kp：最重载环路增益取 0.6（<1，积分介入前不振荡）→ Kp = 0.6/4.0 = 0.15 计数/mA
 * Ti：3 个采样周期（3×0.5ms=1.5ms，给 1.5 拍采样延迟留相位）→ Ki = Kp×Ts/Ti
 *        = 0.15×0.5ms/1.5ms = 0.05/步
 * 电流参考上限 3300mA = 3A 额定 +10%（用户 2026-11-06 确认）
 * @来源 memory ADC_CAL_I=1.659mA/码（电流反馈标度）、IOUT_MAX=3A、VOUT_MIN=5V */
#define APP_CTRL_KP_I              0.15f
#define APP_CTRL_KI_I              0.05f
#define APP_CTRL_IREF_MAX_MA       3300U
#endif

/* ==== 私有变量 ==== */
static app_pid_t s_pid_v;                   /* 电压环（外环） */
#if (CL_CTRL_MODE == CL_CTRL_MODE_VOLT_CURR)
static app_pid_t s_pid_i;                   /* 电流环（内环） */
#endif

static volatile uint32_t s_vref_mv;         /* 目标电压（软启动斜坡值） */
static volatile uint32_t s_vref_target_mv;  /* 最终目标（探测期间临时改成 PROBE_MV） */
static uint32_t s_vref_user_mv;             /* 用户档位目标（5V/12V），不被探测覆盖 */
static volatile uint8_t s_enabled;          /* 输出使能：0 = 按键关机（ISR 只测量不下发） */
static volatile uint16_t s_ccr1_now;        /* 当前下发值 */
static volatile uint16_t s_vout_mv;         /* 最近一次输出电压 */
static volatile uint16_t s_iout_ma;         /* 最近一次输出电流 */
static volatile uint16_t s_iref_ma;         /* 内环电流参考（外环输出，单环模式恒 0） */
static float    s_iref_lim;                 /* 限速后的电流参考（mA），防阶跃打穿内环 */
static uint8_t  s_i_trans;                  /* 1 = 降压恒流过渡（外环旁路） */
static float    s_i_trans_ma;               /* 过渡目标电流（mA） */
static float    s_i_trans_now;              /* 过渡中实际参考（从当前值限速逼近目标） */
static float    s_i_up_lim;                 /* 升压斜坡期间的负载前馈电流（0 = 不启用） */
static uint32_t s_i_trans_ms;               /* 过渡开始时刻 */
static volatile uint8_t s_hold_off; /* 主循环判定的停机态：ISR 不得再驱动 CCR1 */
static uint8_t  s_noin;             /* 1 = 判无输入/短路，输出已关 */
static uint8_t  s_probe;            /* 1 = 低目标探测中 */
static uint8_t  s_noin_reported;    /* NOIN 只报一次，避免每 250ms 刷屏 */
static uint8_t  s_noin_tries;       /* 连续升压失败次数（探测成功/按键时清零） */
static uint8_t  s_noload;           /* 1 = 重试用尽后关机，屏显 NO LOAD，等按键 */
static uint32_t s_noin_ms;          /* 进入停机态的时刻 */
static uint32_t s_low_ms;           /* 连续低电压计时 */
static uint8_t  s_ovp;              /* 过压标志（供上层判读） */
static uint32_t s_ovp_ms;                   /* 过压进入时刻（冷却计时） */
static uint8_t  s_ovp_tries;                /* 窗口内 OVP 次数（判无限重启） */
static uint32_t s_ovp_last_ms;              /* 上一次 OVP 进入时刻（窗口起点） */
static volatile uint32_t s_last_count;      /* 上次见到的扫描计数（失联检测） */
static uint16_t s_softstep;                 /* 软启动剩余步数 */
static uint32_t s_ramp_ms;                  /* 本轮斜坡起始时刻（超时判据） */
static uint16_t s_ramp_lag_mv = (uint16_t)APP_CTRL_RAMP_LAG_MV;   /* 升压门控允许的参考-输出滞后（mV，RTT 可调） */
static float    s_boot_ki_mult = APP_CTRL_BOOT_KI_MULT;           /* 启动段外环积分倍率（RTT 可调） */

/* ==== PID 内核（单环/双环共用） ==== */

void APP_PID_Init(app_pid_t *pid, float kp, float ki, float omin, float omax)
{
  pid->kp = kp;
  pid->ki = ki;
  pid->out_min = omin;
  pid->out_max = omax;
  pid->i_acc = 0.0f;
  pid->out = 0.0f;
  pid->saturated = 0U;
}

float APP_PID_Step(app_pid_t *pid, float err, float ff)
{
  float p_term = pid->kp * err;
  float i_new = pid->i_acc + pid->ki * err;
  float out = ff + p_term + i_new; // P+I+FF

  if (out > pid->out_max)
  {
    out = pid->out_max;
    /* 抗积分饱和：输出顶到上限且误差仍为正（积分还要往上加）→ 冻结本步积分 */
    if (err > 0.0f)
    {
      i_new = pid->i_acc;
    }
    pid->saturated = 1U;
  }
  else if (out < pid->out_min)
  {
    out = pid->out_min;
    if (err < 0.0f)
    {
      i_new = pid->i_acc;
    }
    pid->saturated = 1U;
  }
  else
  {
    pid->saturated = 0U;
  }

  pid->i_acc = i_new;
  pid->out = out;

  return out;
}

/* ==== 私有函数 ==== */

/**
 * @说明: 把 Vout(mV) 换算成对应的 CCR1 计数（执行器模型）
 * @来源 memory DUTY_MAP_CAL（2026-11-07 闭环扫档重标）：CCR1 = 0.15264×Vout[mV] + 466.5
 */
static float app_control_ff_ccr1(uint32_t vout_mv)
{
  return (float)vout_mv * APP_CTRL_CCR_PER_MV + APP_CTRL_CCR_OFFSET;
}

/**
 * @说明: 当前生效的占空比地板（升压斜坡期间放开到 2×DT，其余为正常运行下限）
 * @注意: 只在 ISR 内调用；放开条件与软启动段一致（斜坡未到目标），
 *        5V→12V 升档时 ccr1 已在 1000 以上，放开不改变实际占空比
 * @来源 memory DRV_PWM_DUTY_CNT_MIN / DRV_PWM_DUTY_CNT_BOOT_MIN（随 DRV_PWM_DEADTIME_CNT 推导）
 */
static uint16_t app_control_duty_floor(void)
{
  if ((s_softstep > 0U) && (s_vref_mv < s_vref_target_mv))
  {
    return (uint16_t)APP_CTRL_CCR1_BOOT_MIN;
  }
  return (uint16_t)APP_CTRL_CCR1_MIN;
}

/**
 * @说明: 是否处于「启动升压」阶段（输出仍在向目标爬升，外环可用提速增益）
 * @输出: 1 = 是，0 = 否
 * @注意: 换档升压（s_i_up_lim>0）外环被冻结、不靠积分爬电流，故明确排除；
 *        降压/稳态（含纹波）由 band 排除。条件按「实测 vout」判，不按 vref——
 *        vref 到顶后仍有一段积分追赶，那一段正是原 0→5V 的主要耗时
 */
static uint8_t app_control_boot_boost(void)
{
  if (s_i_up_lim > 0.0f)
  {
    return 0U;
  }
  if (((int32_t)s_vref_target_mv - (int32_t)s_vout_mv) <= (int32_t)APP_CTRL_BOOT_BOOST_BAND_MV)
  {
    return 0U;
  }
  return 1U;
}

/**
 * @说明: 清零控制器积分（切换档位 / 保护动作 / 关机重开时用）
 * @注意: 双环模式必须内外环一起清；只清外环会让内环积分把占空比停在旧工作点
 */
static void app_control_reset_integrators(void)
{
  s_pid_v.i_acc = 0.0f;
#if (CL_CTRL_MODE == CL_CTRL_MODE_VOLT_CURR)
  s_pid_i.i_acc = 0.0f;
#endif
}

/**
 * @说明: 启动/重启软启动斜坡（步骤预算 + 超时计时一起复位）
 * @注意: 所有改 s_vref_target_mv 再要它爬坡的路径都必须调用本函数，
 *        否则 s_ramp_ms 还停在上一轮 → 超时判据立刻成立、斜坡被跳过
 */
static void app_control_ramp_start(void)
{
  s_softstep = APP_CTRL_SOFTSTART_STEPS;
  s_ramp_ms = HAL_GetTick();
}

/**
 * @说明: 重试用尽后的保护性关机（NOIN / OVP 共用）：停波、清积分、置 NO LOAD
 * @注意: 可由 ISR（OVP 分支）或主循环（NOIN 分支）调用，内部关中断保护状态一致性；
 *        置 s_noload 后只能按键重新开启，不再自动重启
 */
static void app_control_fault_off(void)
{
  uint32_t primask = __get_PRIMASK();

  __disable_irq();
  s_enabled = 0U;
  s_hold_off = 0U;
  s_noin = 0U;
  s_probe = 0U;
  s_ovp = 0U;
  s_noload = 1U;
  s_noin_reported = 0U;
  app_control_reset_integrators();
  s_vref_mv = 0U;
  s_iref_ma = 0U;
  s_iref_lim = 0.0f;
  s_i_up_lim = 0.0f;
  s_i_trans = 0U;
  s_ccr1_now = 0U;
  DRV_PWM_Stop();
  __set_PRIMASK(primask);
}

/* ==== 中断入口（由 Core/Src/stm32f1xx_it.c 的 ADC 回调调用） ==== */

/**
 * @说明: 控制步进（ADC 扫描完成中断里调，2kHz）
 * @注意: ISR 上下文；只做计算与下发，不做打印；所有分支都必须写一次 CCR1（避免停在上一个值）
 */
void APP_CONTROL_Step(void)
{
  uint16_t raw_v = 0U;
  uint16_t raw_i = 0U;
  uint32_t vout_mv;
  uint16_t floor_cnt;
  float err;
  float ccr1;

  DRV_ADC_GetRaw(&raw_v, &raw_i);

  /* 软启动/软降落：把参考往目标推进（升压按输出跟随门控，降压固定 120mV/步）
   * 降压必须限速：不限速时 12V→5V 直接下切，实测跌到 3.35V（下冲 33%）
   * 步长必须固定、不能按目标算：按目标算时 5V 档每步只有 51mV，100 步降不完 7V，
   * 实测 12V→5V 卡在 6.9V（2026-11-06 RTT 联调）
   * @来源 memory TIM4_TRIG_FREQ=2kHz 推导：12000mV/100 步 = 120mV/步 = 240V/s */
  if (s_softstep > 0U)
  {
    if (s_vref_mv < s_vref_target_mv)
    {
      /* 升压：vref 只在 Vout 跟上（滞后 ≤ APP_CTRL_RAMP_LAG_MV）时才前进
       * @来源 memory CTRL_BOOT_DIP：固定斜坡 21ms 走完 5V 时 Vout 仅 3.9V */
      if ((uint32_t)(HAL_GetTick() - s_ramp_ms) >= APP_CTRL_RAMP_TIMEOUT_MS)
      {
        /* 超时（无输入/重载跟不上）：放弃软启动，直接给目标（等价旧行为） */
        s_vref_mv = s_vref_target_mv;
        s_softstep = 0U;
        if (s_i_up_lim <= 0.0f)
        {
          s_pid_v.i_acc = (float)s_iout_ma;
        }
      }
      else if (((int32_t)s_vref_mv - (int32_t)s_vout_mv) <= (int32_t)s_ramp_lag_mv)
      {
        s_softstep--;
        s_vref_mv += APP_CTRL_VREF_SLEW_UP_MV;
        if (s_vref_mv > s_vref_target_mv) /* 到达目标后拉平 */
        {
          s_vref_mv = s_vref_target_mv;
          /* 斜坡刚走完：外环积分从实测负载电流起步（无扰切换）
           * 否则积分仍为 0，电流参考只有 P 项的比例值 → 内环把输出拉回去
           * 升档路径已在 SetVref 里给过 i_acc（s_i_up_lim>0），此处不覆盖 */
          if (s_i_up_lim <= 0.0f)
          {
            s_pid_v.i_acc = (float)s_iout_ma;
          }
        }
      }
    }
    else if (s_vref_mv > s_vref_target_mv)
    {
      s_softstep--;
      if ((s_vref_mv - s_vref_target_mv) <= APP_CTRL_VREF_SLEW_DN_MV)
      {
        s_vref_mv = s_vref_target_mv;
      }
      else
      {
        s_vref_mv -= APP_CTRL_VREF_SLEW_DN_MV;
      }
    }
    else
    {
      s_softstep = 0U;   /* 已在目标：结束斜坡，地板回落到正常运行下限 */
    }
  }
	
	// 计算获得的电压与电流值
  vout_mv = ((uint32_t)raw_v * APP_CTRL_MV_NUM) / APP_CTRL_MV_DEN;
  s_vout_mv = (uint16_t)vout_mv;
  s_iout_ma = (uint16_t)(((uint32_t)raw_i * BSP_ADC_I_MA_NUM) / BSP_ADC_I_MA_DEN);

  /* 主循环已判停机（无输入/短路）或按键关机：测量照更新，但不再驱动 CCR1 */
  if ((s_hold_off != 0U) || (s_enabled == 0U))
  {
    return;
  }

  /* 过压保护 */
  if ((s_ovp != 0U) || (vout_mv > APP_CTRL_OVP_MV)) // 已经保护或超压
  {
		/* 电压超阈值立刻停波 + 清积分（不指望 PI 自己救）
		 * 锁存 500ms 后把参考清零重新软启动，避免 2kHz 下反复触发形成抖动 */
    if (s_ovp == 0U) // 这一刻 vout_mv > APP_CTRL_OVP_MV 了（超压了）
    {
      s_ovp = 1U; // 置锁存位，表示超压了。
      s_ovp_ms = HAL_GetTick(); //记录超压时间

      /* 无限重启判据：空载时每次重启都会再冲压，必须在有限次后关机 */
      if ((uint32_t)(s_ovp_ms - s_ovp_last_ms) > APP_CTRL_OVP_RETRY_WIN_MS)
      {
        s_ovp_tries = 0U; // 窗口外才重新计数（偶发单次 OVP 不累计）
      }
      s_ovp_last_ms = s_ovp_ms;
      s_ovp_tries++;
      if (s_ovp_tries > APP_CTRL_OVP_RETRY_MAX)
      {
        SERVICE_DBG_Printf("[OVP] %u trips in %ums, OFF until key (NO LOAD)\r\n",
                           (unsigned)s_ovp_tries,
                           (unsigned)APP_CTRL_OVP_RETRY_WIN_MS);
        app_control_fault_off();
        return;
      }

      app_control_reset_integrators(); //清理积分值
      s_iref_ma = 0U;
      s_iref_lim = 0.0f;
			
			/* 复位缓启动 */
      s_vref_mv = 0U; // 重新设置输出目标为0
      app_control_ramp_start();
      s_i_up_lim = 0.0f;
			
      /* OVP 是安全事件：只在上沿打印一次，便于 RTT 直接归因 */
      SERVICE_DBG_Printf("[OVP] Vbus=%lumV > %umV, output OFF, restart in %ums\r\n",
                         (unsigned long)vout_mv,
                         (unsigned)APP_CTRL_OVP_MV,
                         (unsigned)APP_CTRL_OVP_HOLDOFF_MS);
    } 
    if ((uint32_t)(HAL_GetTick() - s_ovp_ms) >= APP_CTRL_OVP_HOLDOFF_MS)
    {
      s_ovp = 0U; // 清除锁存，重新进行软启动和PID
    }
    else
    {
      /* 安全态 = 停波（不是压到下限）：下限只是正常运行的控制范围约束 */
      s_ccr1_now = 0U; //设置CCR为0
      DRV_PWM_Stop(); // 关闭PWM波
      return;
    }
  }

  err = (float)s_vref_mv - (float)vout_mv;

  /* 地板随启动阶段变化：PID 输出限幅必须与执行器下限同步，
   * 否则 PID 以为已在下限饱和，实际执行器已放到 BOOT_MIN → 抗积分饱和判据失效 */
  floor_cnt = app_control_duty_floor();
#if (CL_CTRL_MODE == CL_CTRL_MODE_VOLT_ONLY)
  s_pid_v.out_min = (float)floor_cnt;
#else
  s_pid_i.out_min = (float)floor_cnt;
#endif

#if (CL_CTRL_MODE == CL_CTRL_MODE_VOLT_ONLY)
  /* 单环电压：前馈给工作点，PI 只修残差 */
  ccr1 = APP_PID_Step(&s_pid_v, err, app_control_ff_ccr1(s_vref_mv));
#else
  {
    /* 级联双环：外环 V→Iref(mA)，内环 I→CCR1
     * 外环输出限幅 [0, IREF_MAX_MA] 由 APP_PID_Init 设定（同时承担抗积分饱和） */
    if (s_i_trans != 0U)
    {
      int32_t dv = (int32_t)s_vout_mv - (int32_t)s_vref_target_mv;

      if (dv < 0)
      {
        dv = -dv;
      }
      if ((dv <= (int32_t)APP_CTRL_TRANS_BAND_MV) ||
          ((uint32_t)(HAL_GetTick() - s_i_trans_ms) >= APP_CTRL_TRANS_TIMEOUT_MS))
      {
        s_i_trans = 0U;
        s_pid_v.i_acc = s_i_trans_now;  /* 交还外环时从过渡电流起步，避免跳变 */
        s_iref_lim = s_i_trans_now;
      }
    }

    if (s_i_trans != 0U)
    {
      /* 降压过渡：内环参考从当前电流限速降到 i0，前馈用实测 Vout */
      if (s_i_trans_now > (s_i_trans_ma + APP_CTRL_IREF_SLEW_MA))
      {
        s_i_trans_now -= APP_CTRL_IREF_SLEW_MA;
      }
      else if (s_i_trans_now < (s_i_trans_ma - APP_CTRL_IREF_SLEW_MA))
      {
        s_i_trans_now += APP_CTRL_IREF_SLEW_MA;
      }
      else
      {
        s_i_trans_now = s_i_trans_ma;
      }
      s_iref_ma = (uint16_t)s_i_trans_now;
      ccr1 = APP_PID_Step(&s_pid_i, s_i_trans_now - (float)s_iout_ma,
                          app_control_ff_ccr1(vout_mv));
    }
    else
    {
      float iref;
      float ierr;

      /* 升压斜坡期间 PI 冻结（否则 50ms 内积分会多累 300mA+，电流过冲）；
       * 只下发按负载估出的稳态电流，电压由前馈占空比沿斜坡推上去 */
      int32_t dv_tgt = (int32_t)vout_mv - (int32_t)s_vref_target_mv;
      if (dv_tgt < 0)
      {
        dv_tgt = -dv_tgt;
      }
      /* 冻结条件：斜坡未走完 且 输出离目标还远（>300mV）。一旦接近目标就交还 PI，
       * 否则固定前馈电流会把输出推过目标（实测 12.55V，+4.6%） */
      if ((s_softstep > 0U) && (s_i_up_lim > 0.0f) && (s_vref_mv < s_vref_target_mv)
          && (dv_tgt > 300))
      {
        iref = s_i_up_lim;
      }
      else
      {
        float ki_base = s_pid_v.ki;

        /* 启动升压期放大积分增益，加速电流爬升（见 APP_CTRL_BOOT_KI_MULT）；
         * 调用后立刻复原，既不覆盖 RTT 的 KII 调参值，也不影响换档/稳态 */
        if (app_control_boot_boost() != 0U)
        {
          s_pid_v.ki = ki_base * s_boot_ki_mult;
        }
        iref = APP_PID_Step(&s_pid_v, err, 0.0f);
        s_pid_v.ki = ki_base;
      }

      /* 电流参考限速：升降都以 5mA/步 推进，输出沿负载线平滑过渡 */
      if (iref > (s_iref_lim + APP_CTRL_IREF_SLEW_MA))
      {
        iref = s_iref_lim + APP_CTRL_IREF_SLEW_MA;
      }
      else if (iref < (s_iref_lim - APP_CTRL_IREF_SLEW_MA))
      {
        iref = s_iref_lim - APP_CTRL_IREF_SLEW_MA;
      }
      if (iref < 0.0f)
      {
        iref = 0.0f;
      }
      s_iref_lim = iref;

      ierr = iref - (float)s_iout_ma;
      s_iref_ma = (uint16_t)iref;   /* 调参回传用（pid-tune 内环 sp） */
      ccr1 = APP_PID_Step(&s_pid_i, ierr, app_control_ff_ccr1(vout_mv));
    }
  }
#endif

  if (ccr1 < (float)floor_cnt)
  {
    ccr1 = (float)floor_cnt;
  }
  if (ccr1 > APP_CTRL_CCR1_MAX)
  {
    ccr1 = APP_CTRL_CCR1_MAX;
  }

  s_ccr1_now = (uint16_t)ccr1;
  DRV_PWM_SetDutyMin(floor_cnt);
  DRV_PWM_SetDutyCount(s_ccr1_now);
}

/* ==== 接口函数 ==== */

/**
 * @说明: 闭环初始化（在 DRV_PWM_Init / DRV_ADC_Init 之后调）
 * @注意: 初始化后控制权归本模块，main 里不要再调 DRV_PWM_SetDutyCount
 */
void APP_CONTROL_Init(void)
{
#if (CL_CTRL_MODE == CL_CTRL_MODE_VOLT_ONLY)
  APP_PID_Init(&s_pid_v, APP_CTRL_KP_V, APP_CTRL_KI_V, APP_CTRL_CCR1_MIN, APP_CTRL_CCR1_MAX);
#else
  /* 外环输出是电流参考 mA（0…IREF_MAX），不是 CCR1 计数——限幅必须是电流量纲 */
  APP_PID_Init(&s_pid_v, APP_CTRL_KP_V, APP_CTRL_KI_V, 0.0f, (float)APP_CTRL_IREF_MAX_MA);
  APP_PID_Init(&s_pid_i, APP_CTRL_KP_I, APP_CTRL_KI_I, APP_CTRL_CCR1_MIN, APP_CTRL_CCR1_MAX);
#endif

  s_vref_user_mv = APP_CTRL_VREF_MV_DEF;   /* 上电档位 = 5V */
  s_vref_target_mv = s_vref_user_mv;
  s_enabled = 0U;                 /* 上电输出关闭，等 Key1 开启（用户 2026-11-06） */
  s_vref_mv = 0U;                 /* 软启动从 0 开始 */
  app_control_ramp_start();
  s_i_up_lim = 0.0f;
  s_ccr1_now = 0U;
  s_iref_ma = 0U;
  s_iref_lim = 0.0f;
  s_i_trans = 0U;
  s_last_count = DRV_ADC_GetScanCount();
  s_ovp = 0U;
  s_hold_off = 0U;
  s_noin = 0U;
  s_probe = 0U;
  s_noin_reported = 0U;
  s_noin_tries = 0U;
  s_ovp_tries = 0U;
  s_ovp_last_ms = 0U;
  s_noload = 0U;
  s_low_ms = HAL_GetTick();

  /* 上电必须是停波态：CCR1/CCR2=0，不能压到占空比下限（否则上电就有 ~4.8V） */
  DRV_PWM_Stop();
}

/**
 * @说明: 主循环调用——ADC 停更（失联）时把占空比压到下限
 * @注意: 控制步进在 ISR 里，本函数只做兜底。超时按 HAL_GetTick 的毫秒算，
 *        不能用主循环次数充当时钟（循环周期随打印/滤波浮动，会误判）
 * @来源 2026-11-04 评审：原实现按「循环次数」累计且正常路径不清零 →
 *        稳态下每 ~4 轮就误判失联、把 CCR1 反复压到下限（闭环失效）
 */
void APP_CONTROL_Loop(void)
{
  static uint32_t s_seen_ms = 0U;
  static uint8_t s_reported = 0U;
  uint32_t cnt = DRV_ADC_GetScanCount();
  uint32_t now;

  if (cnt != s_last_count)
  {
    s_last_count = cnt;
    s_seen_ms = HAL_GetTick();
    s_reported = 0U;   /* 只影响下次故障能否再报一次，不做任何恢复动作 */
    return;
  }

  /* 扫描计数没变 = 采样停了。2kHz 下 5ms 应有 10 次更新
   * 安全动作 = 直接停波（CCR1/CCR2 全 0），不是压到 DUTY_CNT_MIN：
   * 下限只是「正常运行时的控制范围」约束，停机就该真停 */
  now = HAL_GetTick();
  if ((uint32_t)(now - s_seen_ms) > APP_CTRL_LOST_MS)
  {
    app_control_reset_integrators();
    s_iref_lim = 0.0f;
    s_i_up_lim = 0.0f;
    s_ccr1_now = 0U;
    DRV_PWM_Stop();

    if (s_reported == 0U)
    {
      s_reported = 1U;
      SERVICE_DBG_Printf("[LOST] ADC sample lost, output OFF\r\n");
    }
    s_seen_ms = now;
    return;
  }

  /* ---- 无输入/输出短路保护（主循环，毫秒级足够） ---- */

  /* 停机态：等重试间隔到点，再用低目标探测一次 */
  if (s_noin != 0U)
  {
    if ((uint32_t)(now - s_noin_ms) >= APP_CTRL_RETRY_MS)
    {
      s_noin = 0U;
      s_hold_off = 0U;
      s_probe = 1U;
      app_control_reset_integrators();
      s_vref_target_mv = APP_CTRL_PROBE_MV;
      s_vref_mv = 0U;
      app_control_ramp_start();
      s_i_up_lim = 0.0f;
      s_low_ms = now;
    }
    return;
  }

  /* 探测成功（输出有电压响应）→ 恢复正式目标，继续软启动 */
  if ((s_probe != 0U) && (s_vout_mv >= APP_CTRL_NOIN_MV))
  {
    s_probe = 0U;
    s_noin_reported = 0U;   /* 这一轮故障结束了，下次再掉电重新报一次 */
    s_noin_tries = 0U;      /* 输出已恢复，重试计数清零 */
    s_vref_target_mv = s_vref_user_mv;   /* 回到用户档位（5V/12V），不是固定 12V */
    app_control_ramp_start();
    s_i_up_lim = 0.0f;
    SERVICE_DBG_Printf("[NOIN] input back, resume %lumV\r\n",
                       (unsigned long)s_vref_user_mv);
  }

  /* 已在驱动但输出升不起来 → 判无输入（或输出短路），停波重试 */
  if ((s_ccr1_now >= (uint16_t)APP_CTRL_CCR1_MIN) && (s_vout_mv < APP_CTRL_NOIN_MV))
  {
    if ((uint32_t)(now - s_low_ms) >= APP_CTRL_NOIN_MS)
    {
      s_noin_tries++;
      if (s_noin_tries > APP_CTRL_NOIN_RETRY_MAX)
      {
        /* 重试用尽仍升不起来（空载/无输入）→ 真关机，等按键再开，不再自动重启 */
        app_control_fault_off();
        SERVICE_DBG_Printf("[NOIN] no output after %u tries, OFF until key (NO LOAD)\r\n",
                           (unsigned)APP_CTRL_NOIN_RETRY_MAX);
        return;
      }
      s_noin = 1U;
      s_probe = 0U;
      s_noin_ms = now;
      s_hold_off = 1U;
      app_control_reset_integrators();
      s_iref_lim = 0.0f;
      s_ccr1_now = 0U;
      DRV_PWM_Stop();

      if (s_noin_reported == 0U)
      {
        s_noin_reported = 1U;
        SERVICE_DBG_Printf("[NOIN] Vout<%umV at ccr1>=%u, output OFF, retry %ums (%u/%u)\r\n",
                           (unsigned)APP_CTRL_NOIN_MV,
                           (unsigned)APP_CTRL_CCR1_MIN,
                           (unsigned)APP_CTRL_RETRY_MS,
                           (unsigned)s_noin_tries,
                           (unsigned)APP_CTRL_NOIN_RETRY_MAX);
      }
    }
  }
  else
  {
    s_low_ms = now;
  }
}

/**
 * @说明: 设置输出电压档位目标（5V / 12V）
 * @输入: vref_mv 目标电压 mV
 * @注意: 降压直接下切（Buck 降低占空比不会过冲）；升压重新走 50ms 软启动；
 *        改档位清积分，避免旧工作点积分拖慢响应。关机状态只记档位不起输出。
 * @来源 你（2026-11-06 按键切档需求）；目标值来自 APP_CTRL_VREF_MV_5V/12V
 */
void APP_CONTROL_SetVref(uint32_t vref_mv)
{
  uint32_t primask;
  uint32_t prev_mv = s_vref_target_mv;

  if (vref_mv == s_vref_user_mv)
  {
    return;
  }

  primask = __get_PRIMASK();
  __disable_irq();
  s_vref_user_mv = vref_mv;
  if (s_enabled != 0U)
  {
    /* 只清外环：内环积分承载当前占空比工作点，清掉会让输出先掉一截 */
    s_pid_v.i_acc = 0.0f;
    s_i_trans = 0U;
    s_i_up_lim = 0.0f;
    s_vref_target_mv = vref_mv;
    /* 不在这里直接改 s_vref_mv：升/降都由 ISR 的软启动段按 50ms 限速推进 */
    app_control_ramp_start();
    s_i_up_lim = 0.0f;
#if (CL_CTRL_MODE == CL_CTRL_MODE_VOLT_CURR)
    /* 降压：进入恒流过渡（见 APP_CTRL_TRANS_BAND_MV 说明）
     * 按 R = Vout/Iout 估新档位稳态电流；升压仍走电压斜坡 + 外环 */
    if ((vref_mv < prev_mv) && (s_vout_mv >= APP_CTRL_NOIN_MV) && (s_iout_ma >= 20U))
    {
      float i0 = ((float)vref_mv * (float)s_iout_ma) / (float)s_vout_mv;

      if (i0 > (float)APP_CTRL_IREF_MAX_MA)
      {
        i0 = (float)APP_CTRL_IREF_MAX_MA;
      }
      s_i_trans = 1U;
      s_i_trans_ma = i0;
      s_i_trans_now = s_iref_lim;   /* 从当前电流开始限速下降，不能瞬间跳到 i0 */
      s_i_trans_ms = HAL_GetTick();
      s_pid_v.i_acc = i0;
      s_vref_mv = vref_mv;   /* 过渡不用斜坡；前馈改用实测 Vout */
    }
    else if ((vref_mv > prev_mv) && (s_vout_mv >= APP_CTRL_NOIN_MV) && (s_iout_ma >= 20U))
    {
      /* 升压：同样用负载前馈给外环积分初值；否则积分从 0 重建会先把输出拉塌 */
      float i0 = ((float)vref_mv * (float)s_iout_ma) / (float)s_vout_mv;

      if (i0 > (float)APP_CTRL_IREF_MAX_MA)
      {
        i0 = (float)APP_CTRL_IREF_MAX_MA;
      }
      s_pid_v.i_acc = i0;
      /* 斜坡期间外环不介入：直接给负载前馈电流，避免积分累积造成电流过冲 */
      s_i_up_lim = i0;
    }
#endif
    s_low_ms = HAL_GetTick();
  }
  __set_PRIMASK(primask);
}

/**
 * @说明: 5V ↔ 12V 档位切换（按键 Key2 → 本函数）
 * @注意: 输出开/关都响应。关机时只改 SET 档位，开机时按该档位出压
 * @来源 你（2026-11-06 首次需求；2026-11-08 改为关机也可改 SET）
 */
void APP_CONTROL_CycleVref(void)
{
  uint32_t next = (s_vref_user_mv == APP_CTRL_VREF_MV_5V)
                  ? APP_CTRL_VREF_MV_12V
                  : APP_CTRL_VREF_MV_5V;

  APP_CONTROL_SetVref(next);
}

/**
 * @说明: 输出开关（按键 Key1 → 本函数）
 * @输入: on 1 = 开启（从 0V 软启动到当前档位），0 = 关闭（停波）
 * @注意: 关闭 = 停波（CCR1/CCR2 全 0，下管续流），不是压到占空比下限；
 *        重开从 0 重新软启动 50ms，避免上电瞬间占空比已在高位
 * @来源 memory PWM_STOP_MUST_ZERO（停机必须 CCR1=CCR2=0，不得压到下限）
 */
void APP_CONTROL_SetOutput(uint8_t on)
{
  uint32_t primask;

  if (on != 0U)
  {
    if (s_enabled != 0U)
    {
      return;
    }
    primask = __get_PRIMASK();
    __disable_irq();
    s_enabled = 1U;
    s_hold_off = 0U;
    s_noin = 0U;
    s_probe = 0U;
  s_noin_reported = 0U;
  s_noin_tries = 0U;
  s_ovp_tries = 0U;
  s_noload = 0U;
  s_ovp = 0U;
  app_control_reset_integrators();
  s_vref_target_mv = s_vref_user_mv;
    s_vref_mv = 0U;
    s_iref_ma = 0U;
    s_iref_lim = 0.0f;
    s_i_up_lim = 0.0f;
    s_i_trans = 0U;
    app_control_ramp_start();
    s_i_up_lim = 0.0f;
    s_low_ms = HAL_GetTick();
    __set_PRIMASK(primask);
  }
  else
  {
    if (s_enabled == 0U)
    {
      return;
    }
    primask = __get_PRIMASK();
    __disable_irq();
    s_enabled = 0U;
    s_hold_off = 0U;
    s_noin = 0U;
  s_probe = 0U;
  s_noin_tries = 0U;
  s_ovp_tries = 0U;
  s_noload = 0U;
  s_ovp = 0U;
  app_control_reset_integrators();
  s_vref_mv = 0U;
    s_iref_ma = 0U;
    s_iref_lim = 0.0f;
    s_i_up_lim = 0.0f;
    s_i_trans = 0U;
    /* 保留用户档位：下次 Key1 开启按当前 SET 出压（用户 2026-11-08 改需求，
     * 原「关机回 5V」作废） */
    s_vref_target_mv = s_vref_user_mv;
    s_ccr1_now = 0U;
    DRV_PWM_Stop();
    __set_PRIMASK(primask);
    SERVICE_DBG_Printf("[KEY] output OFF\r\n");
  }
}

/**
 * @说明: 输出开关翻转（按键 Key1 按一次反转一次）
 * @来源 你（2026-11-06 按键开关机需求）
 */
void APP_CONTROL_OutputToggle(void)
{
  APP_CONTROL_SetOutput((s_enabled != 0U) ? 0U : 1U);
}

/**
 * @说明: 在线修改环路增益（调参用；单环模式下内环参数被忽略）
 * @输入: kp_v/ki_v 外环（mA/mV、mA/(mV·步)）；kp_i/ki_i 内环（计数/mA、计数/(mA·步)）
 * @注意: 内部按 APP_CTRL_*_LIMIT 限幅；改增益不清积分（由上层决定），ISR 读增益故用临界区
 * @来源 details/pid.md「apply 高风险」；限幅值见 APP_CTRL_KP_V_LIMIT 等
 */
void APP_CONTROL_SetGains(float kp_v, float ki_v, float kp_i, float ki_i)
{
  uint32_t primask;

  if (kp_v < 0.0f)
  {
    kp_v = 0.0f;
  }
  if (ki_v < 0.0f)
  {
    ki_v = 0.0f;
  }
  if (kp_i < 0.0f)
  {
    kp_i = 0.0f;
  }
  if (ki_i < 0.0f)
  {
    ki_i = 0.0f;
  }
  if (kp_v > APP_CTRL_KP_V_LIMIT)
  {
    kp_v = APP_CTRL_KP_V_LIMIT;
  }
  if (ki_v > APP_CTRL_KI_V_LIMIT)
  {
    ki_v = APP_CTRL_KI_V_LIMIT;
  }
  if (kp_i > APP_CTRL_KP_I_LIMIT)
  {
    kp_i = APP_CTRL_KP_I_LIMIT;
  }
  if (ki_i > APP_CTRL_KI_I_LIMIT)
  {
    ki_i = APP_CTRL_KI_I_LIMIT;
  }

  primask = __get_PRIMASK();
  __disable_irq();
  s_pid_v.kp = kp_v;
  s_pid_v.ki = ki_v;
#if (CL_CTRL_MODE == CL_CTRL_MODE_VOLT_CURR)
  s_pid_i.kp = kp_i;
  s_pid_i.ki = ki_i;
#else
  (void)kp_i;
  (void)ki_i;
#endif
  __set_PRIMASK(primask);
}

/**
 * @说明: 读当前环路增益（调参回显用）
 * @输出: kp_v/ki_v 外环、kp_i/ki_i 内环（单环模式内环返回初始化值）
 */
void APP_CONTROL_GetGains(float *kp_v, float *ki_v, float *kp_i, float *ki_i)
{
  if (kp_v != NULL)
  {
    *kp_v = s_pid_v.kp;
  }
  if (ki_v != NULL)
  {
    *ki_v = s_pid_v.ki;
  }
#if (CL_CTRL_MODE == CL_CTRL_MODE_VOLT_CURR)
  if (kp_i != NULL)
  {
    *kp_i = s_pid_i.kp;
  }
  if (ki_i != NULL)
  {
    *ki_i = s_pid_i.ki;
  }
#else
  if (kp_i != NULL)
  {
    *kp_i = 0.0f;
  }
  if (ki_i != NULL)
  {
    *ki_i = 0.0f;
  }
#endif
}

uint8_t APP_CONTROL_GetOutputEnabled(void)
{
  return s_enabled;
}

uint8_t APP_CONTROL_GetNoLoad(void)
{
  return s_noload;
}

uint32_t APP_CONTROL_GetVrefTargetMv(void)
{
  return s_vref_user_mv;
}

uint32_t APP_CONTROL_GetVrefMv(void)
{
  return s_vref_mv;   /* 软启动斜坡后的当前参考，不是用户档位 */
}

uint16_t APP_CONTROL_GetIoutMa(void)
{
  return s_iout_ma;
}

uint16_t APP_CONTROL_GetIrefMa(void)
{
  return s_iref_ma;
}

uint32_t APP_CONTROL_GetVoutMv(void)
{
  return s_vout_mv;
}

uint16_t APP_CONTROL_GetCcr1(void)
{
  return s_ccr1_now;
}

uint8_t APP_CONTROL_GetOvp(void)
{
  return s_ovp;
}

/**
 * @说明: 在线设置启动提速参数（调参用）
 * @输入: ki_mult 启动段外环积分倍率（0 = 关闭提速）；lag_mv 升压门控滞后（mV）
 * @注意: 倍率乘在「当前外环 KI」上，故 KII 调参仍生效；限幅防误发
 * @来源 memory CTRL_RAMP_LAG=800mV（门控 slack 实测）；
 *        di/dt = KI_V×lag×f 推导见 APP_CTRL_BOOT_KI_MULT
 */
void APP_CONTROL_SetBootTune(float ki_mult, uint32_t lag_mv)
{
  uint32_t primask;

  if (ki_mult < 0.0f)
  {
    ki_mult = 0.0f;
  }
  if (ki_mult > APP_CTRL_BOOT_KI_MULT_LIMIT)
  {
    ki_mult = APP_CTRL_BOOT_KI_MULT_LIMIT;
  }
  if (lag_mv < 100U)
  {
    lag_mv = 100U;
  }
  if (lag_mv > 3000U)
  {
    lag_mv = 3000U;
  }

  primask = __get_PRIMASK();
  __disable_irq();
  s_boot_ki_mult = ki_mult;
  s_ramp_lag_mv = (uint16_t)lag_mv;
  __set_PRIMASK(primask);
}

/**
 * @说明: 读启动提速参数（调参回显用）
 */
void APP_CONTROL_GetBootTune(float *ki_mult, uint32_t *lag_mv)
{
  if (ki_mult != NULL)
  {
    *ki_mult = s_boot_ki_mult;
  }
  if (lag_mv != NULL)
  {
    *lag_mv = (uint32_t)s_ramp_lag_mv;
  }
}

