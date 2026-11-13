/*****************************************************************************
 * @文件: config.h
 * @作者: cl
 * @日期: 2026-11-13
 * @版本: v1.0
 * @说明: 编译期功能开关
 * @平台: STM32F103C8T6
 ****************************************************************************/
#ifndef MODULE_CONFIG_H
#define MODULE_CONFIG_H

/* 时序测量宏版开关：0=生产版，1=宏版
 * @来源 memory CTRL_LOOP_HZ=10000 推导：DMA ISR 10kHz */
#ifndef CL_TIMING_MEASURE
#define CL_TIMING_MEASURE 0
#endif /* CL_TIMING_MEASURE */

#endif /* MODULE_CONFIG_H */
