/*****************************************************************************
 * @文件: pwm_sweep.h
 * @作者: cl
 * @日期: 2026-10-28
 * @版本: v1.0
 * @说明: 开环占空比扫描（验收用）：逐档改占空比并自动重算 ADC 触发相位
 * @平台: STM32F103C8T6
 * @注意: 开关 = module/config.h 的 CL_PWM_SWEEP（0 = 生产版，空宏零开销）
 ****************************************************************************/
#ifndef __PWM_SWEEP_H
#define __PWM_SWEEP_H

#include <stdint.h>
#include "config.h"

/* 关闭时不再造空函数（空宏会让调用点看着像有实现）：调用点用 #if 守卫 */
#if CL_PWM_SWEEP
void PWM_SWEEP_Init(void);
void PWM_SWEEP_Loop(void);
#endif /* CL_PWM_SWEEP */

#endif /* __PWM_SWEEP_H */
