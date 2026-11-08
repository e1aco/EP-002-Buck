/*****************************************************************************
 * @文件: drv_oled.h
 * @作者: cl
 * @日期: 2026-11-08
 * @版本: v1.0
 * @说明: SSD1306 128x64 OLED 驱动（I2C1 400kHz，页寻址写显存，脏页局部刷新）
 * @依赖: Core 的 i2c.c（hi2c1，PB6=SCL/PB7=SDA）
 ****************************************************************************/
#ifndef __DRV_OLED_H
#define __DRV_OLED_H

#include <stdint.h>

/* 模块地址：0.96" SSD1306 常见 0x3C（SA0 接地）/ 0x3D（SA0 接 VCC），初始化时自动探测
 * @来源 通用知识-待复核（SSD1306 数据手册从机地址表，项目无该手册） */
#define DRV_OLED_ADDR_A         0x3CU
#define DRV_OLED_ADDR_B         0x3DU

#define DRV_OLED_W              128U
#define DRV_OLED_H              64U
#define DRV_OLED_PAGES          8U

/* 单次 I2C 传输上限：128B 数据 @100kHz ≈ 11.6ms（@400kHz ≈ 2.9ms），取 30ms 留余量
 * @来源 经验值-待复核（两种位速率下的理论传输时间 ± 从机拉伸余量） */
#define DRV_OLED_IO_TIMEOUT_MS  30U

/**
 * @说明: 探测地址（0x3C→0x3D）+ 下发初始化序列，可重复调用（掉线重连）
 * @返回: 1=就绪；0=未找到或传输失败
 * @注意: 屏上电后需 ≥100ms 再调用（等待 VDD 稳定）
 */
uint8_t DRV_OLED_Init(void);
uint8_t DRV_OLED_IsReady(void);
uint8_t DRV_OLED_GetAddr(void);
uint8_t DRV_OLED_GetGeneration(void);
uint32_t DRV_OLED_GetErrorCount(void);

/**
 * @说明: 当前 I2C1 实际速率（400000 = 正常；100000 = 已因错误回退）
 */
uint32_t DRV_OLED_GetBusHz(void);

/**
 * @说明: 清显存（只改本地帧缓冲，随 Flush 一起下发）
 */
void DRV_OLED_Clear(void);

/**
 * @说明: 清矩形区域（列起点 x、页起点 page、宽 w、页数 pages），标记脏页
 * @注意: x 必须为 8 的倍数；越界自动裁剪
 */
void DRV_OLED_ClearArea(uint8_t x, uint8_t page, uint8_t w, uint8_t pages);

/**
 * @说明: 在 (x, y) 叠加画 8x16 字符串（按像素 OR 写入，不自动清背景）
 * @注意: x 必须为 8 的倍数；超出屏右/下边界自动裁剪；字符集 0x20-0x7E，其余画 '?'
 */
void DRV_OLED_DrawText(uint8_t x, uint8_t y, const char *s);

/**
 * @说明: 只把有变化的页写到屏；1=全部成功
 * @注意: 连续 3 次失败 → 复位 I2C1 并重探测，屏不在线则置为未就绪（上层周期重连）
 */
uint8_t DRV_OLED_Flush(void);

#endif /* __DRV_OLED_H */
