/*****************************************************************************
 * @文件: service_dbg.c
 * @作者: cl
 * @日期: 2026-10-23
 * @版本: v1.0
 * @说明: 调试输出服务：格式化 + 按 CL_DBG_BACKEND 分派到 RTT 或 UART
 * @平台: STM32F103C8T6
 * @依赖: module/config.h（后端开关）, drv_rtt, drv_uart
 * @注意: RTT 与 UART 互斥——RTT 走 SWD 口，UART 走 PA9/PA10，同一窗口只能连一个
 ****************************************************************************/
#include <stdarg.h>
#include <stdio.h>
#include "service_dbg.h"
#include "config.h"
#include "drv_rtt.h"
#include "drv_uart.h"

/* 单行缓冲：128B 足够 1Hz 遥测行（实测 ~60B），静态分配避免占用栈
 * 依据 .cl/memory/chip.md STACK_SIZE=0x400（1024B），128B 局部数组会吃掉 1/8 栈 */
#define SERVICE_DBG_BUF_SIZE   128U

static char s_buf[SERVICE_DBG_BUF_SIZE];

/* ==== 接口函数 ==== */

/**
 * @说明: 初始化调试后端（RTT 需显式初始化控制块；UART 由 MX_USART1_UART_Init 负责）
 */
void SERVICE_DBG_Init(void)
{
#if (CL_DBG_BACKEND == CL_DBG_BACKEND_RTT)
  DRV_RTT_Init();
#endif
}

/**
 * @说明: 格式化并输出一行调试信息（等价 printf，不带浮点）
 * @输入: fmt 格式串（纯 ASCII）；... 变参
 * @注意: 输出超长按 127 字节截断；缓冲满时 RTT 丢弃、UART 阻塞至超时
 */
void SERVICE_DBG_Printf(const char *fmt, ...)
{
  va_list ap;
  int n;

  if (fmt == NULL)
  {
    return;
  }

  va_start(ap, fmt);
  n = vsnprintf(s_buf, SERVICE_DBG_BUF_SIZE, fmt, ap);
  va_end(ap);

  if (n <= 0)
  {
    return;
  }

  if ((uint32_t)n >= SERVICE_DBG_BUF_SIZE)
  {
    n = (int)(SERVICE_DBG_BUF_SIZE - 1U);
  }

#if (CL_DBG_BACKEND == CL_DBG_BACKEND_RTT)
  DRV_RTT_Write(s_buf, (uint32_t)n);
#else
  DRV_UART_Write(s_buf, (uint32_t)n);
#endif
}

/**
 * @说明: 读调试口下行数据（RTT 后端走 RTT 下行通道 0）
 * @输入: buf 目标缓冲；len 最多读取字节数
 * @输出: 实际读出字节数
 * @注意: UART 后端未接收包（RX 由 DMA 循环缓冲持有），固定返回 0
 */
uint32_t SERVICE_DBG_Read(char *buf, uint32_t len)
{
#if (CL_DBG_BACKEND == CL_DBG_BACKEND_RTT)
  return DRV_RTT_Read(buf, len);
#else
  (void)buf;
  (void)len;
  return 0U;
#endif
}
