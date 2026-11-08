/*****************************************************************************
 * @文件: adc_cal_sweep.h
 * @作者: cl
 * @日期: 2026-11-04
 * @版本: v1.1
 * @说明: ADC 采样标定用单点保持（调试器写 RAM 变量逐点切换，供万用表对表）
 * @平台: STM32F103C8T6
 * @注意: 开关 = module/config.h 的 CL_ADC_CAL_SWEEP（0 = 生产版，空宏零开销）
 ****************************************************************************/
#ifndef __ADC_CAL_SWEEP_H
#define __ADC_CAL_SWEEP_H

#include <stdint.h>
#include "config.h"

/* 关闭时不再造空函数（空宏会让调用点看着像有实现）：调用点用 #if 守卫 */
#if CL_ADC_CAL_SWEEP
void ADC_CAL_SWEEP_Init(void);
void ADC_CAL_SWEEP_Loop(void);
#endif /* CL_ADC_CAL_SWEEP */

#endif /* __ADC_CAL_SWEEP_H */
