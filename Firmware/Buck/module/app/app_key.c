/*****************************************************************************
 * @文件: app_key.c
 * @作者: cl
 * @日期: 2026-11-06
 * @版本: v1.0
 * @说明: 按键应用（消抖 + 按下沿事件：Key1 输出开关、Key2 5V/12V 切换）
 * @平台: STM32F103C8T6
 * @依赖: bsp_key（裸电平）、app_control（业务动作）、main.h（HAL_GetTick）
 ****************************************************************************/
#include "app_key.h"
#include "bsp_key.h"
#include "app_control.h"
#include "main.h"

/* 消抖时间：机械按键抖动典型 1~10ms（通用经验），取 2 倍上限 20ms
 * 只用按下沿触发一次（按住不重复），松开不动作
 * @来源 通用知识：机械触点抖动 1~10ms；HAL_GetTick 分辨率 1ms */
#define APP_KEY_DEBOUNCE_MS     20U

/* 单个按键的消抖状态 */
typedef struct
{
  uint8_t  stable;      /* 已确认的稳定电平（1 = 按下） */
  uint8_t  raw_last;    /* 上一次采样电平 */
  uint32_t change_ms;   /* 电平最后一次变化的时刻 */
} app_key_state_t;

/* ==== 私有变量 ==== */
static app_key_state_t s_key[BSP_KEY_NUM];

/* ==== 私有函数 ==== */

/**
 * @说明: 把"按下沿"翻译成业务动作
 * @输入: id 按键编号
 * @注意: Key2 关机时也响应，只改 SET 档位，开机按该档位出压
 * @来源 你（2026-11-06 按键功能需求；2026-11-08 关机也可切 SET）
 */
static void app_key_dispatch(bsp_key_id_t id)
{
  if (id == BSP_KEY_1)
  {
    APP_CONTROL_OutputToggle();
  }
  else if (id == BSP_KEY_2)
  {
    APP_CONTROL_CycleVref();
  }
  else
  {
    /* 无动作：未知按键 */
  }
}

/* ==== 接口函数 ==== */

/**
 * @说明: 初始化按键状态（当前电平即稳定初值，避免上电瞬间误判为按下沿）
 * @来源 通用知识：以初始电平填充 stable/raw_last，消抖从静止态开始
 */
void APP_KEY_Init(void)
{
  uint32_t now = HAL_GetTick();
  uint8_t i;

  for (i = 0U; i < (uint8_t)BSP_KEY_NUM; i++)
  {
    s_key[i].stable = BSP_KEY_IsDown((bsp_key_id_t)i);
    s_key[i].raw_last = s_key[i].stable;
    s_key[i].change_ms = now;
  }
}

/**
 * @说明: 主循环调用——逐键消抖，稳定电平由松开变按下时派发一次事件
 * @来源 通用知识：电平持续 APP_KEY_DEBOUNCE_MS 不变才认定，抖动期重新计时
 */
void APP_KEY_Loop(void)
{
  uint32_t now = HAL_GetTick();
  uint8_t i;

  for (i = 0U; i < (uint8_t)BSP_KEY_NUM; i++)
  {
    uint8_t raw = BSP_KEY_IsDown((bsp_key_id_t)i);

    if (raw != s_key[i].raw_last)
    {
      s_key[i].raw_last = raw;
      s_key[i].change_ms = now;
      continue;
    }

    if (raw == s_key[i].stable)
    {
      continue;
    }

    if ((uint32_t)(now - s_key[i].change_ms) < APP_KEY_DEBOUNCE_MS)
    {
      continue;
    }

    s_key[i].stable = raw;
    if (raw != 0U)
    {
      app_key_dispatch((bsp_key_id_t)i);
    }
  }
}
