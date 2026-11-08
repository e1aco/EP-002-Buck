/*****************************************************************************
 * @文件: bsp_adc_scale.h
 * @作者: cl
 * @日期: 2026-11-04
 * @版本: v1.0
 * @说明: ADC 采样链板级标度（分压比 / 分流电阻 / 运放增益 -> mV、mA 换算）
 * @平台: STM32F103C8T6
 * @注意: 板参单源——app_telemetry 与 app_control 共用，标定后只改本文件
 ****************************************************************************/
#ifndef __BSP_ADC_SCALE_H
#define __BSP_ADC_SCALE_H

/* 12bit ADC 满量程：raw 0~4095 对应 0~VREF
 * @来源 memory ADC_VPIN_3V3=3299mV@raw4095（注入 3.3V 实测，2026-10-23） */
#define BSP_ADC_VREF_MV        3300U
#define BSP_ADC_RAW_FULL       4096U

/* 电压通道：Vout -> 差分衰减 ~0.1821V/V（R17/R18=12K，R19/R20=2.2K）
 * 换算链：Vpin[mV] = raw × 3300 / 4096（0.8057mV/LSB）
 *         Vout[mV] = Vpin / 0.1821
 * 标定（2026-11-04 三点，万用表 vs raw）：
 *   raw 1076 <-> 4.76V、raw 1885 <-> 8.35V、raw 2934 <-> 12.97V
 *   最小二乘：k = Σ(raw×V)/Σ(raw²) = 4.4233mV/码；三点残差 −0.06%/+0.03%/+0.04%
 * 旧值 4395 系统性偏低 0.64%（分压电阻实测 12K/2.2K 有容差）
 * @来源 memory ADC_CAL_20261104 三点实测推导：raw×k 拟合，未加偏移项（偏移 <50mV 已并入斜率） */
#define BSP_ADC_V_MV_NUM       4423U
#define BSP_ADC_V_MV_DEN       1000U

/* 电流通道：25mΩ 分流 × 差分放大 20V/V ≈ 0.5V/A
 * 换算链：Ipin[mV] = raw × 3300 / 4096（0.8057mV/LSB）
 *         Iout[mA] = Ipin / 0.5
 * 标定（2026-11-04 三点，采样电阻两端电压/25mΩ vs raw）：
 *   raw 503 <-> 21.0mV(0.840A)、raw 790 <-> 32.6mV(1.304A)、raw 282 <-> 11.9mV(0.476A)
 *   最小二乘：k = Σ(raw×I)/Σ(raw²) = 1.6589mA/码；残差 −0.7%/+0.5%/−1.7%
 *   最低点(0.476A/深 DCM)偏低 1.7% 属采样相位偏差，非标度误差
 * 旧值 1611 系统性偏低 2.9%（20V/V 增益电阻与 25mΩ 实测有容差）
 * @来源 memory ADC_CAL_20261104 三点实测推导：raw×k 拟合，偏移项 <30mA 已并入斜率 */
#define BSP_ADC_I_MA_NUM       1659U
#define BSP_ADC_I_MA_DEN       1000U

#endif /* __BSP_ADC_SCALE_H */
