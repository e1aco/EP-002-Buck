/*****************************************************************************
 * @文件: algo_lpf.h
 * @作者: cl
 * @日期: 2026-11-13
 * @版本: v1.0
 * @说明: 一阶低通滤波器接口
 * @平台: STM32F103C8T6
 * @依赖: stdint.h
 ****************************************************************************/
#ifndef ALGO_LPF_H
#define ALGO_LPF_H

#include <stdint.h>

typedef struct
{
  float tf;              /* 时间常数，单位 s */
  float last_output;     /* 上次滤波输出 */
  uint64_t last_time_us; /* 上次计算时间，单位 us */
} algo_lpf_t;

void ALGO_LPF_Init(algo_lpf_t *lpf, float tf);
float ALGO_LPF_Calc(algo_lpf_t *lpf, float input, uint64_t now_us);

#endif /* ALGO_LPF_H */
