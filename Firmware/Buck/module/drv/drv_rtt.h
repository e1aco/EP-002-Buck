/*****************************************************************************
 * @文件: drv_rtt.h
 * @作者: cl
 * @日期: 2026-10-23
 * @版本: v1.0
 * @说明: SEGGER RTT 目标端驱动（上行通道 0 写字符串）
 * @依赖: vendor/segger_rtt（SEGGER 官方库）
 ****************************************************************************/
#ifndef __DRV_RTT_H
#define __DRV_RTT_H

#include <stdint.h>

void DRV_RTT_Init(void);
void DRV_RTT_Write(const char *s, uint32_t len);
/**
 * @说明: 从 RTT 下行通道 0 读取主机数据
 * @输入: s 目标缓冲；len 最多读取字节数
 * @输出: 实际读出字节数（0 = 无数据）
 * @来源 通用知识：SEGGER_RTT_Read 官方 API（module/vendor/segger_rtt）
 * @注意: 下行缓冲仅 16B（SEGGER_RTT_ConfDefaults），主机需按行短命令发送
 */
uint32_t DRV_RTT_Read(char *s, uint32_t len);

#endif /* __DRV_RTT_H */
