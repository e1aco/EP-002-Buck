/*****************************************************************************
 * @文件: app_adc.c
 * @作者: cl
 * @日期: 2026-11-13
 * @版本: v1.1
 * @说明: ADC 采样应用入口实现
 * @平台: STM32F103C8T6
 * @依赖: app_adc.h, drv_adc.h, drv_tim.h
 ****************************************************************************/
#include "app_adc.h"
#include "drv_adc.h"
#include "drv_tim.h"

/* ==== 接口函数 ==== */
/**
 * @说明: 先武装 ADC+DMA，再启动 TIM4/TIM1 触发链
 * @注: ADC 必须在首个 TIM4_CC4 前进入等待触发状态
 */
void APP_ADC_Init(void)
{
  if (DRV_ADC_Start() != HAL_OK)
  {
    return;
  }

  if (DRV_TIM_StartSync() != HAL_OK)
  {
    DRV_TIM_Stop();
    return;
  }
}
