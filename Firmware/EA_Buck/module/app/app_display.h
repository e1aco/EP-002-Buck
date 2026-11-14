/*****************************************************************************
 * @文件: app_display.h
 * @作者: cl
 * @日期: 2026-11-14
 * @版本: v1.0
 * @说明: OLED 显示应用接口（电压/电流/档位/状态）
 * @平台: STM32F103C8T6
 * @依赖: stdint.h
 ****************************************************************************/
#ifndef APP_DISPLAY_H
#define APP_DISPLAY_H

#include <stdint.h>

void APP_DISPLAY_Init(void);
void APP_DISPLAY_Task(void);

#endif /* APP_DISPLAY_H */
