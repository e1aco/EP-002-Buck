/*****************************************************************************
 * @文件: bsp_pwm.c
 * @作者: cl
 * @日期: 2026-10-30
 * @版本: v2.0
 * @说明: 双通道 PWM 板级引脚/时钟（PA8=TIM1_CH1 → LIN#，PA7=TIM3_CH2 → HIN）
 * @平台: STM32F103C8T6
 * @依赖: Core 的 tim.c（PA8 已由 CubeMX 的 HAL_TIM_MspPostInit 配好，本文件只补 PA7）
 ****************************************************************************/
#include "bsp_pwm.h"
#include "main.h"

/**
 * @说明: 使能 TIM3 时钟、配 PA7 为 TIM3_CH2 复用推挽，并确保 TIM1 不做重映射
 * @注意: PA7 走 TIM3_CH2（默认复用，无需重映射）；若 TIM1 仍处于部分重映射状态，
 *        CH1N 会占用 PA7 → 必须先显式关掉 TIM1 重映射
 * @来源 通用知识-待复核 RM0008 AFIO_MAPR：TIM3_CH2 默认落 PA7；
 *        TIM1_REMAP=00 时 CH1N 在 PB13，不占 PA7
 * @来源 memory PWM_HIN_PIN=PA7/TIM3_CH2、PWM_LIN_PIN=PA8/TIM1_CH1
 */
void BSP_PWM_InitPins(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* 时钟前置：AFIO 未开就写 MAPR = 总线错误 */
  __HAL_RCC_AFIO_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_TIM3_CLK_ENABLE();

  /* 关掉 TIM1 部分重映射：CH1N 退回 PB13，PA7 让给 TIM3_CH2 */
  __HAL_AFIO_REMAP_TIM1_DISABLE();

  GPIO_InitStruct.Pin = GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}
