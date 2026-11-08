/*****************************************************************************
 * @文件: drv_uart.h
 * @作者: cl
 * @日期: 2026-10-23
 * @版本: v1.0
 * @说明: USART1 阻塞发送驱动（调试后端 UART 分支）
 * @依赖: Core 的 usart.c（huart1 已由 CubeMX 配好 115200 8N1）
 ****************************************************************************/
#ifndef __DRV_UART_H
#define __DRV_UART_H

#include <stdint.h>

void DRV_UART_Write(const char *s, uint32_t len);

#endif /* __DRV_UART_H */
