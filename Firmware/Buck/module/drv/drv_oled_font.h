/*****************************************************************************
 * @文件: drv_oled_font.h
 * @作者: cl
 * @日期: 2026-11-08
 * @版本: v1.0
 * @说明: SSD1306 8x16 ASCII 点阵字库（0x20-0x7E，行字节 LSB=最左像素）
 * @依赖: 无
 ****************************************************************************/
#ifndef __DRV_OLED_FONT_H
#define __DRV_OLED_FONT_H

#include <stdint.h>

#define DRV_OLED_FONT_W     8U      /* 字符宽（像素） */
#define DRV_OLED_FONT_H     16U     /* 字符高（像素） */
#define DRV_OLED_FONT_FIRST 0x20U   /* 首个可打印字符 */
#define DRV_OLED_FONT_COUNT 95U     /* 0x20-0x7E */

/* 字形：每个字符 16 字节，1 字节 = 1 行像素，bit0 为最左像素
 * @来源 CLI 生成脚本（Arial 16px 光栅化后阈值二值化），非运行期依赖 */
extern const uint8_t DRV_OLED_FONT8X16[DRV_OLED_FONT_COUNT][DRV_OLED_FONT_H];

#endif /* __DRV_OLED_FONT_H */
