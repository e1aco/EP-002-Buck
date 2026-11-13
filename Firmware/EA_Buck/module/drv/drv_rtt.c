/*****************************************************************************
 * @文件: drv_rtt.c
 * @作者: cl
 * @日期: 2026-11-13
 * @版本: v1.0
 * @说明: SEGGER RTT 上行通道驱动实现（缓冲满即丢弃，不阻塞控制环）
 * @平台: STM32F103C8T6
 * @依赖: drv_rtt.h, SEGGER_RTT.h
 ****************************************************************************/
#include "drv_rtt.h"
#include "SEGGER_RTT.h"

/* ==== 模块常量 ==== */
/* 遥测走 RTT 上行通道 0（J-Link RTTClient 默认读取的通道）
 * @来源 SEGGER_RTT.h p.405: SEGGER_RTT_Write(BufferIndex, pBuffer, NumBytes) */
#define DRV_RTT_CHANNEL 0U

/* ==== 接口函数 ==== */
/**
 * @说明: 初始化 RTT 控制块，供主机端识别并挂接
 * @输出: 无
 * @注意: 不调用也可用（首次 API 调用会自动初始化），显式初始化便于上电即挂接
 * @来源 SEGGER_RTT.c:1858: void SEGGER_RTT_Init(void)
 */
void DRV_RTT_Init(void)
{
  SEGGER_RTT_Init();
}

/**
 * @说明: 向上行通道写入一段文本
 * @输入: text 文本首地址；len 字节数
 * @输出: 实际写入的字节数，0 表示缓冲满被丢弃
 * @注意: 模式为 SEGGER_RTT_MODE_NO_BLOCK_SKIP（SEGGER_RTT_ConfDefaults.h），
 *        缓冲不足时直接丢弃，不会阻塞调用者
 */
uint16_t DRV_RTT_Write(const char *text, uint16_t len)
{
  return (uint16_t)SEGGER_RTT_Write(DRV_RTT_CHANNEL, text, (unsigned)len);
}
