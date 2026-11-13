/*****************************************************************************
 * @文件: drv_key.h
 * @作者: cl
 * @日期: 2026-11-13
 * @版本: v1.0
 * @说明: 按键扫描驱动接口（消抖 + 按下沿事件）
 * @平台: STM32F103C8T6
 * @依赖: stdint.h
 ****************************************************************************/
#ifndef DRV_KEY_H
#define DRV_KEY_H

#include <stdint.h>

/* 按键数量与事件位：KEY1 开关输出、KEY2 循环切档
 * @来源 bsp_key.h BSP_KEY1_PIN=PB5 / BSP_KEY2_PIN=PB4；需求 你 2026-11-13 */
#define DRV_KEY_COUNT       2U
#define DRV_KEY_EVENT_KEY1  0x01U
#define DRV_KEY_EVENT_KEY2  0x02U

void DRV_KEY_Init(void);
uint8_t DRV_KEY_Scan(void);

#endif /* DRV_KEY_H */
