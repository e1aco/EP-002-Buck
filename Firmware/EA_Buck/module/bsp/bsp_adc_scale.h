/*****************************************************************************
 * @文件: bsp_adc_scale.h
 * @作者: cl
 * @日期: 2026-11-13
 * @版本: v1.0
 * @说明: Buck 板 ADC 采样链标度（电压分压、电流分流与放大）
 * @平台: STM32F103C8T6
 * @依赖: 无
 ****************************************************************************/
#ifndef BSP_ADC_SCALE_H
#define BSP_ADC_SCALE_H

/* 电压通道：Vout[mV] = raw_v × 4423 / 1000
 * @来源 memory ADC_V_SCALE=4.4233mV/码 实测 2026-11-04 三点标定 */
#define BSP_ADC_V_MV_NUM  4423U
#define BSP_ADC_V_MV_DEN  1000U

/* 电流通道：Iout[mA] = raw_i × 1659 / 1000
 * @来源 memory ADC_I_SCALE=1.6589mA/码 实测 2026-11-04 三点标定 */
#define BSP_ADC_I_MA_NUM  1659U
#define BSP_ADC_I_MA_DEN  1000U

#endif /* BSP_ADC_SCALE_H */
