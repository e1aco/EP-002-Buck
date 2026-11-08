/*****************************************************************************
 * @文件: tim_test.c
 * @作者: cl
 * @日期: 2026-10-24
 * @版本: v1.0
 * @说明: 时序探针实现（DWT CYCCNT 差值测时，只保留每槽最大值）
 * @平台: STM32F103C8T6（Cortex-M3 @72MHz）
 * @注意: 仅宏版编译；生产版由 tim_test.h 的空宏顶替，本文件不参与链接
 ****************************************************************************/
#include "config.h"

#if CL_TIMING_MEASURE

#include "tim_test.h"
#include "main.h"

#define TEST_TIM_SLOT_NUM   4U

/* 计数频率 = CPU 时钟 = SYSCLK（72MHz，见 .cl/memory/clock.md）
 * @来源 memory SYSCLK=72MHz 推导：cycles / 72 = µs */
#define TEST_TIM_CPU_MHZ    72U

static volatile uint32_t s_start[TEST_TIM_SLOT_NUM];
static volatile uint32_t s_max_cyc[TEST_TIM_SLOT_NUM];

/* ==== 内部函数 ==== */

/* ==== 接口函数 ==== */


/**
 * @说明: 使能 DWT 周期计数器
 * @注意: Cortex-M3 的 DWT->CYCCNT 需先置 DEMCR.TRCENA 才能计数
 */
void TEST_TIM_Init(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

/**
 * @说明: 清零所有槽位的最大值（每轮测量前调用）
 */
void TEST_TIM_Reset(void)
{
  uint32_t i;

  for (i = 0U; i < TEST_TIM_SLOT_NUM; i++)
  {
    s_max_cyc[i] = 0U;
  }
}

/**
 * @说明: 记录槽位起始时刻
 */
void TEST_TIM_Start(uint32_t tag)
{
  if (tag < TEST_TIM_SLOT_NUM)
  {
    s_start[tag] = DWT->CYCCNT;
  }
}

/**
 * @说明: 记录槽位结束时刻并更新最大值
 * @注意: 用无符号差值，CYCCNT 回绕安全
 */
void TEST_TIM_Stop(uint32_t tag)
{
  uint32_t cyc;

  if (tag < TEST_TIM_SLOT_NUM)
  {
    cyc = (uint32_t)(DWT->CYCCNT - s_start[tag]);
    if (cyc > s_max_cyc[tag])
    {
      s_max_cyc[tag] = cyc;
    }
  }
}

/**
 * @说明: 取槽位最坏耗时（µs，保留 3 位小数用整数表示：返回值单位 0.001µs）
 * @注意: 返回值 = cycles * 1000 / 72，便于上层直接按 milli-µs 打印
 */
uint32_t TEST_TIM_GetMaxUs(uint32_t tag)
{
  if (tag >= TEST_TIM_SLOT_NUM)
  {
    return 0U;
  }

  return (s_max_cyc[tag] * 1000U) / TEST_TIM_CPU_MHZ;
}

#endif /* CL_TIMING_MEASURE */
