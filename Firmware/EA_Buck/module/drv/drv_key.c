/*****************************************************************************
 * @文件: drv_key.c
 * @作者: cl
 * @日期: 2026-11-14
 * @版本: v2.0
 * @说明: 按键扫描驱动实现（20ms 消抖，输出按下/释放沿与持续时长）
 * @平台: STM32F103C8T6
 * @依赖: drv_key.h, bsp_key.h
 ****************************************************************************/
#include "drv_key.h"
#include "bsp_key.h"

/* ==== 模块常量 ==== */
/* 机械按键抖动通常 5–20ms，取 20ms 覆盖常见轻触开关
 * @来源 通用知识-待复核：轻触按键抖动时间典型 5–20ms */
#define DRV_KEY_DEBOUNCE_MS 20U

#define DRV_KEY_COUNT       2U

/* ==== 模块类型 ==== */
typedef struct
{
  GPIO_TypeDef *port;
  uint16_t pin;
} drv_key_pin_t;

typedef struct
{
  uint8_t raw;        /* 上一次采样到的原始电平 */
  uint8_t stable;     /* 消抖确认后的稳定电平 */
  uint32_t change_ms; /* 原始电平最后一次变化的时刻 */
  uint32_t press_ms;  /* 稳定按下时刻，用于计算 held_ms */
} drv_key_ctx_t;

/* ==== 模块变量 ==== */
static const drv_key_pin_t s_drv_key_pins[DRV_KEY_COUNT] =
{
  { BSP_KEY1_PORT, BSP_KEY1_PIN },
  { BSP_KEY2_PORT, BSP_KEY2_PIN }
};

static drv_key_ctx_t s_drv_key_ctx[DRV_KEY_COUNT];

/* ==== 内部函数 ==== */
/**
 * @说明: 读取单只按键的当前逻辑状态
 * @输入: index 按键序号（0=KEY1, 1=KEY2）
 * @输出: 1=按下，0=释放
 */
static uint8_t drv_key_read(uint8_t index)
{
  GPIO_PinState state;

  state = HAL_GPIO_ReadPin(s_drv_key_pins[index].port, s_drv_key_pins[index].pin);

  return (state == BSP_KEY_PRESSED_STATE) ? 1U : 0U;
}

/* ==== 接口函数 ==== */
/**
 * @说明: 初始化按键状态，按当前电平作为初始稳定值
 * @输出: 无
 * @注意: 上电时若按键已按下，不会误报按下沿
 */
void DRV_KEY_Init(void)
{
  uint8_t i;
  uint8_t raw;
  uint32_t now;

  now = HAL_GetTick();

  for (i = 0U; i < DRV_KEY_COUNT; i++)
  {
    raw = drv_key_read(i);
    s_drv_key_ctx[i].raw = raw;
    s_drv_key_ctx[i].stable = raw;
    s_drv_key_ctx[i].change_ms = now;
    s_drv_key_ctx[i].press_ms = now;
  }
}

/**
 * @说明: 扫描两只按键，输出各自的按下状态、边沿与持续时长
 * @输入: key1/key2 输出状态结构
 * @输出: 无，通过指针返回
 * @注意: 需在主循环高频调用（≥100Hz），否则 held_ms 分辨率变差
 * @注意: 本层只负责电平与计时，长按/短按的策略由上层决定
 */
void DRV_KEY_Scan(drv_key_state_t *key1, drv_key_state_t *key2)
{
  drv_key_state_t *out[DRV_KEY_COUNT];
  uint32_t now;
  uint8_t i;
  uint8_t raw;

  if ((key1 == 0) || (key2 == 0))
  {
    return;
  }

  out[0] = key1;
  out[1] = key2;
  now = HAL_GetTick();

  for (i = 0U; i < DRV_KEY_COUNT; i++)
  {
    raw = drv_key_read(i);

    out[i]->pressed = 0U;
    out[i]->pressed_edge = 0U;
    out[i]->released_edge = 0U;
    out[i]->held_ms = 0U;

    if (raw != s_drv_key_ctx[i].raw)
    {
      /* 电平刚变化，重新计时消抖窗口 */
      s_drv_key_ctx[i].raw = raw;
      s_drv_key_ctx[i].change_ms = now;
    }
    else if ((s_drv_key_ctx[i].stable != raw) &&
             ((uint32_t)(now - s_drv_key_ctx[i].change_ms) >= DRV_KEY_DEBOUNCE_MS))
    {
      /* 稳定满消抖窗口才确认状态翻转 */
      s_drv_key_ctx[i].stable = raw;

      if (raw != 0U)
      {
        out[i]->pressed_edge = 1U;
        s_drv_key_ctx[i].press_ms = now;
      }
      else
      {
        out[i]->released_edge = 1U;
      }
    }
    else
    {
      /* 抖动期间或状态未变，不产出事件 */
    }

    out[i]->pressed = s_drv_key_ctx[i].stable;
    if (s_drv_key_ctx[i].stable != 0U)
    {
      out[i]->held_ms = now - s_drv_key_ctx[i].press_ms;
    }
  }
}
