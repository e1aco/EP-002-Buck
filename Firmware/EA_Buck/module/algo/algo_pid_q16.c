/*****************************************************************************
 * @文件: algo_pid_q16.c
 * @作者: cl
 * @日期: 2026-11-14
 * @版本: v1.0
 * @说明: 定点 PID 控制器实现（梯形积分 + 双端限幅，零浮点运算）
 * @平台: STM32F103C8T6
 * @依赖: algo_pid_q16.h
 * @来源 本设计：Q16 状态精度 1/65536，输出量化误差 < 1 码
 ****************************************************************************/
#include "algo_pid_q16.h"

/* ==== 模块常量 ==== */
/* 积分系数 Q20 定标：delta_q16 = (ki_q * (e+e_last) * dt_us) >> 20
 * 推导：delta_real = ki*(e+e_last)/2*dt_us*1e-6；delta_q16 = delta_real*2^16
 *       => ki_q = ki * 1e-6/2 * 2^16 * 2^20 = ki * 0.032768 * 2^20 */
#define ALGO_PID_Q16_KI_SHIFT   20

/* 比例系数 Q16 定标 */
#define ALGO_PID_Q16_KP_SHIFT   16

/* 单拍最大间隔 1000us：主循环/中断阻塞时不让积分一次吃进过长时间
 * @来源 memory APP_CONTROL_DT_MAX_US=1000 推导：10×控制周期 */
#define ALGO_PID_Q16_DT_MAX_US  1000U

/* ==== 接口函数 ==== */
/**
 * @说明: 初始化定点 PID
 * @输入: pid 句柄；kp_q 比例系数 Q16；ki_q 积分系数 Q20；out_min/out_max 输出限幅
 * @输出: 无
 */
void ALGO_PID_Q16_Init(algo_pid_q16_t *pid, int32_t kp_q, int32_t ki_q,
                       int32_t out_min, int32_t out_max)
{
  pid->kp_q = kp_q;
  pid->ki_q = ki_q;
  pid->setpoint = 0;
  pid->out_min = out_min;
  pid->out_max = out_max;
  pid->i_term_q = 0;
  pid->last_error = 0;
  pid->last_us = 0U;
  pid->started = 0U;
}

/**
 * @说明: 复位控制器状态，积分项归零
 * @输入: pid 句柄
 * @输出: 无
 */
void ALGO_PID_Q16_Reset(algo_pid_q16_t *pid)
{
  pid->i_term_q = 0;
  pid->last_error = 0;
  pid->last_us = 0U;
  pid->started = 0U;
}

/**
 * @说明: 执行一次定点 PID 运算（位置式 + 梯形积分）
 * @输入: pid 句柄；input 反馈值（输入单位）；now_us 当前时间戳（us）
 * @输出: 控制器输出（输出单位）
 * @注意: now_us 必须单调递增；首拍只输出 P 项，跳过积分
 * @注意: 积分项与总输出各自限幅，与 float 版 algo_pid 行为一致
 */
int32_t ALGO_PID_Q16_Compute(algo_pid_q16_t *pid, int32_t input, uint32_t now_us)
{
  int32_t error;
  int32_t output;
  uint32_t dt_us;
  int64_t num;
  int32_t delta;
  int32_t limit_min_q;
  int32_t limit_max_q;

  error = pid->setpoint - input;
  output = (int32_t)(((int64_t)pid->kp_q * (int64_t)error) >> ALGO_PID_Q16_KP_SHIFT);

  if (pid->started != 0U)
  {
    dt_us = now_us - pid->last_us;
    if (dt_us > ALGO_PID_Q16_DT_MAX_US)
    {
      dt_us = ALGO_PID_Q16_DT_MAX_US;
    }

    num = (int64_t)pid->ki_q * (int64_t)(error + pid->last_error);
    delta = (int32_t)((num * (int64_t)dt_us) >> ALGO_PID_Q16_KI_SHIFT);

    pid->i_term_q += delta;

    limit_min_q = pid->out_min << ALGO_PID_Q16_KP_SHIFT;
    limit_max_q = pid->out_max << ALGO_PID_Q16_KP_SHIFT;
    if (pid->i_term_q < limit_min_q)
    {
      pid->i_term_q = limit_min_q;
    }
    else if (pid->i_term_q > limit_max_q)
    {
      pid->i_term_q = limit_max_q;
    }
    else
    {
      /* 未越限，保持 */
    }

    output += pid->i_term_q >> ALGO_PID_Q16_KP_SHIFT;
  }

  if (output < pid->out_min)
  {
    output = pid->out_min;
  }
  else if (output > pid->out_max)
  {
    output = pid->out_max;
  }
  else
  {
    /* 未越限，保持 */
  }

  pid->last_error = error;
  pid->last_us = now_us;
  pid->started = 1U;

  return output;
}
