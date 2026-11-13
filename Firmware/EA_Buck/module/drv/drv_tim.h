/*****************************************************************************
 * @文件: drv_tim.h
 * @作者: cl
 * @日期: 2026-11-13
 * @版本: v1.1
 * @说明: TIM1/TIM4 主从同步启动、上管中点触发与故障停机接口
 * @平台: STM32F103C8T6
 * @依赖: tim.h
 * @来源: RM0008.pdf p.334: 14.3.19 Reset mode; p.409: Table 86 TIM4 ITR0
 ****************************************************************************/
#ifndef DRV_TIM_H
#define DRV_TIM_H

#include "tim.h"

HAL_StatusTypeDef DRV_TIM_StartSync(void);
void DRV_TIM_Stop(void);
void DRV_TIM_SetDuty(uint16_t ccr1);

#endif /* DRV_TIM_H */
