/*****************************************************************************
 * @文件: bsp_key.h
 * @作者: cl
 * @日期: 2026-11-13
 * @版本: v1.0
 * @说明: 按键板级事实（引脚映射与有效电平）
 * @平台: STM32F103C8T6
 * @依赖: main.h
 ****************************************************************************/
#ifndef BSP_KEY_H
#define BSP_KEY_H

#include "main.h"

/* KEY1=PB5、KEY2=PB4，均配置为 GPIO_MODE_INPUT + GPIO_NOPULL
 * @来源 EA_Buck.ioc PB4.GPIO_Label=KEY2 / PB5.GPIO_Label=KEY1
 * @来源 Core/Src/gpio.c:53 GPIO_InitStruct.Pin = KEY2_Pin|KEY1_Pin, Pull = GPIO_NOPULL */
#define BSP_KEY1_PORT     KEY1_GPIO_Port
#define BSP_KEY1_PIN      KEY1_Pin
#define BSP_KEY2_PORT     KEY2_GPIO_Port
#define BSP_KEY2_PIN      KEY2_Pin

/* 有效电平：外部上拉、按下接地
 * @来源 实测 GPIOB IDR=0x0000FDFB（未按键时 PB4/PB5 均为高）2026-11-13
 * @待确认 若硬件为按下接 VCC，改 GPIO_PIN_SET 即可 */
#define BSP_KEY_PRESSED_STATE  GPIO_PIN_RESET

#endif /* BSP_KEY_H */
