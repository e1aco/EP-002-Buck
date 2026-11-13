/*****************************************************************************
 * @文件: drv_adc.h
 * @作者: cl
 * @日期: 2026-11-13
 * @版本: v1.3
 * @说明: ADC1 + DMA1_Channel1 采样驱动接口
 * @平台: STM32F103C8T6
 * @依赖: adc.h
 * @来源: RM0008.pdf p.227: 11.8 DMA request; p.280: Table 77 TC interrupt
 ****************************************************************************/
#ifndef DRV_ADC_H
#define DRV_ADC_H

#include "adc.h"

#define DRV_ADC_FRAME_COUNT 2U
#define DRV_ADC_BUF_LEN     DRV_ADC_FRAME_COUNT
#define DRV_ADC_IDX_I       0U
#define DRV_ADC_IDX_V       1U

extern volatile uint16_t g_drv_adc_frame[DRV_ADC_FRAME_COUNT];

HAL_StatusTypeDef DRV_ADC_Start(void);
void DRV_ADC_GetLatestFrame(uint16_t *i_out, uint16_t *v_out);
void DRV_ADC_ConvCpltIsr(void);
void DRV_ADC_ErrorIsr(void);

#endif /* DRV_ADC_H */
