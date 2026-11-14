/*****************************************************************************
 * @文件: app_key.h
 * @作者: cl
 * @日期: 2026-11-13
 * @版本: v1.0
 * @说明: 按键业务编排接口（KEY1 输出开关、KEY2 档位切换）
 * @平台: STM32F103C8T6
 * @依赖: stdint.h
 ****************************************************************************/
#ifndef APP_KEY_H
#define APP_KEY_H

#include <stdint.h>

void APP_KEY_Init(void);
void APP_KEY_Task(void);
int8_t APP_KEY_GetDirection(void);

#endif /* APP_KEY_H */
