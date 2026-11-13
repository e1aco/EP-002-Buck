/*****************************************************************************
 * @文件: drv_key.c
 * @作者: cl
 * @日期: 2026-11-13
 * @版本: v1.0
 * @说明: 按键扫描驱动实现（20ms 消抖，只在按下沿产出事件）
 * @平台: STM32F103C8T6
 * @依赖: drv_key.h, bsp_key.h
 ****************************************************************************/
#include "drv_key.h"
#include "bsp_key.h"

/* ==== 模块常量 ==== */
/* 机械按键抖动通常 5–20ms，取 20ms 覆盖常见轻触开关
 * @来源 通用知识-待复核：轻触按键抖动时间典型 5–20ms */
#define DRV_KEY_DEBOUNCE_MS 20U

/* ==== 模块类型 ==== */
typedef struct
{
  GPIO_TypeDef *port;
  uint16_t pin;
} drv_key_pin_t;

/* ==== 模块变量 ==== */
static const drv_key_pin_t s_drv_key_pins[DRV_KEY_COUNT] =
{
  { BSP_KEY1_PORT, BSP_KEY1_PIN },
  { BSP_KEY2_PORT, BSP_KEY2_PIN }
};

static uint8_t s_drv_key_raw[DRV_KEY_COUNT];
static uint8_t s_drv_key_stable[DRV_KEY_COUNT];
static uint32_t s_drv_key_change_ms[DRV_KEY_COUNT];

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
 * @注意: 上电时若按键已按下，不会误报一次按下事件
 */
void DRV_KEY_Init(void)
{
  uint8_t i;
  uint8_t raw;

  for (i = 0U; i < DRV_KEY_COUNT; i++)
  {
    raw = drv_key_read(i);
    s_drv_key_raw[i] = raw;
    s_drv_key_stable[i] = raw;
    s_drv_key_change_ms[i] = HAL_GetTick();
  }
}

/**
 * @说明: 扫描按键，返回本轮新产生的按下沿事件
 * @输出: 事件位（DRV_KEY_EVENT_KEY1 / DRV_KEY_EVENT_KEY2），无事件返回 0
 * @注意: 需在主循环高频调用（≥100Hz）；电平变化后需稳定 20ms 才确认
 */
uint8_t DRV_KEY_Scan(void)
{
  uint8_t events = 0U;
  uint8_t i;
  uint8_t raw;
  uint32_t now;

  now = HAL_GetTick();

  for (i = 0U; i < DRV_KEY_COUNT; i++)
  {
    raw = drv_key_read(i);

    if (raw != s_drv_key_raw[i])
    {
      /* 电平刚变化，重新计时消抖窗口 */
      s_drv_key_raw[i] = raw;
      s_drv_key_change_ms[i] = now;
    }
    else if ((s_drv_key_stable[i] != raw) &&
             ((uint32_t)(now - s_drv_key_change_ms[i]) >= DRV_KEY_DEBOUNCE_MS))
    {
      /* 稳定满消抖窗口才确认状态翻转 */
      s_drv_key_stable[i] = raw;

      if (raw != 0U)
      {
        events |= (uint8_t)(1U << i);
      }
    }
    else
    {
      /* 抖动期间或状态未变，不产出事件 */
    }
  }

  return events;
}
