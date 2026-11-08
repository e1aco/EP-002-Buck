/*****************************************************************************
 * @文件: app_telemetry.h
 * @作者: cl
 * @日期: 2026-10-23
 * @版本: v1.0
 * @说明: 遥测应用：1Hz 打印电压/电流采样结果（原始码值 + 物理量）
 ****************************************************************************/
#ifndef __APP_TELEMETRY_H
#define __APP_TELEMETRY_H

void APP_TELEMETRY_Init(void);
void APP_TELEMETRY_Loop(void);

#endif /* __APP_TELEMETRY_H */
