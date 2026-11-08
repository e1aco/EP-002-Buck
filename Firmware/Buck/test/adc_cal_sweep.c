/*****************************************************************************
 * @文件: adc_cal_sweep.c
 * @作者: cl
 * @日期: 2026-11-04
 * @版本: v1.1
 * @说明: ADC 采样标定用单点保持（占空比由调试器改 RAM 变量逐点切换，不稳走时）
 * @平台: STM32F103C8T6
 * @依赖: drv_pwm（设定 CCR1）、service_dbg（打印档位）
 * @注意: 仅标定用；CL_ADC_CAL_SWEEP=0 时整段编译为空。CCR1 上限硬钳 3140（18V/75%）
 ****************************************************************************/
#include "config.h"

#if CL_ADC_CAL_SWEEP

#include "adc_cal_sweep.h"
#include "drv_pwm.h"
#include "service_dbg.h"
#include "main.h"

/* CCR1 <-> 占空比：D = (CCR1 − 2×DT) / 3600，DT=220
 * 参考点：980=15%、1520=30%(7.2V)、2240=50%(12.0V)、2960=70%(16.8V)、3140=75%(18.0V)
 * @来源 memory DUTY_MAP=(CCR1−440)/3600×24、PWM_DT=220 推导：CCR1 = 3600×D + 440 */
#define ADC_CAL_CCR1_MIN     980U
#define ADC_CAL_CCR1_MAX     3140U
#define ADC_CAL_CCR1_DEF     1520U

/* 调试器可写：J-Link write-mem 到本变量地址即切档（免重新烧录） */
static volatile uint16_t s_ccr1 = ADC_CAL_CCR1_DEF;
static uint16_t s_applied;

/* ==== 私有函数 ==== */

/**
 * @说明: 打印当前档位（ccr1 + 有效占空比 %，ASCII 输出）
 * @输入: ccr1 已钳位的计数值
 */
static void adc_cal_sweep_report(uint16_t ccr1)
{
  uint32_t duty_x10 = (((uint32_t)ccr1 - 440U) * 1000U) / 3600U;

  SERVICE_DBG_Printf("[CAL] ccr1=%u duty=%u.%u%%\r\n",
                     (unsigned)ccr1,
                     (unsigned)(duty_x10 / 10U),
                     (unsigned)(duty_x10 % 10U));
}

/* ==== 接口函数 ==== */

/**
 * @说明: 以默认档位起动并打印；之后只跟随 s_ccr1 变化（不自动走时）
 * @注意: 必须在 DRV_PWM_Init 之后调用；不要在闭环使能（CL_CTRL_ENABLE=1）时使用
 */
void ADC_CAL_SWEEP_Init(void)
{
  s_applied = ADC_CAL_CCR1_MIN;
  DRV_PWM_SetDutyCount(s_applied);
  SERVICE_DBG_Printf("[CAL] hold-and-measure mode, write s_ccr1 to step\r\n");
  ADC_CAL_SWEEP_Loop();
}

/**
 * @说明: 主循环调用：检测到 s_ccr1 变化就钳位后下发并打印
 * @注意: 钳位在本层做，调试器写越界值也超不过 75%（3140 = 18V）
 */
void ADC_CAL_SWEEP_Loop(void)
{
  uint16_t ccr1 = s_ccr1;

  if (ccr1 == s_applied)
  {
    return;
  }

  if (ccr1 < ADC_CAL_CCR1_MIN)
  {
    ccr1 = ADC_CAL_CCR1_MIN;
  }
  else if (ccr1 > ADC_CAL_CCR1_MAX)
  {
    ccr1 = ADC_CAL_CCR1_MAX;
  }

  s_applied = ccr1;
  DRV_PWM_SetDutyCount(ccr1);
  adc_cal_sweep_report(ccr1);
}

#endif /* CL_ADC_CAL_SWEEP */
