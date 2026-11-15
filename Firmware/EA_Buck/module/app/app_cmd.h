/*****************************************************************************
 * @文件: app_cmd.h
 * @作者: cl
 * @日期: 2026-11-15
 * @版本: v1.0
 * @说明: RTT 命令通道应用层接口（替代按键设定输出）
 * @平台: STM32F103C8T6
 * @依赖: stdint.h
 ****************************************************************************/
#ifndef APP_CMD_H
#define APP_CMD_H

#include <stdint.h>

void APP_CMD_Init(void);
void APP_CMD_Task(void);

#endif /* APP_CMD_H */
