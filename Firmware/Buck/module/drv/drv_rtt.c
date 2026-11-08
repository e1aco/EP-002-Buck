/*****************************************************************************
 * @文件: drv_rtt.c
 * @作者: cl
 * @日期: 2026-10-23
 * @版本: v1.0
 * @说明: SEGGER RTT 目标端驱动（封装官方库，仅暴露 Init / Write）
 * @平台: STM32F103C8T6（RTT 控制块落内部 RAM，J-Link 自动扫描 "SEGGER RTT" 标识）
 * @依赖: module/vendor/segger_rtt/SEGGER_RTT.c
 ****************************************************************************/
#include <stddef.h>
#include "drv_rtt.h"
#include "SEGGER_RTT.h"

/* ==== 接口函数 ==== */

/**
 * @说明: 初始化 RTT 控制块（首次 Write 时库也会惰性初始化，这里显式调用便于上电即就绪）
 */
void DRV_RTT_Init(void)
{
  SEGGER_RTT_Init();
}

/**
 * @说明: 向 RTT 上行通道 0 写入数据
 * @输入: s 数据首地址；len 字节数
 * @注意: 缓冲满时库默认丢弃（SEGGER_RTT_MODE_DEFAULT），不阻塞、不影响控制时序
 */
void DRV_RTT_Write(const char *s, uint32_t len)
{
  if ((s == NULL) || (len == 0U))
  {
    return;
  }

  (void)SEGGER_RTT_Write(0U, s, (unsigned)len);
}

/**
 * @说明: 从 RTT 下行通道 0 读取主机数据
 * @输入: s 目标缓冲；len 最多读取字节数
 * @输出: 实际读出字节数（0 = 无数据）
 * @来源 通用知识：SEGGER_RTT_Read 官方 API（module/vendor/segger_rtt）
 * @注意: 下行缓冲仅 16B；缓冲空时立即返回，不阻塞
 */
uint32_t DRV_RTT_Read(char *s, uint32_t len)
{
  if ((s == NULL) || (len == 0U))
  {
    return 0U;
  }

  return (uint32_t)SEGGER_RTT_Read(0U, s, (unsigned)len);
}
