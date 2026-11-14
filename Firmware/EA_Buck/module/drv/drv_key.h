/*****************************************************************************
 * @文件: drv_key.h
 * @作者: cl
 * @日期: 2026-11-14
 * @版本: v2.0
 * @说明: 按键扫描驱动接口（消抖 + 按下/释放沿 + 持续时长，供上层做长按策略）
 * @平台: STM32F103C8T6
 * @依赖: stdint.h
 ****************************************************************************/
#ifndef DRV_KEY_H
#define DRV_KEY_H

#include <stdint.h>

/* 单只按键的状态快照
 * @来源 bsp_key.h BSP_KEY1_PIN=PB5 / BSP_KEY2_PIN=PB4；需求 你 2026-11-14 */
typedef struct
{
  uint8_t pressed;       /* 1=当前处于按下状态（已消抖） */
  uint8_t pressed_edge;  /* 1=本拍产生按下沿（消抖确认时置一次） */
  uint8_t released_edge; /* 1=本拍产生释放沿 */
  uint32_t held_ms;      /* 已持续按下的毫秒数；未按下为 0 */
} drv_key_state_t;

void DRV_KEY_Init(void);
void DRV_KEY_Scan(drv_key_state_t *key1, drv_key_state_t *key2);

#endif /* DRV_KEY_H */
