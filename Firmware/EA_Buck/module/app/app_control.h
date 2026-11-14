/*****************************************************************************
 * @文件: app_control.h
 * @作者: cl
 * @日期: 2026-11-13
 * @版本: v1.2
 * @说明: Buck 输出电压闭环、主循环任务、输出使能与保护接口
 * @平台: STM32F103C8T6
 * @依赖: stdint.h
 ****************************************************************************/
#ifndef APP_CONTROL_H
#define APP_CONTROL_H

#include <stdint.h>

void APP_CONTROL_Init(void);
void APP_CONTROL_ProtectIsr(void);
void APP_CONTROL_Update(void);
void APP_CONTROL_SetVoltageMv(uint32_t voltage_mv);
void APP_CONTROL_SetOutput(uint8_t on);
uint8_t APP_CONTROL_GetOutput(void);
uint8_t APP_CONTROL_IsFault(void);

#endif /* APP_CONTROL_H */
