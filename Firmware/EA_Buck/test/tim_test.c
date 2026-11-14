/*****************************************************************************
 * @文件: tim_test.c
 * @作者: cl
 * @日期: 2026-11-13
 * @版本: v1.0
 * @说明: DWT CYCCNT 时序测量探针，宏版专用，生产版编译为空
 * @平台: STM32F103C8T6
 * @依赖: tim_test.h, main.h, SEGGER_RTT.h, gpio.h
 * @来源 memory CLOCK_HCLK=72MHz 推导：DWT cycles ÷ 72 = µs
 ****************************************************************************/
#include "tim_test.h"

#if CL_TIMING_MEASURE

#include <stdio.h>
#include "main.h"
#include "SEGGER_RTT.h"
#include "gpio.h"
#include "app_control.h"

/* ==== 模块常量 ==== */
/* DWT 计数频率 = CPU 时钟
 * @来源 memory CLOCK_HCLK=72MHz 推导：CYCCNT 1 个计数 = 1/72MHz */
#define TEST_TIM_CPU_MHZ       72U

/* 校准/测试翻转脚：PB0，未在 CubeMX 中占用
 * @来源 待确认：逻辑分析仪接线后确认 PB0 可用 */
#define TEST_TIM_GPIO_PORT     GPIOB
#define TEST_TIM_GPIO_PIN      GPIO_PIN_0

/* ==== 模块变量 ==== */
static volatile uint32_t s_start_cycles[TEST_TIM_SLOT_COUNT];
static volatile uint32_t s_max_cycles[TEST_TIM_SLOT_COUNT];
static volatile uint64_t s_sum_cycles[TEST_TIM_SLOT_COUNT];
static uint32_t s_last_report_ms;
static uint32_t s_last_sweep_ms;
static uint32_t s_sweep_index;

/* 不同占空比工况：通过输出电压目标间接改变占空比
 * @来源 memory CTRL_VREF_MIN_MV=3300 / CTRL_VREF_MAX_MV=12000 你 2026-11-13 */
static const uint16_t s_sweep_mv[] = {3300U, 5000U, 8000U, 10000U, 12000U};

/* ==== 内部函数 ==== */
/**
 * @说明: 将 CYCCNT 差值换算为 0.1µs
 * @输入: cycles CYCCNT 差值
 * @输出: 0.1µs 单位的时长
 */
static uint32_t test_tim_cycles_to_us_x10(uint32_t cycles)
{
  return (uint32_t)(((uint64_t)cycles * 10U + (TEST_TIM_CPU_MHZ / 2U)) /
                    TEST_TIM_CPU_MHZ);
}

/* ==== 接口函数 ==== */
/**
 * @说明: 初始化 DWT 和校准翻转脚
 * @输出: 无
 * @注意: 宏版在 APP_ADC_Init 前调用，确保首个 DMA 中断可测
 */
void TEST_TIM_Init(void)
{
  uint32_t i;
  GPIO_InitTypeDef gpio = {0};

  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  __HAL_RCC_GPIOB_CLK_ENABLE();
  gpio.Pin = TEST_TIM_GPIO_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(TEST_TIM_GPIO_PORT, &gpio);
  HAL_GPIO_WritePin(TEST_TIM_GPIO_PORT, TEST_TIM_GPIO_PIN, GPIO_PIN_RESET);

  for (i = 0U; i < TEST_TIM_SLOT_COUNT; i++)
  {
    s_start_cycles[i] = 0U;
    s_max_cycles[i] = 0U;
    s_sum_cycles[i] = 0U;
  }
  SEGGER_RTT_Init();
  s_last_report_ms = HAL_GetTick();
  s_last_sweep_ms = s_last_report_ms;
  s_sweep_index = 0U;
}

/**
 * @说明: 记录一个测量窗口起点
 * @输入: tag slot 号
 * @输出: 无
 */
void TEST_TIM_Start(uint32_t tag)
{
  if (tag >= TEST_TIM_SLOT_COUNT)
  {
    return;
  }

  s_start_cycles[tag] = DWT->CYCCNT;
}

/**
 * @说明: 记录一个测量窗口终点并更新最大值/累计值
 * @输入: tag slot 号
 * @输出: 无
 */
void TEST_TIM_Stop(uint32_t tag)
{
  uint32_t now;
  uint32_t delta;

  if (tag >= TEST_TIM_SLOT_COUNT)
  {
    return;
  }

  now = DWT->CYCCNT;
  delta = now - s_start_cycles[tag];

  if (delta > s_max_cycles[tag])
  {
    s_max_cycles[tag] = delta;
  }
  s_sum_cycles[tag] += delta;
}

/**
 * @说明: 每 5s 切换一次输出电压目标，覆盖不同占空比
 * @输出: 无
 * @注意: 通过电压环间接改变占空比；带载时覆盖的占空比范围更大
 */
void TEST_TIM_Sweep(void)
{
  uint32_t i;
  uint32_t primask;

  if ((uint32_t)(HAL_GetTick() - s_last_sweep_ms) < 5000U)
  {
    return;
  }
  s_last_sweep_ms = HAL_GetTick();

  APP_CONTROL_SetVoltageMv((uint32_t)s_sweep_mv[s_sweep_index]);

  primask = __get_PRIMASK();
  __disable_irq();
  for (i = 0U; i < TEST_TIM_SLOT_COUNT; i++)
  {
    s_max_cycles[i] = 0U;
    s_sum_cycles[i] = 0U;
  }
  __set_PRIMASK(primask);

  s_sweep_index++;
  if (s_sweep_index >= (sizeof(s_sweep_mv) / sizeof(s_sweep_mv[0])))
  {
    s_sweep_index = 0U;
  }
}

/**
 * @说明: 每秒输出一次 [TM] 行；主循环调用
 * @输出: 无，UART1 115200 输出
 */
void TEST_TIM_Report(void)
{
  uint32_t i;
  uint32_t primask;
  uint32_t max_cycles;
  uint64_t sum_cycles;
  uint32_t max_us_x10;
  uint32_t sum_us;
  uint32_t ccr;
  uint32_t arr;
  uint32_t duty_permille;
  char line[120];
  int len;

  if ((uint32_t)(HAL_GetTick() - s_last_report_ms) < 1000U)
  {
    return;
  }
  s_last_report_ms = HAL_GetTick();

  for (i = 1U; i < TEST_TIM_SLOT_COUNT; i++)
  {
    primask = __get_PRIMASK();
    __disable_irq();
    max_cycles = s_max_cycles[i];
    sum_cycles = s_sum_cycles[i];
    __set_PRIMASK(primask);

    max_us_x10 = test_tim_cycles_to_us_x10(max_cycles);
    sum_us = (uint32_t)(sum_cycles / TEST_TIM_CPU_MHZ);
    ccr = TIM1->CCR1;
    arr = TIM1->ARR;
    duty_permille = (ccr * 1000U) / (arr + 1U);

    len = snprintf(line, sizeof(line),
                   "[TM] Slot%lu %lu.%01lu us max, %lu us total, D=%lu.%lu%%\r\n",
                   (unsigned long)i,
                   (unsigned long)(max_us_x10 / 10U),
                   (unsigned long)(max_us_x10 % 10U),
                   (unsigned long)sum_us,
                   (unsigned long)(duty_permille / 10U),
                   (unsigned long)(duty_permille % 10U));
    if (len > 0)
    {
      SEGGER_RTT_WriteString(0U, line);
    }
  }
}

/**
 * @说明: 翻转测试 IO，供逻辑分析仪测量窗口
 * @输出: 无
 */
void TEST_TIM_Toggle(void)
{
  HAL_GPIO_TogglePin(TEST_TIM_GPIO_PORT, TEST_TIM_GPIO_PIN);
}

/**
 * @说明: 时钟基准校准翻转脚，每调用一次翻转一次
 * @输出: 无
 * @注意: 正式校准按 probe.md 的 100 基准单位序列调用
 */
void TEST_TIM_CalibToggle(void)
{
  HAL_GPIO_TogglePin(TEST_TIM_GPIO_PORT, TEST_TIM_GPIO_PIN);
}

#endif /* CL_TIMING_MEASURE */
