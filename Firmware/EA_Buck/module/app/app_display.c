/*****************************************************************************
 * @文件: app_display.c
 * @作者: cl
 * @日期: 2026-11-14
 * @版本: v1.0
 * @说明: OLED 显示实现（电压/电流/输出状态/设定值，2Hz 刷新）
 * @平台: STM32F103C8T6
 * @依赖: app_display.h, app_control.h, drv_oled.h, stm32f1xx
 ****************************************************************************/
#include "app_display.h"
#include "app_control.h"
#include "app_key.h"
#include "drv_oled.h"
#include "stm32f1xx.h"

/* ==== 模块常量 ==== */
/* 刷新周期 500ms = 2Hz：人眼足够，且全屏刷新 23ms 不占用过多主循环
 * @来源 memory CLOCK_HCLK=72MHz 推导：500ms × 72000 = 36e6 cycles */
#define APP_DISPLAY_PERIOD_CYCLES  (500U * 72000U)

/* 显示行位置（页号，每页 8 像素高）
 * @来源 SSD1306 datasheet 10.1.4：64 行 = 8 页 */
#define APP_DISPLAY_PAGE_VOLT   0U
#define APP_DISPLAY_PAGE_CURR   2U
#define APP_DISPLAY_PAGE_STATE  4U
#define APP_DISPLAY_PAGE_SET    6U

/* ==== 模块变量 ==== */
static uint32_t s_last_cycles;

/* ==== 内部函数 ==== */
/**
 * @说明: 无符号整数转十进制字符串
 * @输入: buf 目标缓冲；value 待转换值
 * @输出: 写入的字符数
 */
static uint8_t app_display_u32(char *buf, uint32_t value)
{
  char tmp[10];
  uint8_t n = 0U;
  uint8_t i = 0U;

  do
  {
    tmp[n] = (char)('0' + (char)(value % 10U));
    value /= 10U;
    n++;
  } while ((value != 0U) && (n < 10U));

  while (n > 0U)
  {
    n--;
    buf[i] = tmp[n];
    i++;
  }

  return i;
}

/**
 * @说明: 毫伏值格式化为 "X.XX"（两位小数）
 * @输入: buf 目标缓冲；mv 电压值（mV）
 * @输出: 写入的字符数
 */
static uint8_t app_display_mv(char *buf, int32_t mv)
{
  uint32_t integer_part;
  uint32_t frac_part;
  uint8_t n;

  if (mv < 0)
  {
    mv = 0;
  }

  integer_part = (uint32_t)mv / 1000U;
  frac_part = ((uint32_t)mv % 1000U) / 10U;

  n = app_display_u32(buf, integer_part);
  buf[n] = '.';
  n++;
  buf[n] = (char)('0' + (char)(frac_part / 10U));
  n++;
  buf[n] = (char)('0' + (char)(frac_part % 10U));
  n++;

  return n;
}

/**
 * @说明: 拼一段固定字符串到缓冲区
 * @输入: buf 目标缓冲；pos 起始位置；text 源字符串
 * @输出: 写入后的新位置
 */
static uint8_t app_display_text(char *buf, uint8_t pos, const char *text)
{
  while (*text != 0)
  {
    buf[pos] = *text;
    pos++;
    text++;
  }

  return pos;
}

/* ==== 接口函数 ==== */
/**
 * @说明: 初始化 OLED 并复位刷新节拍
 * @输出: 无
 * @注意: 需在 I2C 初始化之后、主循环之前调用
 */
void APP_DISPLAY_Init(void)
{
  (void)DRV_OLED_Init();
  s_last_cycles = DWT->CYCCNT;
}

/**
 * @说明: 按 2Hz 刷新显示内容
 * @输出: 无
 * @注意: 全屏刷新约 23ms 阻塞；控制已在 ISR 中，主循环阻塞不影响控制
 */
void APP_DISPLAY_Task(void)
{
  char line[24];
  int32_t vout_mv = 0;
  int32_t iout_ma = 0;
  uint32_t vref_mv = 0U;
  uint8_t output_on = 0U;
  uint8_t fault = 0U;
  uint32_t now_cycles;
  uint8_t n;

  now_cycles = DWT->CYCCNT;
  if ((uint32_t)(now_cycles - s_last_cycles) < APP_DISPLAY_PERIOD_CYCLES)
  {
    return;
  }
  s_last_cycles = now_cycles;

  APP_CONTROL_GetSnapshot(&vout_mv, &iout_ma, &vref_mv, &output_on, &fault);

  DRV_OLED_Fill(0x00U);

  /* 第 1 行：输出电压 */
  n = app_display_text(line, 0U, "V:");
  n = (uint8_t)(n + app_display_mv(&line[n], vout_mv));
  n = app_display_text(line, n, "V");
  line[n] = 0;
  DRV_OLED_DrawString(0U, APP_DISPLAY_PAGE_VOLT, line);

  /* 第 2 行：输出电流 */
  n = app_display_text(line, 0U, "I:");
  n = (uint8_t)(n + app_display_u32(&line[n], (iout_ma < 0) ? 0U : (uint32_t)iout_ma));
  n = app_display_text(line, n, "MA");
  line[n] = 0;
  DRV_OLED_DrawString(0U, APP_DISPLAY_PAGE_CURR, line);

  /* 第 3 行：输出状态 */
  if (fault != 0U)
  {
    n = app_display_text(line, 0U, "STATE:FAULT");
  }
  else if (output_on != 0U)
  {
    n = app_display_text(line, 0U, "STATE:ON");
  }
  else
  {
    n = app_display_text(line, 0U, "STATE:OFF");
  }
  line[n] = 0;
  DRV_OLED_DrawString(0U, APP_DISPLAY_PAGE_STATE, line);

  /* 第 4 行：设定电压 + 无极调压方向标识（++ 增 / -- 减） */
  n = app_display_text(line, 0U, "SET:");
  n = (uint8_t)(n + app_display_mv(&line[n], (int32_t)vref_mv));
  n = app_display_text(line, n, "V ");
  if (APP_KEY_GetDirection() > 0)
  {
    n = app_display_text(line, n, "++");
  }
  else
  {
    n = app_display_text(line, n, "--");
  }
  line[n] = 0;
  DRV_OLED_DrawString(0U, APP_DISPLAY_PAGE_SET, line);

  DRV_OLED_Refresh();
}
