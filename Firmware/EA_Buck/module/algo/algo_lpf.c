/*****************************************************************************
 * @文件: algo_lpf.c
 * @作者: cl
 * @日期: 2026-11-13
 * @版本: v1.0
 * @说明: 一阶低通滤波器实现
 * @平台: STM32F103C8T6
 * @依赖: algo_lpf.h
 ****************************************************************************/
#include "algo_lpf.h"

/* ==== 模块常量 ==== */
#define ALGO_LPF_TIME_INVALID UINT64_MAX
#define ALGO_LPF_US_TO_S      1.0e-6f

/*
 * 一阶低通传递函数：H(s) = 1 / (Tf*s + 1)
 * 离散化：y(k) = y(k-1) + (x(k) - y(k-1)) * dt / Tf
 */

/* ==== 接口函数 ==== */
/**
 * @说明: 初始化一阶低通滤波器
 * @输入: lpf - 滤波器句柄；tf - 时间常数，单位 s
 * @输出: 无
 * @注意: tf 必须大于 0；首次计算直接输出输入值
 */
void ALGO_LPF_Init(algo_lpf_t *lpf, float tf)
{
  lpf->tf = tf;
  lpf->last_output = 0.0f;
  lpf->last_time_us = ALGO_LPF_TIME_INVALID;
}

/**
 * @说明: 计算一次一阶低通滤波输出
 * @输入: lpf - 滤波器句柄；input - 输入；now_us - 当前时间，单位 us
 * @输出: 滤波后的输出
 * @注意: now_us 必须单调递增；tf <= 0 时退化为直通，避免除零
 */
float ALGO_LPF_Calc(algo_lpf_t *lpf, float input, uint64_t now_us)
{
  float dt_s;
  float output;

  if (lpf->last_time_us == ALGO_LPF_TIME_INVALID)
  {
    output = input;
  }
  else if (lpf->tf <= 0.0f)
  {
    output = input;
  }
  else
  {
    dt_s = (float)(now_us - lpf->last_time_us) * ALGO_LPF_US_TO_S;
    output = lpf->last_output + (input - lpf->last_output) * dt_s / lpf->tf;
  }

  lpf->last_time_us = now_us;
  lpf->last_output = output;

  return output;
}
