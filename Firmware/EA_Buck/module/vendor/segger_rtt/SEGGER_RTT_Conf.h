/*********************************************************************
*                   (c) SEGGER Microcontroller GmbH                  *
*                        The Embedded Experts                        *
*                           www.segger.com                           *
**********************************************************************
*                                                                    *
*        SEGGER RTT * Real Time Transfer for embedded targets        *
*                  https://github.com/SEGGERMicro/RTT                *
*                                                                    *
**********************************************************************

---------------------------END-OF-HEADER------------------------------
Purpose : User configuration file for RTT.
          For available configuration,
          refer to SEGGER_RTT_ConfDefaults.h.

----------------------------------------------------------------------
*/

#ifndef SEGGER_RTT_CONF_H
#define SEGGER_RTT_CONF_H

/*********************************************************************
*
*       Defines, configurable
*
**********************************************************************
*/

/* 上行缓冲 1024B：1Hz 一行 ~50B，够留 20 行历史；RAM 预算见 .cl/memory/chip.md
 * （CHIP_RAM=20KB，1024B 占 5%，留足栈与 ADC/DMA 缓冲）。 */
#define BUFFER_SIZE_UP  1024

#endif
/*************************** End of file ****************************/
