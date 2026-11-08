/*****************************************************************************
 * @文件: bsp_key.c
 * @作者: cl
 * @日期: 2026-11-06
 * @版本: v1.0
 * @说明: 板载按键读取（Key1=PB5、Key2=PB4，低有效）
 * @平台: STM32F103C8T6
 * @依赖: Core 的 gpio.c（MX_GPIO_Init 已配置输入 + NOPULL）
 ****************************************************************************/
#include "bsp_key.h"
#include "main.h"

/* ==== 接口函数 ==== */

/**
 * @说明: 读按键原始状态（低有效取反）
 * @输入: id 按键编号
 * @输出: 1 按下 / 0 松开；非法编号返回 0
 * @来源 memory BUTTON1=PB5/Key1、BUTTON2=PB4/Key2（板载 10K 上拉 R27/R28，低有效）
 *        .cl/require.md 引脚表：Key1_Pin=PB5、Key2_Pin=PB4（CubeMX 标签）
 * @注意: 只读，不做消抖；消抖与事件分发在 app_key.c
 */
uint8_t BSP_KEY_IsDown(bsp_key_id_t id)
{
  GPIO_TypeDef *port;
  uint16_t pin;

  switch (id)
  {
    case BSP_KEY_1:
      port = Key1_GPIO_Port;
      pin = Key1_Pin;
      break;
    case BSP_KEY_2:
      port = Key2_GPIO_Port;
      pin = Key2_Pin;
      break;
    default:
      return 0U;
  }

  /* 低有效：按下读到 RESET(0) → 取反成 1 */
  return (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_RESET) ? 1U : 0U;
}
