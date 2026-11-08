/*****************************************************************************
 * @文件: drv_adc.h
 * @作者: cl
 * @日期: 2026-10-28
 * @版本: v1.1
 * @说明: ADC1 双通道采样驱动（TIM4_CC4 触发 + DMA1_Ch1 循环，2kHz，相位锁定 PWM）
 * @依赖: Core 的 adc.c / tim.c
 ****************************************************************************/
#ifndef __DRV_ADC_H
#define __DRV_ADC_H

#include <stdint.h>

/* DMA 缓冲内通道顺序 = ADC 规则组 Rank 顺序（Rank1=IN1 电流，Rank2=IN0 电压）
 * @来源 memory ADC_CH1=IN1/PA1 输出电流、ADC_CH0=IN0/PA0 输出电压（Buck.ioc 规则组）
 *        电流排 Rank1：触发后立即采样，避开电压通道 2.167µs 的转换延迟 */
#define DRV_ADC_CH_I           0U
#define DRV_ADC_CH_V           1U
#define DRV_ADC_SCAN_LEN       2U

/* 触发相位：TIM4 由 TIM1_TRGO(2kHz) 复位清零 → CNT 零点 = PA8 上升沿
 * CCR4 = 导通中点换算到 TIM4 计数（TIM1 1 计数 = 10 个 TIM4 计数）再前移补偿
 * @来源 memory TIM4_PSC=9/TIM1_PSC=0 推导：CCR4 = CCR1/20 - 9
 *        9 计数 = 1.25µs，抵扣触发->采样保持延迟（经验值，待 LA 实测复核） */
#define DRV_ADC_CCR4_MIN       18U     /* 2.5µs：占空比很小时的下限 */
#define DRV_ADC_CCR4_COMP      9U      /* 1.25µs 触发链延迟补偿 */

void DRV_ADC_Init(void);
/**
 * @说明: 按当前 TIM1 占空比重算 ADC 触发点（导通中点），写 TIM4 CCR4
 * @注意: 已由 DRV_PWM_SetDutyCount 与 DRV_ADC_Init 自动调用，上层无需手动配对；
 *        仅在绕过 DRV_PWM 直接改 CCR1 时才需自行调用
 * @来源 memory TIM1_PSC=0/TIM4_PSC=9 推导：TIM1 计数 13.889ns、TIM4 计数 138.89ns
 *        -> CCR4 = CCR1/2 × (1/10) - 补偿 = CCR1/20 - 9（下限 18）
 */
void DRV_ADC_TrigFollowPwm(void);
void DRV_ADC_GetRaw(uint16_t *raw_v, uint16_t *raw_i);
uint32_t DRV_ADC_GetScanCount(void);

/**
 * @说明: ADC 扫描完成的中断实体（双通道快照 + 扫描计数）
 * @注意: 只由 Core/Src/stm32f1xx_it.c 的 HAL_ADC_ConvCpltCallback 调用；
 *        ISR 上下文，内部不得打印/阻塞
 */
void DRV_ADC_ConvCpltIsr(void);

#endif /* __DRV_ADC_H */
