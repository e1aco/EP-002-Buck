/*****************************************************************************
 * @文件: algo_pid.h
 * @作者: cl
 * @日期: 2026-11-13
 * @版本: v1.0
 * @说明: PID 控制器接口
 * @平台: STM32F103C8T6
 * @依赖: stdint.h, algo_lpf.h
 ****************************************************************************/
#ifndef ALGO_PID_H
#define ALGO_PID_H

#include <stdint.h>
#include "algo_lpf.h"

typedef struct
{
  float kp;                 /* 比例系数 */
  float ki;                 /* 积分系数 */
  float kd;                 /* 微分系数 */
  float setpoint;           /* 参考值 */
  float output_upper_limit; /* 输出上限 */
  float output_lower_limit; /* 输出下限 */
  float default_output;     /* 默认输出，同时作为积分项初值 */
} algo_pid_init_t;

typedef struct
{
  algo_pid_init_t init;      /* 初始化参数副本 */
  uint8_t cmd;              /* 0 - 禁止，1 - 使能 */
  uint64_t last_time_us;    /* 上次计算时间，单位 us */
  float i_term;             /* 积分项 */
  float d_term;             /* 微分项 */
  float last_input;         /* 上次输入 */
  float last_error;         /* 上次误差 */
  float kp;                 /* 当前比例系数 */
  float ki;                 /* 当前积分系数 */
  float kd;                 /* 当前微分系数 */
  float output_upper_limit; /* 当前输出上限 */
  float output_lower_limit; /* 当前输出下限 */
  float setpoint;           /* 当前参考值 */
  algo_lpf_t lpf;            /* 输出端低通滤波器 */
  uint8_t lpf_cmd;          /* 0 - 禁止，1 - 使能 */
} algo_pid_t;

void ALGO_PID_Init(algo_pid_t *pid, const algo_pid_init_t *init);
void ALGO_PID_ConfigLpf(algo_pid_t *pid, float tf, uint8_t new_state);
void ALGO_PID_Enable(algo_pid_t *pid, uint8_t new_state);
void ALGO_PID_Reset(algo_pid_t *pid);
float ALGO_PID_Compute1(algo_pid_t *pid, float input, uint64_t now_us);
float ALGO_PID_Compute2(algo_pid_t *pid, float input, float d_input_dt, uint64_t now_us);
void ALGO_PID_SetTunings(algo_pid_t *pid, float new_kp, float new_ki, float new_kd);
void ALGO_PID_SetSetpoint(algo_pid_t *pid, float new_setpoint);
float ALGO_PID_GetSetpoint(const algo_pid_t *pid);
void ALGO_PID_GetTunings(const algo_pid_t *pid, float *kp_out, float *ki_out, float *kd_out);

#endif /* ALGO_PID_H */
