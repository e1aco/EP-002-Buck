/*****************************************************************************
 * @文件: drv_adc.c
 * @作者: cl
 * @日期: 2026-10-28
 * @版本: v1.1
 * @说明: ADC1 双通道采样驱动：TIM4_CC4 触发规则组扫描，DMA 循环搬运到内存
 * @平台: STM32F103C8T6（ADC1 IN0/IN1 + DMA1_Ch1）
 * @依赖: Core 的 adc.c（hadc1/hdma_adc1）、tim.c（htim1/htim4）
 * @注意: 采样链增益来自 .cl/memory/adc.md；本层只出原始码值，物理量换算在上层
 ****************************************************************************/
#include <stddef.h>
#include "drv_adc.h"
#include "drv_pwm.h"
#include "adc.h"
#include "tim.h"

/* DMA 目标缓冲：循环模式下硬件持续写入，长度 = 规则组通道数 2
 * 依据 .cl/memory/adc.md ADC_SCAN=4.333µs（2ch @12MHz） */
static uint16_t s_dma_buf[DRV_ADC_SCAN_LEN];

/* 快照：ISR 写完 s_seq 变偶数，主循环读到奇数/前后不一致即重读（免临界区的一致读） */
static volatile uint16_t s_latest[DRV_ADC_SCAN_LEN];
static volatile uint32_t s_seq;
static volatile uint32_t s_scan_count;

/* ==== 接口函数 ==== */

/**
 * @说明: 启动采样链：ADC 自校准 -> 武装 DMA -> 设定触发相位 -> 启动 TIM4
 * @注意: 必须先武装 ADC+DMA 再开 TIM4，否则首个触发到来时 DMA 未就绪会置 OVR
 * @来源 memory ADC_TRIG=TIM4_CC4@2kHz、ADC_DMA=DMA1_Ch1 circular 推导：
 *        每个 CC4 事件触发一次 2 通道扫描，DMA 半字写入 s_dma_buf
 */
void DRV_ADC_Init(void)
{
  /* ADC 自校准：F1 系列上电后必须执行，否则零点/增益有固定偏差 */
  if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)s_dma_buf, DRV_ADC_SCAN_LEN) != HAL_OK)
  {
    Error_Handler();
  }

  /* 关掉 DMA 半传输中断：HAL_ADC_Start_DMA -> HAL_DMA_Start_IT 默认开 TC|HT|TE，
   * 而缓冲长度=一次扫描(2 通道)，HT 只表示搬完第 1 个通道（半次扫描，数据不可用），
   * 只会空跑一次 HAL_ADC_ConvHalfCpltCallback；这里显式关 HT，只留 TC(完整扫描)/TE
   * @来源 memory ADC_SCAN=4.333µs（2ch）推导：HT 落在扫描中点 -> 每 2kHz 多进一次空中断 */
  __HAL_DMA_DISABLE_IT(hadc1.DMA_Handle, DMA_IT_HT);

  /* CCR4 预装载必须打开：CCR1 是预装载、只在 TIM1 UEV 提交，而 TIM4 的 UEV 正由 TRGO 复位产生、
   * 与 TIM1 UEV 同刻 → CCR4 与 CCR1 一起提交，改占空比时采样相位不会落后一拍
   * @来源 联网-待复核 RM0008 p.371 CCMR2.OC4PE：1=使能预装载（CCR4 于 UEV 装入影子寄存器）；
   *        TIM4 从模式复位同样产生 UEV */
  __HAL_TIM_ENABLE_OCxPRELOAD(&htim4, TIM_CHANNEL_4);

  /* TIM4 CH4 同时输出到 PB9（AF_PP）供逻辑分析仪观测触发相位 */
  DRV_ADC_TrigFollowPwm();

  if (HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
 * @说明: 按当前 TIM1 占空比重算 ADC 触发点（上管导通中点），写 TIM4 CCR4
 * @注意: 已由 DRV_PWM_SetDutyCount 与 DRV_ADC_Init 自动调用，上层无需手动配对；
 *        仅在绕过 DRV_PWM 直接改 CCR1 时才需自行调用
 * @来源 memory TIM1_PSC=0/TIM4_PSC=9 推导：TIM1 计数 13.889ns、TIM4 计数 138.89ns
 *        上管实际导通窗 = [HIN 上升沿, CCR1)：HIN 上升沿 = DTG + 外部反相器上升延迟
 *        （实测 2.5µs = 180 计数）→ 中点 = (CCR1+180)/2
 *        → CCR4 = (CCR1+180)/2 × (1/10) − 补偿 = (CCR1+180)/20 − 9（下限 18）
 * @来源 memory TIM1_PSC=0/TIM4_PSC=9 推导：TIM1 计数 13.889ns、TIM4 计数 138.89ns
 *        内缩死区下上管导通窗 = [DT, CCR1−DT)，两端各缩 DT → 中点仍为 CCR1/2
 *        → CCR4 = CCR1/2 × (1/10) − 补偿 = CCR1/20 − 9（下限 18）
 * @来源 memory PWM_INNER_CCR2=CCR1−2×DEADTIME：内脉冲两端对称内缩，故中点不随 DT 移动
 */
void DRV_ADC_TrigFollowPwm(void)
{
  uint32_t ccr1 = __HAL_TIM_GET_COMPARE(&htim1, TIM_CHANNEL_1);
  uint32_t ccr4 = ccr1 / 20U;

  if (ccr4 > (DRV_ADC_CCR4_MIN + DRV_ADC_CCR4_COMP))
  {
    ccr4 -= DRV_ADC_CCR4_COMP;
  }
  else
  {
    ccr4 = DRV_ADC_CCR4_MIN;
  }

  __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, ccr4);
}

/**
 * @说明: 取最近一次完整扫描的原始码值（0~4095）
 * @输出: raw_v 电压通道码值；raw_i 电流通道码值
 * @注意: 免临界区一致读——写侧 s_seq 变偶数表示快照完成；读侧前后序号不一致或为奇数则重读
 */
void DRV_ADC_GetRaw(uint16_t *raw_v, uint16_t *raw_i)
{
  uint32_t seq1;
  uint32_t seq2;
  uint16_t v;
  uint16_t i;

  do
  {
    seq1 = s_seq;
    v = s_latest[DRV_ADC_CH_V];
    i = s_latest[DRV_ADC_CH_I];
    seq2 = s_seq;
  } while (((seq1 & 1U) != 0U) || (seq1 != seq2));

  if (raw_v != NULL)
  {
    *raw_v = v;
  }
  if (raw_i != NULL)
  {
    *raw_i = i;
  }
}

/**
 * @说明: 取累计完成的扫描次数（2kHz 触发，可用于核对采样节拍是否在跑）
 */
uint32_t DRV_ADC_GetScanCount(void)
{
  return s_scan_count;
}

/* ==== 中断服务实体（回调本体在 Core/Src/stm32f1xx_it.c） ==== */

/**
 * @说明: ADC 规则组扫描完成的中断实体（DMA 搬完 2 个半字后由 it.c 回调进入）
 * @注意: 本函数即 ISR 上下文：只做快照与计数，不做换算/打印，保持极简
 *        HAL_ADC_ConvCpltCallback 统一放 stm32f1xx_it.c（全工程中断集中处）
 */
void DRV_ADC_ConvCpltIsr(void)
{
  s_seq++;
  s_latest[DRV_ADC_CH_V] = s_dma_buf[DRV_ADC_CH_V];
  s_latest[DRV_ADC_CH_I] = s_dma_buf[DRV_ADC_CH_I];
  s_scan_count++;
  s_seq++;
}
