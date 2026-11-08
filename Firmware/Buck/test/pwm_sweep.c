/*****************************************************************************
 * @文件: pwm_sweep.c
 * @作者: cl
 * @日期: 2026-10-28
 * @版本: v1.0
 * @说明: 开环占空比扫描：每 2s 切一档 CCR1，由 PWM 驱动同步重算 TIM4 触发相位
 * @平台: STM32F103C8T6（TIM1_CH1=PA8 PWM，TIM4_CH4=PB9 触发观测）
 * @依赖: drv_pwm（设定占空比+同步相位）、service_dbg（RTT/UART 打印）
 * @注意: 仅验收用，量产置 CL_PWM_SWEEP=0 后整段编译为空
 ****************************************************************************/
#include "config.h"

#if CL_PWM_SWEEP

#include "pwm_sweep.h"
#include "drv_pwm.h"
#include "service_dbg.h"
#include "main.h"

/* ==== 扫描档位 ==== */

/* 档位 = CCR1 计数：1/5/10% 低于 DRV_PWM_DUTY_CNT_MIN(540)，应被夹到 15% —— 用于验证下限保护
 * 10~90% 为工作区判据
 * @来源 memory TIM1_PWM_RES=3600 计数/周期 推导：CCR1 = 3600 × D%
 *        1%=36、5%=180、10%=360、25%=900、50%=1800、75%=2700、90%=3240 */
static const uint16_t s_ccr1_tab[] =
{
  36U, 180U, 360U, 900U, 1800U, 2700U, 3240U
};

#define PWM_SWEEP_STEPS      (sizeof(s_ccr1_tab) / sizeof(s_ccr1_tab[0]))
#define PWM_SWEEP_DWELL_MS   2000U

static uint32_t s_idx;
static uint32_t s_tick_ms;

/* ==== 接口函数 ==== */

/**
 * @说明: 从第一档开始扫描，每 2s 进一档并循环
 * @注意: 必须在 DRV_PWM_Init / DRV_ADC_Init 之后调用（占空比一旦设定即重算相位）
 */
void PWM_SWEEP_Init(void)
{
  s_idx = 0U;
  s_tick_ms = HAL_GetTick();
  DRV_PWM_SetDutyCount(s_ccr1_tab[0]);
  SERVICE_DBG_Printf("[SWEEP] begin %u steps x %ums\r\n",
                     (unsigned)PWM_SWEEP_STEPS,
                     (unsigned)PWM_SWEEP_DWELL_MS);
}

/**
 * @说明: 主循环调用：驻留时间到则切下一档并打印当前档位
 * @注意: 切档走 DRV_PWM_SetDutyCount，ADC 触发相位在同一次调用内重算，不会脱钩
 */
void PWM_SWEEP_Loop(void)
{
  uint32_t now = HAL_GetTick();

  if ((now - s_tick_ms) < PWM_SWEEP_DWELL_MS)
  {
    return;
  }

  s_tick_ms = now;
  s_idx = (s_idx + 1U) % PWM_SWEEP_STEPS;
  DRV_PWM_SetDutyCount(s_ccr1_tab[s_idx]);
  SERVICE_DBG_Printf("[SWEEP] idx=%u ccr1=%u\r\n",
                     (unsigned)s_idx,
                     (unsigned)s_ccr1_tab[s_idx]);
}

#endif /* CL_PWM_SWEEP */
