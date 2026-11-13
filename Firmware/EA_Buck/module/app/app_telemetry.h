/*****************************************************************************
 * @文件: app_telemetry.h
 * @作者: cl
 * @日期: 2026-11-13
 * @版本: v1.0
 * @说明: RTT 遥测接口（1kHz CSV 输出，供 pid-tune 采集）
 * @平台: STM32F103C8T6
 * @依赖: stdint.h
 ****************************************************************************/
#ifndef APP_TELEMETRY_H
#define APP_TELEMETRY_H

#include <stdint.h>

typedef struct
{
  uint32_t t_us;     /* 控制时间戳，单位 us */
  int32_t sp_mv;     /* 电压给定，单位 mV */
  int32_t pv_mv;     /* 输出电压实测，单位 mV */
  uint16_t ccr;      /* 本拍写入的 TIM1_CCR1 */
  int32_t iout_ma;   /* 输出电流实测，单位 mA */
  uint16_t kp_milli; /* 生效环 Kp ×1000 */
  uint16_t ki_milli; /* 生效环 Ki ×1000 */
  uint8_t cascade;   /* 0=启动环 1=级联 */
  uint8_t fault;     /* 故障锁存 */
  uint8_t output;    /* 1=输出开启 0=输出关闭 */
} app_telemetry_frame_t;

void APP_TELEMETRY_Init(void);
void APP_TELEMETRY_Push(const app_telemetry_frame_t *frame);

#endif /* APP_TELEMETRY_H */
