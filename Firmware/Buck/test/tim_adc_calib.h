/*****************************************************************************
 * @文件: tim_adc_calib.h
 * @作者: cl
 * @日期: 2026-10-24
 * @版本: v1.0
 * @说明: ADC 转换时间标定（触发频率扫描法）—— 一次性验证代码，不进量产
 * @注意: 开关宏 = module/config.h 的 CL_ADC_CALIB（0 = 空宏，零开销）
 ****************************************************************************/
#ifndef __TIM_ADC_CALIB_H
#define __TIM_ADC_CALIB_H

#include "config.h"

/* 关闭时不再造空函数（空宏会让调用点看着像有实现）：调用点用 #if 守卫 */
#if CL_ADC_CALIB
void TIM_ADC_CALIB_Run(void);
#endif

#endif /* __TIM_ADC_CALIB_H */
