/*****************************************************************************
 * @文件: app_key.c
 * @作者: cl
 * @日期: 2026-11-14
 * @版本: v2.1
 * @说明: 按键业务实现（KEY1 长按开关输出；KEY2 短按切档、长按交替增减 0.1V）
 * @平台: STM32F103C8T6
 * @依赖: app_key.h, app_control.h, drv_key.h
 ****************************************************************************/
#include "app_key.h"
#include "app_control.h"
#include "drv_key.h"

/* ==== 模块常量 ==== */
#define APP_KEY_VOLT_COUNT   4U

/* 长按判定 500ms；重复间隔 200ms（0.5V/s）；步进 0.1V
 * @来源 你 2026-11-14：KEY1 长按开关输出；KEY2 长按交替增减，每次 0.1V
 * @来源 你 2026-11-14：调压速度过快，重复间隔由 100ms 放宽到 200ms */
#define APP_KEY_LONG_MS      500U
#define APP_KEY_REPEAT_MS    200U
#define APP_KEY_STEP_MV      100

/* 单拍最多补发次数：防止长时间阻塞后一次性跳变过大 */
#define APP_KEY_MAX_CATCHUP  5U

/* 电压钳位范围
 * @来源 memory CTRL_VREF_MIN_MV=3300 / CTRL_VREF_MAX_MV=12000 你 2026-11-13 */
#define APP_KEY_MIN_MV       3300
#define APP_KEY_MAX_MV       12000

/* 预设档位表：3.3 / 5 / 9 / 12V（整数 mV，避免控制链引入浮点）
 * @来源 memory CTRL_VREF_MIN_MV=3300 / CTRL_VREF_MAX_MV=12000 你 2026-11-13 */
static const uint32_t s_app_key_mv[APP_KEY_VOLT_COUNT] =
{
  3300U,
  5000U,
  9000U,
  12000U
};

/* ==== 模块变量 ==== */
static uint8_t s_app_key_long1;      /* KEY1 本次按下已执行长按动作 */
static uint8_t s_app_key_long2;      /* KEY2 本次按下已进入长按 */
static uint32_t s_app_key_next2_ms;  /* KEY2 下次重复触发的 held_ms 阈值 */
static int8_t s_app_key_dir2;        /* KEY2 长按方向：+1 增 / -1 减，每次长按取反 */

/* ==== 内部函数 ==== */
/**
 * @说明: 在当前设定电压上叠加增量并钳位到 3.3–12V
 * @输入: delta_mv 增量，单位 mV（负数为降压）
 * @输出: 无
 */
static void app_key_adjust(int32_t delta_mv)
{
  int32_t target;

  target = (int32_t)APP_CONTROL_GetVoltageMv() + delta_mv;

  if (target < APP_KEY_MIN_MV)
  {
    target = APP_KEY_MIN_MV;
  }
  else if (target > APP_KEY_MAX_MV)
  {
    target = APP_KEY_MAX_MV;
  }
  else
  {
    /* 在范围内，保持 */
  }

  APP_CONTROL_SetVoltageMv((uint32_t)target);
}

/**
 * @说明: 切到比当前电压大的下一个预设档，超过最高档则回绕到 3.3V
 * @输出: 无
 * @注意: 无极调压后电压可能不在预设点上，此时切到"下一个更大的预设"
 */
static void app_key_next_preset(void)
{
  uint32_t current;
  uint8_t i;

  current = APP_CONTROL_GetVoltageMv();

  for (i = 0U; i < APP_KEY_VOLT_COUNT; i++)
  {
    if (s_app_key_mv[i] > current)
    {
      break;
    }
  }

  if (i >= APP_KEY_VOLT_COUNT)
  {
    i = 0U;
  }

  APP_CONTROL_SetVoltageMv(s_app_key_mv[i]);
}

/**
 * @说明: 切换输出开关状态
 * @输出: 无
 */
static void app_key_toggle_output(void)
{
  if (APP_CONTROL_GetOutput() == 0U)
  {
    APP_CONTROL_SetOutput(1U);
  }
  else
  {
    APP_CONTROL_SetOutput(0U);
  }
}

/* ==== 接口函数 ==== */
/**
 * @说明: 初始化按键业务状态：档位归 3.3V、输出关闭、长按方向初始为"减"
 * @输出: 无
 * @注意: 必须在 APP_CONTROL_Init 之后调用
 * @来源 需求：上电默认设定 3.3V 且无电压输出 你 2026-11-13
 * @来源 需求：KEY2 第一次长按为增加，故初值取 -1，首次长按取反后为 +1 你 2026-11-14
 */
void APP_KEY_Init(void)
{
  /* 必须先初始化驱动上下文：否则 ctx 全 0（=按下），首次扫描会把
   * 空闲按键误判为按下，长按逻辑随即被误触发 */
  DRV_KEY_Init();

  s_app_key_long1 = 0U;
  s_app_key_long2 = 0U;
  s_app_key_next2_ms = APP_KEY_LONG_MS;
  s_app_key_dir2 = -1;

  APP_CONTROL_SetVoltageMv(s_app_key_mv[0]);
  APP_CONTROL_SetOutput(0U);
}

/**
 * @说明: 扫描按键并执行动作
 * @输出: 无
 * @注意: 需在主循环每轮调用
 * @注意: KEY1 长按(≥500ms)切换输出，短按无动作
 * @注意: KEY2 短按切预设档；长按进入调压，方向每次长按取反，每 200ms 变 0.1V
 */
void APP_KEY_Task(void)
{
  drv_key_state_t key1;
  drv_key_state_t key2;
  uint8_t catchup;

  DRV_KEY_Scan(&key1, &key2);

  /* ---- KEY1：长按切换输出（短按无动作，避免误触） ---- */
  if (key1.pressed_edge != 0U)
  {
    s_app_key_long1 = 0U;
  }

  if ((key1.pressed != 0U) && (key1.held_ms >= APP_KEY_LONG_MS) &&
      (s_app_key_long1 == 0U))
  {
    s_app_key_long1 = 1U;
    app_key_toggle_output();
  }

  /* ---- KEY2：短按切档，长按交替增减 ---- */
  if (key2.pressed_edge != 0U)
  {
    s_app_key_long2 = 0U;
    s_app_key_next2_ms = APP_KEY_LONG_MS;
  }

  catchup = 0U;
  while ((key2.pressed != 0U) && (key2.held_ms >= s_app_key_next2_ms) &&
         (catchup < APP_KEY_MAX_CATCHUP))
  {
    if (s_app_key_long2 == 0U)
    {
      /* 长按首次触发：取反方向（首次为增，因为初值是 -1） */
      s_app_key_long2 = 1U;
      s_app_key_dir2 = (int8_t)(-s_app_key_dir2);
    }

    s_app_key_next2_ms += APP_KEY_REPEAT_MS;
    app_key_adjust((int32_t)s_app_key_dir2 * APP_KEY_STEP_MV);
    catchup++;
  }

  if ((key2.released_edge != 0U) && (s_app_key_long2 == 0U))
  {
    app_key_next_preset();
  }
}
