/*****************************************************************************
 * @文件: algo_pid.c
 * @作者: cl
 * @日期: 2026-11-13
 * @版本: v1.0
 * @说明: PID 控制器实现
 * @平台: STM32F103C8T6
 * @依赖: algo_pid.h
 ****************************************************************************/
#include "algo_pid.h"

/* ==== 模块常量 ==== */
#define ALGO_PID_TIME_INVALID UINT64_MAX
#define ALGO_PID_US_TO_S      1.0e-6f

/* ==== 内部函数 ==== */
static float algo_pid_limit(float value, float lower, float upper)
{
  if (value > upper)
  {
    return upper;
  }

  if (value < lower)
  {
    return lower;
  }

  return value;
}

static float algo_pid_dt(uint64_t now_us, uint64_t last_time_us)
{
  return (float)(now_us - last_time_us) * ALGO_PID_US_TO_S;
}

/* ==== 接口函数 ==== */
/**
 * @说明: 初始化 PID 控制器
 * @输入: pid - PID 句柄；init - 初始化参数
 * @输出: 无
 * @注意: init 不得为空；default_output 作为积分项初值
 */
void ALGO_PID_Init(algo_pid_t *pid, const algo_pid_init_t *init)
{
  pid->init = *init;
  pid->setpoint = init->setpoint;
  pid->kp = init->kp;
  pid->ki = init->ki;
  pid->kd = init->kd;
  pid->output_lower_limit = init->output_lower_limit;
  pid->output_upper_limit = init->output_upper_limit;
  pid->i_term = init->default_output;
  pid->d_term = 0.0f;
  pid->last_time_us = ALGO_PID_TIME_INVALID;
  pid->last_input = 0.0f;
  pid->last_error = 0.0f;
  pid->cmd = 0U;
  pid->lpf_cmd = 0U;
  ALGO_LPF_Init(&pid->lpf, 0.0f);
}

/**
 * @说明: 配置 PID 输出端低通滤波器
 * @输入: pid - PID 句柄；tf - 时间常数，单位 s；new_state - 0 禁止，非 0 使能
 * @输出: 无
 * @注意: tf <= 0 时滤波器退化为直通
 */
void ALGO_PID_ConfigLpf(algo_pid_t *pid, float tf, uint8_t new_state)
{
  pid->lpf_cmd = new_state;
  ALGO_LPF_Init(&pid->lpf, tf);
}

/**
 * @说明: 复位 PID 控制器
 * @输入: pid - PID 句柄
 * @输出: 无
 * @注意: 强制积分项等于默认输出，并清空历史误差
 */
void ALGO_PID_Reset(algo_pid_t *pid)
{
  pid->last_time_us = ALGO_PID_TIME_INVALID;
  pid->i_term = pid->init.default_output;
  pid->last_error = 0.0f;
  pid->last_input = 0.0f;
}

/**
 * @说明: 执行一次 PID 运算，由误差计算微分项
 * @输入: pid - PID 句柄；input - 传感器输入；now_us - 当前时间，单位 us
 * @输出: 控制器输出
 * @注意: now_us 必须单调递增；首次计算跳过积分和微分
 */
float ALGO_PID_Compute1(algo_pid_t *pid, float input, uint64_t now_us)
{
  float error = pid->setpoint - input;
  float dt_s = 0.0f;
  float output = pid->kp * error;

  if (pid->last_time_us != ALGO_PID_TIME_INVALID)
  {
    dt_s = algo_pid_dt(now_us, pid->last_time_us);

    if ((pid->kd != 0.0f) && (dt_s > 0.0f))
    {
      pid->d_term = pid->kd * (error - pid->last_error) / dt_s;
      output += pid->d_term;
    }

    if (pid->ki != 0.0f)
    {
      pid->i_term += pid->ki * (error + pid->last_error) * 0.5f * dt_s;
      pid->i_term = algo_pid_limit(pid->i_term, pid->output_lower_limit, pid->output_upper_limit);
      output += pid->i_term;
    }
  }

  output = algo_pid_limit(output, pid->output_lower_limit, pid->output_upper_limit);

  pid->last_input = input;
  pid->last_time_us = now_us;
  pid->last_error = error;

  if (pid->lpf_cmd != 0U)
  {
    output = ALGO_LPF_Calc(&pid->lpf, output, now_us);
  }

  return output;
}

/**
 * @说明: 执行一次 PID 运算，由输入变化率计算微分项
 * @输入: pid - PID 句柄；input - 传感器输入；d_input_dt - 输入变化率；
 *        now_us - 当前时间，单位 us
 * @输出: 控制器输出
 * @注意: now_us 必须单调递增；首次计算跳过积分和微分
 */
float ALGO_PID_Compute2(algo_pid_t *pid, float input, float d_input_dt, uint64_t now_us)
{
  float error = pid->setpoint - input;
  float dt_s = 0.0f;
  float output = pid->kp * error;

  if (pid->last_time_us != ALGO_PID_TIME_INVALID)
  {
    dt_s = algo_pid_dt(now_us, pid->last_time_us);

    if (pid->kd != 0.0f)
    {
      pid->d_term = -pid->kd * d_input_dt;
      output += pid->d_term;
    }

    if (pid->ki != 0.0f)
    {
      pid->i_term += pid->ki * (error + pid->last_error) * 0.5f * dt_s;
      pid->i_term = algo_pid_limit(pid->i_term, pid->output_lower_limit, pid->output_upper_limit);
      output += pid->i_term;
    }
  }

  output = algo_pid_limit(output, pid->output_lower_limit, pid->output_upper_limit);

  pid->last_input = input;
  pid->last_time_us = now_us;
  pid->last_error = error;

  if (pid->lpf_cmd != 0U)
  {
    output = ALGO_LPF_Calc(&pid->lpf, output, now_us);
  }

  return output;
}

/**
 * @说明: 设置 PID 参数
 * @输入: pid - PID 句柄；new_kp - 比例系数；new_ki - 积分系数；new_kd - 微分系数
 * @输出: 无
 */
void ALGO_PID_SetTunings(algo_pid_t *pid, float new_kp, float new_ki, float new_kd)
{
  pid->kp = new_kp;
  pid->ki = new_ki;
  pid->kd = new_kd;
}

/**
 * @说明: 设置 PID 参考值
 * @输入: pid - PID 句柄；new_setpoint - 新参考值
 * @输出: 无
 */
void ALGO_PID_SetSetpoint(algo_pid_t *pid, float new_setpoint)
{
  pid->setpoint = new_setpoint;
}

/**
 * @说明: 获取 PID 参考值
 * @输入: pid - PID 句柄
 * @输出: 当前参考值
 */
float ALGO_PID_GetSetpoint(const algo_pid_t *pid)
{
  return pid->setpoint;
}

/**
 * @说明: 获取当前 PID 参数
 * @输入: pid - PID 句柄
 * @输出: kp_out - 比例系数；ki_out - 积分系数；kd_out - 微分系数
 */
void ALGO_PID_GetTunings(const algo_pid_t *pid, float *kp_out, float *ki_out, float *kd_out)
{
  *kp_out = pid->kp;
  *ki_out = pid->ki;
  *kd_out = pid->kd;
}

/**
 * @说明: 使能或禁止 PID 控制器
 * @输入: pid - PID 句柄；new_state - 0 禁止，1 使能
 * @输出: 无
 * @注意: 禁止后仅置 cmd=0，输出保持最后一次计算值；手动输出接口未实现
 */
void ALGO_PID_Enable(algo_pid_t *pid, uint8_t new_state)
{
  if ((pid->cmd == 0U) && (new_state == 1U))
  {
    pid->i_term = pid->init.default_output;
    pid->i_term = algo_pid_limit(pid->i_term, pid->output_lower_limit, pid->output_upper_limit);
  }

  pid->cmd = new_state;
}
