/*****************************************************************************
 * @文件: tim_test.h
 * @作者: cl
 * @日期: 2026-11-13
 * @版本: v1.0
 * @说明: DWT 时序测量探针接口，仅宏版声明
 * @平台: STM32F103C8T6
 * @依赖: config.h
 ****************************************************************************/
#ifndef TEST_TIM_TEST_H
#define TEST_TIM_TEST_H

#include <stdint.h>
#include "config.h"

/* slot1 = DMA1_Channel1_IRQHandler 全程
 * @来源 memory CTRL_LOOP_HZ=10000 推导：DMA ISR 10kHz */
#define TEST_TIM_SLOT_DMA   1U
#define TEST_TIM_SLOT_COUNT 2U

#if CL_TIMING_MEASURE
void TEST_TIM_Init(void);
void TEST_TIM_Start(uint32_t tag);
void TEST_TIM_Stop(uint32_t tag);
void TEST_TIM_Report(void);
void TEST_TIM_Sweep(void);
void TEST_TIM_Toggle(void);
void TEST_TIM_CalibToggle(void);
#endif

#endif /* TEST_TIM_TEST_H */
