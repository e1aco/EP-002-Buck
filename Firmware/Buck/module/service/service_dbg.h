/*****************************************************************************
 * @文件: service_dbg.h
 * @作者: cl
 * @日期: 2026-10-23
 * @版本: v1.0
 * @说明: 调试输出服务（编译期在 RTT / UART 之间二选一，见 config.h CL_DBG_BACKEND）
 * @依赖: drv_rtt / drv_uart
 ****************************************************************************/
#ifndef __SERVICE_DBG_H
#define __SERVICE_DBG_H

#include <stdint.h>

void SERVICE_DBG_Init(void);
void SERVICE_DBG_Printf(const char *fmt, ...);
/**
 * @说明: 读调试口下行数据（仅 RTT 后端支持；UART 后端返回 0）
 * @输入: buf 目标缓冲；len 最多读取字节数
 * @输出: 实际读出字节数
 * @注意: 非阻塞；供调参等测试功能接收主机命令
 */
uint32_t SERVICE_DBG_Read(char *buf, uint32_t len);

#endif /* __SERVICE_DBG_H */
