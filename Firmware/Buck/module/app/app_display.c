/*****************************************************************************
 * @文件: app_display.c
 * @作者: cl
 * @日期: 2026-11-08
 * @版本: v1.0
 * @说明: 0.96" OLED 显示：4 行 8x16（OUT 状态 / Vout / Iout / Set），10Hz 变化行刷新
 * @依赖: app_control.c（闭环取数）、drv_oled.c（SSD1306）、service_dbg（启动日志）
 ****************************************************************************/
#include <stdio.h>
#include <string.h>
#include "app_display.h"
#include "app_control.h"
#include "drv_oled.h"
#include "service_dbg.h"
#include "main.h"

/* ==== 私有宏 ==== */
/* 刷新节拍：10Hz
 * 推导：400kHz 下 1 页 =(3+1+128)B×9bit÷400k ≈ 3.0ms；稳态 V/I 两行 4 页 ≈ 12ms
 *        占 100ms 预算 ≈ 12%；回退 100kHz 时 ≈ 47ms（控制环在 2kHz ISR，不受阻塞影响）
 * @来源 推演链（I2C 位速率×字节数；旧 100kHz 实测 23~46ms/帧 佐证） */
#define APP_DISP_PERIOD_MS      100U
#define APP_DISP_RETRY_MS       1000U   /* 屏掉线后的重探测周期 */

#define APP_DISP_LINES          4U
#define APP_DISP_LINE_H         16U     /* 与字库高度一致：一行占 2 页 */
#define APP_DISP_LINE_CHARS     16U     /* 128px / 8px */

/* ==== 私有变量 ==== */
static char s_cache[APP_DISP_LINES][APP_DISP_LINE_CHARS + 1U];
static uint32_t s_last_ms;
static uint32_t s_retry_ms;
static uint8_t s_gen;                   /* 屏初始化代数，变化 = 需整屏重绘 */

/* ==== 私有函数 ==== */
/**
 * @说明: 单行更新：文本未变则跳过，变了则清该行 2 页 + 重画
 */
static void app_disp_line(uint8_t idx, const char *text)
{
  if (strncmp(s_cache[idx], text, APP_DISP_LINE_CHARS) == 0)
  {
    return;
  }
  strncpy(s_cache[idx], text, APP_DISP_LINE_CHARS);
  s_cache[idx][APP_DISP_LINE_CHARS] = '\0';
  DRV_OLED_ClearArea(0U, (uint8_t)(idx * 2U), DRV_OLED_W, 2U);
  DRV_OLED_DrawText(0U, (uint8_t)(idx * APP_DISP_LINE_H), text);
}

static void app_disp_render(void)
{
  char line[APP_DISP_LINE_CHARS + 2U];
  uint32_t v_mv = APP_CONTROL_GetVoutMv();
  uint32_t i_ma = (uint32_t)APP_CONTROL_GetIoutMa();
  uint32_t set_mv = APP_CONTROL_GetVrefTargetMv();

  if (APP_CONTROL_GetOutputEnabled() != 0U)
  {
    app_disp_line(0U, "OUT: ON");
  }
  else if (APP_CONTROL_GetNoLoad() != 0U)
  {
    app_disp_line(0U, "OUT: OFF NOLOAD");
  }
  else
  {
    app_disp_line(0U, "OUT: OFF");
  }

  (void)snprintf(line, sizeof(line), "Vout %2lu.%02lu V",
                 (unsigned long)(v_mv / 1000U), (unsigned long)((v_mv % 1000U) / 10U));
  app_disp_line(1U, line);

  (void)snprintf(line, sizeof(line), "Iout %lu.%03lu A",
                 (unsigned long)(i_ma / 1000U), (unsigned long)(i_ma % 1000U));
  app_disp_line(2U, line);

  (void)snprintf(line, sizeof(line), "Set  %2lu.%02lu V",
                 (unsigned long)(set_mv / 1000U), (unsigned long)((set_mv % 1000U) / 10U));
  app_disp_line(3U, line);

  (void)DRV_OLED_Flush();     /* 只下发有变化的页 */
}

/* ==== 接口函数 ==== */
void APP_DISPLAY_Init(void)
{
  memset(s_cache, 0, sizeof(s_cache));
  s_last_ms = HAL_GetTick();
  s_retry_ms = s_last_ms;

  HAL_Delay(100U);            /* SSD1306 上电 VDD 稳定时间（通用序列要求） */
  if (DRV_OLED_Init())
  {
    SERVICE_DBG_Printf("[DISP] ok addr=0x%02X bus=%lukHz\r\n", DRV_OLED_GetAddr(),
                       (unsigned long)(DRV_OLED_GetBusHz() / 1000U));
    s_gen = DRV_OLED_GetGeneration();
    app_disp_render();
  }
  else
  {
    SERVICE_DBG_Printf("[DISP] not found 0x3C/0x3D\r\n");
  }
}

void APP_DISPLAY_Loop(void)
{
  uint32_t now = HAL_GetTick();

  if ((uint32_t)(now - s_last_ms) < APP_DISP_PERIOD_MS)
  {
    return;
  }
  s_last_ms = now;

  if (!DRV_OLED_IsReady())
  {
    if ((uint32_t)(now - s_retry_ms) < APP_DISP_RETRY_MS)
    {
      return;
    }
    s_retry_ms = now;
    if (!DRV_OLED_Init())
    {
      return;
    }
    SERVICE_DBG_Printf("[DISP] up addr=0x%02X\r\n", DRV_OLED_GetAddr());
    memset(s_cache, 0, sizeof(s_cache));    /* 屏显存已被初始化清空 → 强制重绘 */
    s_gen = DRV_OLED_GetGeneration();
  }
  else if (s_gen != DRV_OLED_GetGeneration())
  {
    memset(s_cache, 0, sizeof(s_cache));    /* 驱动内部重初始化后同样重绘 */
    s_gen = DRV_OLED_GetGeneration();
  }
  app_disp_render();
}
