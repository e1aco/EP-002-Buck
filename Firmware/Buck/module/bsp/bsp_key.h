/*****************************************************************************
 * @文件: bsp_key.h
 * @作者: cl
 * @日期: 2026-11-06
 * @版本: v1.0
 * @说明: 板载按键引脚事实（Key1=PB5 开关机、Key2=PB4 切档；外部 10K 上拉、低有效）
 * @平台: STM32F103C8T6
 ****************************************************************************/
#ifndef __BSP_KEY_H
#define __BSP_KEY_H

#include <stdint.h>

/* 按键编号：与 .cl/require.md 引脚表一致（Key1=PB5、Key2=PB4）
 * @来源 memory BUTTON1=PB5/Key1、BUTTON2=PB4/Key2（板上 10K 上拉 R27/R28，低有效） */
typedef enum
{
  BSP_KEY_1 = 0,   /* PB5 / Key1：输出开关（按一次反转一次） */
  BSP_KEY_2 = 1,   /* PB4 / Key2：5V / 12V 档位切换 */
  BSP_KEY_NUM = 2
} bsp_key_id_t;

/**
 * @说明: 读按键原始状态（已按低有效取反：1 = 按下）
 * @输入: id 按键编号
 * @输出: 1 按下 / 0 松开；非法编号返回 0
 * @来源 memory BUTTON1/BUTTON2：外部 10K 上拉、低有效（.cl/require.md 引脚表）
 * @注意: 引脚已由 CubeMX MX_GPIO_Init 配成 GPIO_MODE_INPUT + NOPULL，本层不重复初始化
 */
uint8_t BSP_KEY_IsDown(bsp_key_id_t id);

#endif /* __BSP_KEY_H */
