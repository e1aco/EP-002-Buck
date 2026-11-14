/*****************************************************************************
 * @文件: drv_oled.h
 * @作者: cl
 * @日期: 2026-11-14
 * @版本: v1.1
 * @说明: SSD1306 OLED 驱动接口（I2C，128x64 单色）
 * @平台: STM32F103C8T6
 * @依赖: stdint.h
 ****************************************************************************/
#ifndef DRV_OLED_H
#define DRV_OLED_H

#include <stdint.h>

/* 显存尺寸：128×64 单色，按页组织 8 页 × 128 字节 = 1024B
 * @来源 SSD1306 datasheet 10.1.4：128×64 显存，页寻址模式 8 页 */
#define DRV_OLED_WIDTH      128U
#define DRV_OLED_HEIGHT     64U
#define DRV_OLED_PAGES      (DRV_OLED_HEIGHT / 8U)

uint8_t DRV_OLED_Probe(void);
uint8_t DRV_OLED_Init(void);
void DRV_OLED_Fill(uint8_t pattern);
void DRV_OLED_Refresh(void);
void DRV_OLED_DrawChar(uint8_t x, uint8_t page, char ch);
void DRV_OLED_DrawString(uint8_t x, uint8_t page, const char *str);

#endif /* DRV_OLED_H */
