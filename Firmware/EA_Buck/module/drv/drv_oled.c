/*****************************************************************************
 * @文件: drv_oled.c
 * @作者: cl
 * @日期: 2026-11-13
 * @版本: v1.0
 * @说明: SSD1306 OLED 驱动实现（当前仅 I2C 总线探测）
 * @平台: STM32F103C8T6
 * @依赖: drv_oled.h, drv_rtt.h, i2c.h
 ****************************************************************************/
#include "drv_oled.h"
#include "drv_rtt.h"
#include "i2c.h"

/* ==== 模块常量 ==== */
/* I2C 7 位地址扫描范围 0x08–0x77（保留地址不扫）
 * @来源 I2C 规范：0x00–0x07 与 0x78–0x7F 为保留地址 */
#define DRV_OLED_ADDR_MIN  0x08U
#define DRV_OLED_ADDR_MAX  0x77U

/* 每地址探测 2 次、单次超时 5ms：SSD1306 上电 100ms 内可能不应答
 * @来源 SSD1306 datasheet：上电后需 100ms 初始化延时 */
#define DRV_OLED_TRIALS    2U
#define DRV_OLED_TIMEOUT   5U

/* ==== 内部函数 ==== */
/**
 * @说明: 通过 RTT 打印一个 I2C 地址，格式 "I2C:0x3C\n"
 * @输入: addr 7 位地址
 * @输出: 无
 */
static void drv_oled_print_addr(uint8_t addr)
{
  static const char hex[] = "0123456789ABCDEF";
  char buf[16];
  uint8_t n = 0U;

  buf[n] = 'I';
  n++;
  buf[n] = '2';
  n++;
  buf[n] = 'C';
  n++;
  buf[n] = ':';
  n++;
  buf[n] = '0';
  n++;
  buf[n] = 'x';
  n++;
  buf[n] = hex[(uint8_t)(addr >> 4) & 0x0FU];
  n++;
  buf[n] = hex[addr & 0x0FU];
  n++;
  buf[n] = '\n';
  n++;

  (void)DRV_RTT_Write(buf, n);
}

/* ==== 接口函数 ==== */
/**
 * @说明: 扫描 I2C1 总线，打印所有应答的 7 位地址
 * @输出: 找到的设备数量
 * @注意: 全程阻塞，最坏约 112 × 2 × 5ms ≈ 1.1s，必须在 PWM 启动前调用
 * @注意: 仅用于硬件连通性确认，确认后应从 main 中移除调用
 * @来源 ES096 勘误：I2C BUSY 位可能在从机未就绪时粘滞，需复位外设状态机
 */
uint8_t DRV_OLED_Probe(void)
{
  uint8_t addr;
  uint8_t found = 0U;

  /* 复位 I2C 外设，清除粘滞的 BUSY 位 */
  (void)HAL_I2C_DeInit(&hi2c1);
  MX_I2C1_Init();

  for (addr = DRV_OLED_ADDR_MIN; addr <= DRV_OLED_ADDR_MAX; addr++)
  {
    if (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)((uint16_t)addr << 1), DRV_OLED_TRIALS,
                              DRV_OLED_TIMEOUT) == HAL_OK)
    {
      drv_oled_print_addr(addr);
      found++;
    }
  }

  return found;
}
