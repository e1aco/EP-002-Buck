/*****************************************************************************
 * @文件: app_telemetry.c
 * @作者: cl
 * @日期: 2026-10-23
 * @版本: v1.0
 * @说明: 遥测应用：把 ADC 原始码值换算成输出电压(mV)/输出电流(mA)，1Hz 打印
 * @平台: STM32F103C8T6
 * @依赖: service_dbg（后端可切）, drv_adc（采样链）, config.h（周期/后端）
 ****************************************************************************/
#include "app_telemetry.h"
#include "config.h"
#include "service_dbg.h"
#include "drv_adc.h"
#include "bsp_adc_scale.h"
#include "app_control.h"
#include "main.h"

/* ADC 换算（整数运算，避免浮点库），板参单源 = bsp_adc_scale.h：
 * 1) 引脚电压 mV：Vpin = raw * 3300 / 4096（VREF=3.3V，12bit）
 * 2) 电压通道：Vbus_mV = raw * BSP_ADC_V_MV_NUM / BSP_ADC_V_MV_DEN（4.423mV/LSB）
 *    校验：raw 2713 -> 2713*4423/1000 = 12000mV ≈ 12.0V
 * 3) 电流通道：Iout_mA = raw * BSP_ADC_I_MA_NUM / BSP_ADC_I_MA_DEN（1.659mA/LSB）
 *    校验：raw 1808 -> 1808*1659/1000 = 2999mA ≈ 3.0A
 * 溢出核算：raw<=4095 -> 4095*4423=1.81e7、4095*1659=6.8e6，均远小于 2^31
 * @来源 memory ADC_CAL_V（2026-11-04 实测四点）（R17/R18=12K -> R19/R20=2.2K，原理图 v1.0 2026-10-24）
 * @来源 memory ADC_CAL_I（2026-11-04 实测四点）（20V/V x 25mΩ，R1 已由 50mΩ 改 25mR）
 * @注意: Vpin/Ipin 是 ADC 引脚上的真实电压，Vbus/Iout 才是折算后的物理量；
 *        调试接线时先看 Vpin（3.3V 注入应读到 ~3300mV），别看 Vbus
 */
#define APP_TELE_ADC_NUM       BSP_ADC_VREF_MV
#define APP_TELE_ADC_DEN       BSP_ADC_RAW_FULL

static uint32_t s_last_print_ms;

/* ==== 接口函数 ==== */

/**
 * @说明: 初始化调试后端 + 采样链，并打印启动横幅（watcher 靠它计 boots）
 * @注意: 横幅必须带固定标识 "Buck Ready"（.cl/require.md 登记）
 */
void APP_TELEMETRY_Init(void)
{
  SERVICE_DBG_Init();
  DRV_ADC_Init();

  SERVICE_DBG_Printf("[BOOT] Buck Ready\r\n");

  s_last_print_ms = HAL_GetTick();
}

/**
 * @说明: 主循环调用：每 CL_TELE_PERIOD_MS 取一次快照并打印一行遥测
 * @注意: 用 (now - last) 差值判周期，HAL_GetTick 回绕安全
 */
void APP_TELEMETRY_Loop(void)
{
  uint32_t now = HAL_GetTick();
  uint16_t raw_v = 0U;
  uint16_t raw_i = 0U;
  uint32_t v_pin_mv;
  uint32_t i_pin_mv;
  uint32_t vbus_mv;
  uint32_t iout_ma;
  uint32_t vref_mv = APP_CONTROL_GetVrefTargetMv();
  uint8_t  out_en = APP_CONTROL_GetOutputEnabled();

  if ((uint32_t)(now - s_last_print_ms) < CL_TELE_PERIOD_MS)
  {
    return;
  }
  s_last_print_ms = now;

  DRV_ADC_GetRaw(&raw_v, &raw_i);

  v_pin_mv = ((uint32_t)raw_v * APP_TELE_ADC_NUM) / APP_TELE_ADC_DEN;
  i_pin_mv = ((uint32_t)raw_i * APP_TELE_ADC_NUM) / APP_TELE_ADC_DEN;
  vbus_mv = ((uint32_t)raw_v * BSP_ADC_V_MV_NUM) / BSP_ADC_V_MV_DEN;
  iout_ma = ((uint32_t)raw_i * BSP_ADC_I_MA_NUM) / BSP_ADC_I_MA_DEN;

  SERVICE_DBG_Printf("[TELE] n=%lu rv=%u ri=%u Vpin=%lumV Ipin=%lumV "
                     "Vbus=%lu.%03luV Iout=%lu.%03luA "
                     "vref=%lu.%03luV en=%u\r\n",
                     (unsigned long)DRV_ADC_GetScanCount(),
                     (unsigned)raw_v,
                     (unsigned)raw_i,
                     (unsigned long)v_pin_mv,
                     (unsigned long)i_pin_mv,
                     (unsigned long)(vbus_mv / 1000U),
                     (unsigned long)(vbus_mv % 1000U),
                     (unsigned long)(iout_ma / 1000U),
                     (unsigned long)(iout_ma % 1000U),
                     (unsigned long)(vref_mv / 1000U),
                     (unsigned long)(vref_mv % 1000U),
                     (unsigned)out_en);
}
