/*****************************************************************************
 * @文件: tim_test.h
 * @作者: cl
 * @日期: 2026-10-24
 * @版本: v1.0
 * @说明: 时序探针（DWT CYCCNT 测时）—— 宏版声明函数，生产版同名空宏
 * @平台: STM32F103C8T6（Cortex-M3，DWT 可用）
 * @注意: 开关宏 = module/config.h 的 CL_TIMING_MEASURE（0 = 生产版，零开销）
 ****************************************************************************/
#ifndef __TIM_TEST_H
#define __TIM_TEST_H

#include <stdint.h>
#include "config.h"

/* 探针槽位 ↔ 操作 映射（改动须同步 .cl/state.md 时序测量表） */
#define TEST_SLOT_ADC_ISR   2U    /* ADC 扫描完成 DMA1_Ch1 TC 中断 */
#define TEST_SLOT_SYSTICK   1U    /* SysTick_Handler */

/* 关闭时不再造空函数/空宏（会让调用点看着像有实现）：调用点用 #if 守卫 */
#if CL_TIMING_MEASURE
void TEST_TIM_Init(void);
void TEST_TIM_Start(uint32_t tag);
void TEST_TIM_Stop(uint32_t tag);
uint32_t TEST_TIM_GetMaxUs(uint32_t tag);
void TEST_TIM_Reset(void);
#endif

#endif /* __TIM_TEST_H */
