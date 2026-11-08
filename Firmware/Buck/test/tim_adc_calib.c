/*****************************************************************************
 * @文件: tim_adc_calib.c
 * @作者: cl
 * @日期: 2026-10-24
 * @版本: v1.1
 * @说明: ADC 转换时间标定：扫描 TIM4 触发频率，用 DMA 的 CNDTR 计数实际转换次数
 * @平台: STM32F103C8T6（ADC1 + DMA1_Ch1，TIM4_CC4 触发）
 * @依赖: drv_adc（扫描计数）, service_dbg（RTT 打印）
 * @注意: v1.1 改为"CNDTR 计数法"——标定期间关闭 DMA 传输完成中断，
 *        排除中断开销干扰，测到的才是 ADC 本身的转换能力（v1.0 用中断计数，
 *        在 160kHz 处被 ISR 拖死，测的是"ISR 上限"而非转换时间）
 ****************************************************************************/
#include "config.h"

#if CL_ADC_CALIB

#include "tim_adc_calib.h"
#include "tim_test.h"
#include "drv_adc.h"
#include "service_dbg.h"
#include "main.h"
#include "tim.h"
#include "adc.h"

/* 扫描档位（TIM4 ARR）：触发频率 = 7.2MHz / (ARR+1)
 * @来源 memory TIM4_PSC=9 推导：72MHz/(9+1)=7.2MHz 计数时钟
 * 生产档 ARR=3599(2kHz)；ARR 大于生产档时会被 TIM1_TRGO(2kHz, 500µs) 复位截断而测不到，
 * 故最大档取 1799(1kHz)，向下到 14->480kHz，覆盖理论拐点 461kHz 两侧 */
static const uint16_t s_arr_tab[] =
{
  1799U, 899U, 449U, 299U, 224U, 179U, 149U, 119U,
  89U,   71U,   59U,   44U,  35U,  29U,  23U,  19U,
  17U,   14U
};

#define TIM_ADC_CALIB_STEPS    (sizeof(s_arr_tab) / sizeof(s_arr_tab[0]))
#define TIM_ADC_CALIB_BUF      4096U     /* 8KB 环形缓冲：2ms 窗口内不会绕圈 */
#define TIM_ADC_CALIB_WIN_MS   2U        /* 观测窗口（远小于 8.2ms 绕圈时间） */
#define TIM_ADC_CALIB_REPEAT   40U       /* 每档重复次数，取平均降噪 */

static uint16_t s_cal_buf[TIM_ADC_CALIB_BUF];

/**
 * @说明: 算两次 CNDTR 之间的传输次数（CNDTR 递减，到 0 自动重装）
 */
static uint32_t cndtr_delta(uint16_t before, uint16_t after)
{
  if (before >= after)
  {
    return (uint32_t)(before - after);
  }

  return (uint32_t)before + TIM_ADC_CALIB_BUF - (uint32_t)after;
}

/* ==== 接口函数 ==== */

/**
 * @说明: 执行一次完整扫描并打印标定表
 * @注意: 阻塞约 1.5s；标定期间 ADC 改由大缓冲 DMA 接收、关闭 TC 中断
 */
void TIM_ADC_CALIB_Run(void)
{
  uint32_t i;
  uint32_t k;
  uint32_t cyc0;
  uint32_t cyc1;
  uint32_t dt_us;
  uint32_t xfer;
  uint32_t rate;
  uint32_t ftrig;
  uint32_t ratio_pm;
  uint32_t arr_prod;

  arr_prod = __HAL_TIM_GET_AUTORELOAD(&htim4);    /* 生产档 ARR（2kHz=3599），扫完按此恢复 */

  SERVICE_DBG_Printf("[CAL] begin: %u steps, CNDTR method, win=%ums x%u\r\n",
                     (unsigned)TIM_ADC_CALIB_STEPS,
                     (unsigned)TIM_ADC_CALIB_WIN_MS,
                     (unsigned)TIM_ADC_CALIB_REPEAT);

  /* 换大缓冲并关掉传输完成中断：排除 ISR 开销，纯测 ADC 转换能力 */
  (void)HAL_ADC_Stop_DMA(&hadc1);
  __HAL_DMA_DISABLE_IT(hadc1.DMA_Handle, DMA_IT_TC);
  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)s_cal_buf, TIM_ADC_CALIB_BUF) != HAL_OK)
  {
    SERVICE_DBG_Printf("[CAL] dma restart failed\r\n");
    return;
  }

  for (i = 0U; i < TIM_ADC_CALIB_STEPS; i++)
  {
    uint16_t arr = s_arr_tab[i];

    /* 先改 ARR 再改 CCR4，避免 CCR4 > ARR 时收不到比较事件 */
    __HAL_TIM_SET_AUTORELOAD(&htim4, (uint32_t)arr);
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, (uint32_t)(arr + 1U) / 2U);
    HAL_Delay(2U);                                  /* 等新周期稳定 */

    xfer = 0U;
    dt_us = 0U;

    for (k = 0U; k < TIM_ADC_CALIB_REPEAT; k++)
    {
      uint16_t c0 = (uint16_t)hadc1.DMA_Handle->Instance->CNDTR;

      cyc0 = DWT->CYCCNT;
      HAL_Delay(TIM_ADC_CALIB_WIN_MS);
      cyc1 = DWT->CYCCNT;

      xfer += cndtr_delta(c0, (uint16_t)hadc1.DMA_Handle->Instance->CNDTR);
      dt_us += (cyc1 - cyc0) / 72U;
    }

    if (dt_us == 0U)
    {
      dt_us = 1U;
    }

    rate = (xfer * 1000000U) / dt_us;               /* 实际转换次数/秒 */
    ftrig = 7200000U / ((uint32_t)arr + 1U);        /* 触发频率 */
    ratio_pm = (rate * 1000U) / ftrig;              /* 千分比 */

    SERVICE_DBG_Printf("[CAL] arr=%u ftrig=%luHz adc=%luHz ratio=%lu.%lu%%\r\n",
                       (unsigned)arr,
                       (unsigned long)ftrig,
                       (unsigned long)rate,
                       (unsigned long)(ratio_pm / 10U),
                       (unsigned long)(ratio_pm % 10U));
  }

  /* 恢复生产配置：按进入时的 ARR 恢复触发节拍、CCR4 回到当前占空比对应的导通中点
   * （ADC 缓冲不回切，重启即可恢复） */
  __HAL_TIM_SET_AUTORELOAD(&htim4, arr_prod);
  DRV_ADC_TrigFollowPwm();
  SERVICE_DBG_Printf("[CAL] done (reboot to restore normal sampling)\r\n");
}

#endif /* CL_ADC_CALIB */
