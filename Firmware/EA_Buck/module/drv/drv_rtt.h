/*****************************************************************************
 * @文件: drv_rtt.h
 * @作者: cl
 * @日期: 2026-11-13
 * @版本: v1.0
 * @说明: SEGGER RTT 上行通道驱动接口
 * @平台: STM32F103C8T6
 * @依赖: stdint.h
 ****************************************************************************/
#ifndef DRV_RTT_H
#define DRV_RTT_H

#include <stdint.h>

void DRV_RTT_Init(void);
uint16_t DRV_RTT_Write(const char *text, uint16_t len);

#endif /* DRV_RTT_H */
