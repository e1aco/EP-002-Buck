/*****************************************************************************
 * @文件: algo_pid_q16.h
 * @作者: cl
 * @日期: 2026-11-14
 * @版本: v1.0
 * @说明: 定点 PID 控制器接口（Q16 状态 / Q20 积分系数），零浮点
 * @平台: STM32F103C8T6
 * @依赖: stdint.h
 ****************************************************************************/
#ifndef ALGO_PID_Q16_H
#define ALGO_PID_Q16_H

#include <stdint.h>

typedef struct
{
  int32_t kp_q;      /* 比例系数，Q16 */
  int32_t ki_q;      /* 积分系数，Q20（已含 1e-6/2 因子） */
  int32_t setpoint;  /* 参考值，输入单位 */
  int32_t out_min;   /* 输出下限，输出单位 */
  int32_t out_max;   /* 输出上限，输出单位 */
  int32_t i_term_q;  /* 积分项，Q16 */
  int32_t last_error;
  uint32_t last_us;
  uint8_t started;   /* 0=首拍（跳过积分），1=已启动 */
} algo_pid_q16_t;

void ALGO_PID_Q16_Init(algo_pid_q16_t *pid, int32_t kp_q, int32_t ki_q,
                       int32_t out_min, int32_t out_max);
void ALGO_PID_Q16_Reset(algo_pid_q16_t *pid);
int32_t ALGO_PID_Q16_Compute(algo_pid_q16_t *pid, int32_t input, uint32_t now_us);

#endif /* ALGO_PID_Q16_H */
