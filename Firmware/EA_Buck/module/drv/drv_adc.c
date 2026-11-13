/*****************************************************************************
 * @文件: drv_adc.c
 * @作者: cl
 * @日期: 2026-11-13
 * @版本: v1.3
 * @说明: ADC1 + DMA1_Channel1 采样驱动实现
 * @平台: STM32F103C8T6
 * @依赖: drv_adc.h, drv_tim.h, adc.h
 * @来源: RM0008.pdf p.223: 11.4 Calibration; p.227: 11.8 DMA request; p.280: Table 77 TC interrupt
 ****************************************************************************/
#include "drv_adc.h"
#include "drv_tim.h"

/* ==== 模块变量 ==== */
volatile uint16_t g_drv_adc_frame[DRV_ADC_FRAME_COUNT];
static uint16_t s_drv_adc_dma_buf[DRV_ADC_BUF_LEN];

/* ==== 内部函数 ==== */
static void drv_adc_store(const uint16_t *frame)
{
  g_drv_adc_frame[DRV_ADC_IDX_I] = frame[DRV_ADC_IDX_I];
  g_drv_adc_frame[DRV_ADC_IDX_V] = frame[DRV_ADC_IDX_V];
}

/* ==== 接口函数 ==== */
/**
 * @说明: 校准 ADC1，启动 ADC+DMA 循环转换并等待 TIM4_CC4 触发
 * @来源: RM0008.pdf p.223: 11.4 Calibration
 * @来源: RM0008.pdf p.225: Table 67 TIM4_CC4 EXTSEL=101
 * @来源: RM0008.pdf p.227: 11.8 DMA request
 * @来源: RM0008.pdf p.278: Circular mode
 * @来源: RM0008.pdf p.280: Table 77 TC interrupt
 * @注意: 转换顺序 Rank1=ADC_I(PA1), Rank2=ADC_V(PA0)
 * @注意: 单缓冲 2 半字，仅保留 TC；HT 关闭，避免新电流配旧电压
 */
HAL_StatusTypeDef DRV_ADC_Start(void)
{
  if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK)
  {
    return HAL_ERROR;
  }

  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)s_drv_adc_dma_buf, DRV_ADC_BUF_LEN) != HAL_OK)
  {
    return HAL_ERROR;
  }

  __HAL_DMA_DISABLE_IT(hadc1.DMA_Handle, DMA_IT_HT);

  return HAL_OK;
}

/**
 * @说明: 原子读取最新一帧 ADC_I/ADC_V，避免主循环读到撕裂数据
 * @输出: i_out 电流原始码；v_out 电压原始码
 * @注意: 临界区仅复制两个半字
 */
void DRV_ADC_GetLatestFrame(uint16_t *i_out, uint16_t *v_out)
{
  uint32_t primask = __get_PRIMASK();

  __disable_irq();
  *i_out = g_drv_adc_frame[DRV_ADC_IDX_I];
  *v_out = g_drv_adc_frame[DRV_ADC_IDX_V];
  __set_PRIMASK(primask);
}

/**
 * @说明: DMA 传输完成中断，存储一帧 ADC_I/ADC_V
 * @来源: RM0008.pdf p.280: Table 77 TCIF/TCIE
 */
void DRV_ADC_ConvCpltIsr(void)
{
  drv_adc_store(&s_drv_adc_dma_buf[0]);
}

/**
 * @说明: ADC/DMA 错误回调，直接关闭 PWM 防止失控
 * @来源: RM0008.pdf p.280: 13.3.5 Error management
 */
void DRV_ADC_ErrorIsr(void)
{
  DRV_TIM_Stop();
}
