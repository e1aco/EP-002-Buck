/*****************************************************************************
 * @文件: app_key.c
 * @作者: cl
 * @日期: 2026-11-13
 * @版本: v1.0
 * @说明: 按键业务实现（KEY1 开关输出、KEY2 循环切换 3.3/5/9/12V）
 * @平台: STM32F103C8T6
 * @依赖: app_key.h, app_control.h, drv_key.h
 ****************************************************************************/
#include "app_key.h"
#include "app_control.h"
#include "drv_key.h"

/* ==== 模块常量 ==== */
#define APP_KEY_VOLT_COUNT 4U

/* 档位表：3.3V → 5V → 9V → 12V 循环（整数 mV，避免控制链引入浮点）
 * @来源 memory CTRL_VREF_MIN_MV=3300 / CTRL_VREF_MAX_MV=12000 你 2026-11-13 */
static const uint32_t s_app_key_mv[APP_KEY_VOLT_COUNT] =
{
  3300U,
  5000U,
  9000U,
  12000U
};

/* ==== 模块变量 ==== */
static uint8_t s_app_key_index;

/* ==== 接口函数 ==== */
/**
 * @说明: 初始化按键业务状态：档位归 3.3V，输出关闭
 * @输出: 无
 * @注意: 必须在 APP_CONTROL_Init 之后调用
 * @来源 需求：上电默认设定 3.3V 且无电压输出 你 2026-11-13
 */
void APP_KEY_Init(void)
{
  s_app_key_index = 0U;
  APP_CONTROL_SetVoltageMv(s_app_key_mv[s_app_key_index]);
  APP_CONTROL_SetOutput(0U);
}

/**
 * @说明: 扫描按键并执行对应动作
 * @输出: 无
 * @注意: 需在主循环每轮调用；只在按下沿触发一次
 */
void APP_KEY_Task(void)
{
  uint8_t events;

  events = DRV_KEY_Scan();

  /* KEY2：切换到下一档，到顶回绕 */
  if ((events & DRV_KEY_EVENT_KEY2) != 0U)
  {
    s_app_key_index++;
    if (s_app_key_index >= APP_KEY_VOLT_COUNT)
    {
      s_app_key_index = 0U;
    }

    APP_CONTROL_SetVoltageMv(s_app_key_mv[s_app_key_index]);
  }

  /* KEY1：开/关输出 */
  if ((events & DRV_KEY_EVENT_KEY1) != 0U)
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
}
