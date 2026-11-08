/*****************************************************************************
 * @文件: app_control.h
 * @作者: cl
 * @日期: 2026-10-30
 * @版本: v1.0
 * @说明: 闭环控制接口（PID 内核可复用；单环电压 / 双环电压+电流 由 CL_CTRL_MODE 切换）
 * @平台: STM32F103C8T6
 ****************************************************************************/
#ifndef __APP_CONTROL_H
#define __APP_CONTROL_H

#include <stdint.h>

/* PID 内核：单环电压与双环（外环电压 / 内环电流）共用同一套结构与步进函数
 * 输出单位由调用方定义（电压环=CCR1 计数，电流环=CCR1 计数） */
typedef struct
{
  float kp;         /* 比例增益 [输出单位/误差单位] */
  float ki;         /* 积分增益 [输出单位/(误差单位·步)] */
  float out_min;    /* 输出下限（绝对限幅，安全边界靠配置前置） */
  float out_max;    /* 输出上限 */
  float i_acc;      /* 积分累加器 */
  float out;        /* 上一步输出（含前馈） */
  uint8_t saturated;/* 上一步是否被限幅（供上层判读） */
} app_pid_t;

/**
 * @说明: 初始化 PID 内核
 * @输入: kp/ki 增益，omin/omax 输出绝对限幅
 */
void APP_PID_Init(app_pid_t *pid, float kp, float ki, float omin, float omax);

/**
 * @说明: 单步 PID（位置式，带前馈与抗积分饱和）
 * @输入: err 误差（参考−反馈，单位同调用方）、ff 前馈量（单位同输出）
 * @输出: 限幅后的输出
 * @注意: 输出饱和时若积分方向仍在加深饱和，则冻结本步积分（clamping 抗饱和）
 */
float APP_PID_Step(app_pid_t *pid, float err, float ff);

void APP_CONTROL_Init(void);
/**
 * @说明: 控制步进（PID + 软启动 + OVP + 下发 CCR1）
 * @注意: ISR 上下文，2kHz——只由 Core/Src/stm32f1xx_it.c 的
 *        HAL_ADC_ConvCpltCallback 在快照更新之后调用，勿在别处调
 */
void APP_CONTROL_Step(void);
/** @说明: 主循环调用——只做失联 / 无输入保护（控制步进在 ADC ISR 里跑） */
void APP_CONTROL_Loop(void);
void APP_CONTROL_SetVref(uint32_t vref_mv);
/** @说明: 5V ↔ 12V 档位切换（按键 Key2）；开/关机均改 SET，开机按 SET 出压 */
void APP_CONTROL_CycleVref(void);
/** @说明: 输出开关（按键 Key1）：on=1 软启动到当前档位，on=0 停波 */
void APP_CONTROL_SetOutput(uint8_t on);
/** @说明: 输出开关翻转（按键 Key1 按一次反转一次） */
void APP_CONTROL_OutputToggle(void);
uint8_t APP_CONTROL_GetOutputEnabled(void);
/** @说明: 1 = 无输出重试用尽后自动关机（屏显 NO LOAD），按键开启可清 */
uint8_t APP_CONTROL_GetNoLoad(void);
/** @说明: 当前用户档位目标（mV），上电默认 5000 */
uint32_t APP_CONTROL_GetVrefTargetMv(void);
/** @说明: 当前软启动斜坡参考（mV），调参回传用 */
uint32_t APP_CONTROL_GetVrefMv(void);
/** @说明: 最近一次输出电流（mA），调参回传用 */
uint16_t APP_CONTROL_GetIoutMa(void);
/** @说明: 内环电流参考（mA，外环输出），调参回传用 */
uint16_t APP_CONTROL_GetIrefMa(void);
/** @说明: 在线改增益（调参用；内部限幅，单环模式忽略内环参数） */
void APP_CONTROL_SetGains(float kp_v, float ki_v, float kp_i, float ki_i);
/** @说明: 读当前增益（调参回显用） */
void APP_CONTROL_GetGains(float *kp_v, float *ki_v, float *kp_i, float *ki_i);
/** @说明: 在线设置启动提速参数（启动段外环 KI 倍率 + 升压门控滞后 mV） */
void APP_CONTROL_SetBootTune(float ki_mult, uint32_t lag_mv);
/** @说明: 读启动提速参数（调参回显用） */
void APP_CONTROL_GetBootTune(float *ki_mult, uint32_t *lag_mv);
uint32_t APP_CONTROL_GetVoutMv(void);
uint16_t APP_CONTROL_GetCcr1(void);
uint8_t APP_CONTROL_GetOvp(void);

#endif /* __APP_CONTROL_H */
