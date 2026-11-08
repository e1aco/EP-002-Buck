/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    tim.h
  * @brief   This file contains all the function prototypes for
  *          the tim.c file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __TIM_H__
#define __TIM_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

extern TIM_HandleTypeDef htim1;

extern TIM_HandleTypeDef htim3;

/* USER CODE BEGIN Private defines */

/* ============================================================================
 * 双定时器同频 PWM + 硬件主从触发死区（相移）
 *
 *   TIM1 (主) : PWM CH1 -> PA8 ,  TRGO = Update Event
 *   TIM3 (从) : PWM CH2 -> PA7 ,  Slave = Trigger Mode, TRGI = ITR0 (= TIM1 TRGO)
 *
 * 时基: PSC = 0, 定时器计数时钟 72 MHz
 *   TIM1 走 APB2 = 72MHz; TIM3 走 APB1 定时器时钟 = 2 x PCLK1 = 72MHz
 *   两个定时器 PSC / ARR 完全相同 => 周期严格相等、相位锁定后不漂移
 *
 * 目标波形 (N = PWM_DEADTIME_CNT = 5, ARR+1 = 3600, 20 kHz):
 *   PA8 (TIM1): 高 [0, 1800)    占空比 50%
 *   PA7 (TIM3): 高 [5, 1795)    比 PA8 晚 N 开通、早 N 关断
 *               ^       ^
 *   两个上升沿相差 N，两个下降沿也相差 N => PA7 脉冲"内缩"在 PA8 脉冲里
 * ==========================================================================*/
#define PWM_TIMER_CLK_HZ     72000000UL     /* 定时器计数时钟 (Hz) */
#define PWM_PRESCALER        0UL            /* PSC = 0, 1 个计数 = 1/72MHz = 13.89 ns */
#define PWM_FREQ_HZ          20000UL        /* 目标 PWM 频率 (Hz) */

/* ARR = 计数时钟 / ((PSC+1) * 频率) - 1  => 3599 -> 20 kHz */
#define PWM_ARR_VALUE        (PWM_TIMER_CLK_HZ / ((PWM_PRESCALER + 1UL) * PWM_FREQ_HZ) - 1UL)

#define PWM_DUTY_PERMILLE    500UL          /* TIM1(PA8) 占空比, 千分比: 500 = 50% */
/* TIM1 的 CCR = (ARR+1) * 占空比 => 3600*50% = 1800 */
#define PWM_PULSE            (((PWM_ARR_VALUE) + 1UL) * PWM_DUTY_PERMILLE / 1000UL)

/* 死区 = 从定时器边沿相对主定时器"滞后/提前"的计数个数 N
 *   1 个计数 = 13.89 ns; 5 个计数 ≈ 69 ns
 *   上升沿: PA7 比 PA8 晚 N 个计数
 *   下降沿: PA7 比 PA8 早 N 个计数 */
#define PWM_DEADTIME_CNT     5UL

/* TIM3 的 CCR: 要比 TIM1 早 N 个计数关断, 所以在 TIM1 的 CCR 上再减 2N
 *   (TIM3 整体已滞后 N, 要让它早 N 关断就得再减 N, 合计减 2N)
 *   => PA7 高电平区间 = [N, CCR1 - N) = [5, 1795) */
#define PWM_PULSE_T3         ((PWM_PULSE) - 2UL * (PWM_DEADTIME_CNT))

/* TIM3 在触发模式下由 TIM1 的 TRGO 硬件启动, 启动时从这个预置值开始计数。
 * 因为 CNT > CCR, PWM 模式 1 的输出此时为低; 等计数器回卷到 0 才拉高。
 *   上升沿滞后 = (ARR + 1 - TIM3_CNT_PRESET) = (3599 + 1 - 3595) = 5 计数
 * 约束: 必须满足 CCR < TIM3_CNT_PRESET <= ARR, 否则输出不会从低电平起步。 */
#define TIM3_CNT_PRESET      ((PWM_ARR_VALUE) + 1UL - PWM_DEADTIME_CNT)

#if ((PWM_DEADTIME_CNT) < 1UL) || ((PWM_DEADTIME_CNT) > ((PWM_ARR_VALUE) + 1UL))
#error "PWM_DEADTIME_CNT must satisfy 1 <= N <= ARR+1"
#endif

#if ((PWM_PULSE) <= 2UL * (PWM_DEADTIME_CNT))
#error "PWM_PULSE too small: need CCR1 > 2N so that PWM_PULSE_T3 stays positive"
#endif

#if (PWM_PULSE_T3) >= (TIM3_CNT_PRESET)
#error "PWM_PULSE_T3 must be < TIM3_CNT_PRESET"
#endif

/* USER CODE END Private defines */

void MX_TIM1_Init(void);
void MX_TIM3_Init(void);

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* USER CODE BEGIN Prototypes */

void MX_PWM_Start_PhaseLocked(void);

/* 调试用: 关键寄存器快照, 在 Keil 的 Watch 窗口直接观察 */
extern volatile uint32_t g_dbg_tim1_cr1;
extern volatile uint32_t g_dbg_tim1_cr2;
extern volatile uint32_t g_dbg_tim1_cnt;
extern volatile uint32_t g_dbg_tim3_cr1;
extern volatile uint32_t g_dbg_tim3_smcr;
extern volatile uint32_t g_dbg_tim3_cnt;
extern volatile uint32_t g_dbg_tim3_sr;

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __TIM_H__ */

