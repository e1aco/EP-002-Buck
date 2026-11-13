/*****************************************************************************
 * @文件: drv_tim.c
 * @作者: cl
 * @日期: 2026-11-13
 * @版本: v1.1
 * @说明: TIM1/TIM4 主从同步启动、上管中点触发与故障停机实现
 * @平台: STM32F103C8T6
 * @依赖: drv_tim.h, tim.h
 * @来源: RM0008.pdf p.334: 14.3.19 Reset mode; p.409: Table 86 TIM4 ITR0
 ****************************************************************************/
#include "drv_tim.h"

/* ==== 模块常量 ==== */
#define DRV_TIM_ADC_TRIG_OFFSET_TICKS (-4)

/* ==== 内部函数 ==== */
/**
 * @说明: 计算上管导通中点对应的 TIM4_CC4
 * @来源: 用户确认 PA8/TIM1_CH1 为上管 PWM，采样上管导通中点
 * @来源: 原理图 Buck V1.0: R1=25mR 位于 VSS_OUT→GND 输出回流，低端 shunt 电流连续；上管中点采样有效
 * @来源: memory TIM1_ARR=3599, TIM4_PSC=9 推导：上管中点=CCR1/2 TIM1 tick → /10 → TIM4 tick
 * @注: TIM4 tick=10×TIM1 tick；CCR4=(CCR1+10)/20-4，50% 时约 86
 * @注: max 预留 8 个 TIM4 tick(≈1.11us) 给 ADC 采样窗口，避免越过上管关断
 */
static int32_t drv_tim_high_side_trigger(uint16_t ccr1)
{
  uint32_t ccr4;
  int32_t trig;
  int32_t max;

  ccr4 = ((uint32_t)ccr1 + 10U) / 20U;
  trig = (int32_t)ccr4 + DRV_TIM_ADC_TRIG_OFFSET_TICKS;
  max = ((int32_t)ccr1 / 10) - 8;

  if (trig > max)
  {
    trig = max;
  }

  if (trig < 1)
  {
    trig = 1;
  }

  if (trig > (int32_t)htim4.Init.Period)
  {
    trig = (int32_t)htim4.Init.Period;
  }

  return trig;
}

/* ==== 接口函数 ==== */
/**
 * @说明: 立即停止两路 PWM 并强制关闭 TIM1 主输出
 * @注: 可从 ADC/DMA 错误中断调用；重复调用安全
 */
void DRV_TIM_Stop(void)
{
  (void)HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
  (void)HAL_TIM_PWM_Stop(&htim4, TIM_CHANNEL_4);
  __HAL_TIM_MOE_DISABLE_UNCONDITIONALLY(&htim1);
}

/**
 * @说明: 先启动 TIM4 从机(复位模式, ITR0=TIM1)，再启动 TIM1 主机 TRGO
 * @来源: RM0008.pdf p.334: 14.3.19 Reset mode
 * @来源: RM0008.pdf p.408: TIMx_SMCR TS=000 ITR0, SMS=100 Reset mode
 * @来源: RM0008.pdf p.409: Table 86 TIM4 ITR0 = TIM1
 * @来源: RM0008.pdf p.306: 14.3.3 Repetition counter (TIM1 RCR=1 -> TRGO=10kHz)
 * @来源: RM0008.pdf p.386: 15.3.8 Output compare mode (TIM4 CC4 event)
 * @注: TIM1=20kHz/50%，TIM4_CC4=10kHz；CCR4 动态对齐上管导通中点，PB9 仅作触发监视
 * @注: 任一路启动失败即调用 DRV_TIM_Stop() 停两路，不保留全局故障位
 */
HAL_StatusTypeDef DRV_TIM_StartSync(void)
{
  __HAL_TIM_ENABLE_OCxPRELOAD(&htim1, TIM_CHANNEL_1);
  __HAL_TIM_ENABLE_OCxPRELOAD(&htim4, TIM_CHANNEL_4);
  DRV_TIM_SetDuty((uint16_t)(0)); // 设置为0,开机时无PWM输出 htim1.Init.Period / 2U

  if (HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_4) != HAL_OK)
  {
    DRV_TIM_Stop();
    return HAL_ERROR;
  }

  if (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1) != HAL_OK)
  {
    DRV_TIM_Stop();
    return HAL_ERROR;
  }

  return HAL_OK;
}

/**
 * @说明: 更新 TIM1 占空比并同步计算 TIM4_CC4 上管中点触发
 * @来源: memory TIM1_ARR=3599, TIM4_PSC=9 推导：CCR4=(CCR1+10)/20-4
 * @注: 两路 CCR 均预装载，调用后在下个更新事件一起生效
 * @注: 低占空比受 max 钳位，控制环需限制最小占空比以保证 ADC 采样窗口
 */
void DRV_TIM_SetDuty(uint16_t ccr1)
{
  uint32_t period = htim1.Init.Period;
  int32_t trig;

  if ((uint32_t)ccr1 > period)
  {
    ccr1 = (uint16_t)period;
  }

  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, ccr1);
  trig = drv_tim_high_side_trigger(ccr1);
  __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, (uint32_t)trig);
}
