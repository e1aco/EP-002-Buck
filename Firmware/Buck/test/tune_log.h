/*****************************************************************************
 * @文件: tune_log.h
 * @作者: cl
 * @日期: 2026-11-06
 * @版本: v1.0
 * @说明: 调参采样（50ms CSV 上行 + 自动 5V/12V 阶跃序列），仅供 /cl pid 调参
 * @平台: STM32F103C8T6
 * @注意: 开关 = module/config.h 的 CL_TUNE_LOG（0 = 不编入，生产版保持 0）
 ****************************************************************************/
#ifndef __TUNE_LOG_H
#define __TUNE_LOG_H

#include "config.h"

/* 关闭时函数不声明 + 实现整段编译掉；main 里忘开宏就取消注释 → 编译报错（响亮失败） */
#if CL_TUNE_LOG
void TUNE_LOG_Init(void);
void TUNE_LOG_Loop(void);
#endif /* CL_TUNE_LOG */

#endif /* __TUNE_LOG_H */
