/*****************************************************************************
 * @文件: drv_uart.c
 * @作者: cl
 * @日期: 2026-10-23
 * @版本: v1.0
 * @说明: USART1 阻塞发送驱动（调试后端 UART 分支，1Hz 遥测用，不追求吞吐）
 * @平台: STM32F103C8T6（USART1 -> PA9/PA10）
 * @依赖: Core 的 usart.c（huart1）
 ****************************************************************************/
#include <stddef.h>
#include "drv_uart.h"
#include "usart.h"

/* 阻塞发送超时（ms）：1Hz 一行 ~50B，@115200 8N1 = 10bit/字节 -> 50*10/115200 = 4.3ms
 * 取 100ms 留 ~23 倍余量；超时直接丢弃，绝不因调试打印卡住控制循环 */
#define DRV_UART_TX_TIMEOUT_MS   100U

/* ==== 接口函数 ==== */

/**
 * @说明: 阻塞发送一段字符串
 * @输入: s 数据首地址；len 字节数（超过 uint16 上限按 65535 截断）
 * @注意: 仅 UART 后端使用；RTT 后端不走本函数
 * @来源 memory UART1_BAUD=115200 8N1 推导：50B*10/115200=4.3ms << 100ms 超时
 */
void DRV_UART_Write(const char *s, uint32_t len)
{
  uint16_t n;

  if ((s == NULL) || (len == 0U))
  {
    return;
  }

  n = (len > 0xFFFFU) ? 0xFFFFU : (uint16_t)len;

  (void)HAL_UART_Transmit(&huart1, (uint8_t *)s, n, DRV_UART_TX_TIMEOUT_MS);
}
