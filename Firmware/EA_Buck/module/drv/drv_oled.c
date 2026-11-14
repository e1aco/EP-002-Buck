/*****************************************************************************
 * @文件: drv_oled.c
 * @作者: cl
 * @日期: 2026-11-14
 * @版本: v1.1
 * @说明: SSD1306 OLED 驱动实现（I2C 阻塞发送 + 页刷新，128x64）
 * @平台: STM32F103C8T6
 * @依赖: drv_oled.h, drv_rtt.h, i2c.h
 * @来源 SSD1306 datasheet 8.1.2 控制字节 / 10.1.1 初始化序列 -待复核
 ****************************************************************************/
#include "drv_oled.h"
#include "drv_rtt.h"
#include "i2c.h"
#include <string.h>

/* ==== 模块常量 ==== */
/* I2C 7 位地址扫描范围 0x08–0x77（保留地址不扫）
 * @来源 I2C 规范：0x00–0x07 与 0x78–0x7F 为保留地址 */
#define DRV_OLED_ADDR_MIN  0x08U
#define DRV_OLED_ADDR_MAX  0x77U

/* 每地址探测 2 次、单次超时 5ms
 * @来源 SSD1306 datasheet：上电后需 100ms 初始化延时 */
#define DRV_OLED_TRIALS    2U
#define DRV_OLED_TIMEOUT   5U

/* SSD1306 7 位地址 0x3C
 * @来源 实测 I2C 扫描返回 I2C:0x3C 2026-11-14 */
#define DRV_OLED_I2C_ADDR  0x3CU

/* 单次传输超时 100ms（129 字节 @400kHz 约 2.9ms，留足余量）
 * @来源 400kHz: 129B × 9bit / 400kHz = 2.9ms */
#define DRV_OLED_TX_TIMEOUT 100U

/* 控制字节：Co=0、D/C#=0 为命令流，D/C#=1 为数据流
 * @来源 SSD1306 datasheet 8.1.2 */
#define DRV_OLED_CTRL_CMD  0x00U
#define DRV_OLED_CTRL_DATA 0x40U

/* 页地址命令基址 0xB0 + page（0..7）
 * @来源 SSD1306 datasheet 10.1.4 页寻址模式 */
#define DRV_OLED_PAGE_BASE 0xB0U

/* ==== 模块变量 ==== */
static uint8_t s_drv_oled_buf[DRV_OLED_WIDTH * DRV_OLED_PAGES];
static uint8_t s_drv_oled_tx[1U + DRV_OLED_WIDTH];

/* SSD1306 通用初始化序列
 * @来源 SSD1306 datasheet 10.1.1 -待复核 */
static const uint8_t s_drv_oled_init_seq[] =
{
  0xAEU,        /* display off            */
  0xD5U, 0x80U, /* clock divide / osc     */
  0xA8U, 0x3FU, /* multiplex ratio 64     */
  0xD3U, 0x00U, /* display offset 0       */
  0x40U,        /* start line 0           */
  0x8DU, 0x14U, /* charge pump enable     */
  0x20U, 0x00U, /* horizontal addressing  */
  0xA1U,        /* segment remap          */
  0xC8U,        /* COM scan direction dec */
  0xDAU, 0x12U, /* COM pins config        */
  0x81U, 0xCFU, /* contrast               */
  0xD9U, 0xF1U, /* pre-charge period      */
  0xDBU, 0x40U, /* VCOMH deselect level   */
  0xA4U,        /* resume to RAM content  */
  0xA6U,        /* normal (not inverse)   */
  0xAFU         /* display on             */
};

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

/**
 * @说明: 发送 SSD1306 初始化命令序列并清空显存
 * @输出: 0=成功，1=I2C 发送失败
 * @注意: 需在 I2C 初始化之后调用；内部已包含 display on
 */
uint8_t DRV_OLED_Init(void)
{
  uint8_t cmd[sizeof(s_drv_oled_init_seq) + 1U];
  uint8_t i;

  cmd[0] = DRV_OLED_CTRL_CMD;
  for (i = 0U; i < (uint8_t)sizeof(s_drv_oled_init_seq); i++)
  {
    cmd[i + 1U] = s_drv_oled_init_seq[i];
  }

  if (HAL_I2C_Master_Transmit(&hi2c1, (uint16_t)(DRV_OLED_I2C_ADDR << 1),
                              cmd, (uint16_t)sizeof(cmd),
                              DRV_OLED_TX_TIMEOUT) != HAL_OK)
  {
    return 1U;
  }

  DRV_OLED_Fill(0x00U);
  DRV_OLED_Refresh();

  return 0U;
}

/**
 * @说明: 用指定图案填充整块显存（0x00 全灭 / 0xFF 全亮）
 * @输入: pattern 填充字节
 * @输出: 无
 */
void DRV_OLED_Fill(uint8_t pattern)
{
  (void)memset(s_drv_oled_buf, pattern, sizeof(s_drv_oled_buf));
}

/**
 * @说明: 把显存整屏刷到 OLED（8 页 × 128 字节，阻塞发送）
 * @输出: 无
 * @注意: 全屏约 23ms @400kHz；控制已在 ISR 中，主循环阻塞不影响控制
 */
void DRV_OLED_Refresh(void)
{
  uint8_t page;
  uint8_t cmd[4];

  cmd[0] = DRV_OLED_CTRL_CMD;
  cmd[2] = 0x00U;
  cmd[3] = 0x10U;

  for (page = 0U; page < DRV_OLED_PAGES; page++)
  {
    cmd[1] = (uint8_t)(DRV_OLED_PAGE_BASE | page);

    if (HAL_I2C_Master_Transmit(&hi2c1, (uint16_t)(DRV_OLED_I2C_ADDR << 1),
                                cmd, 4U, DRV_OLED_TX_TIMEOUT) != HAL_OK)
    {
      return;
    }

    s_drv_oled_tx[0] = DRV_OLED_CTRL_DATA;
    (void)memcpy(&s_drv_oled_tx[1], &s_drv_oled_buf[page * DRV_OLED_WIDTH], DRV_OLED_WIDTH);

    if (HAL_I2C_Master_Transmit(&hi2c1, (uint16_t)(DRV_OLED_I2C_ADDR << 1),
                                s_drv_oled_tx, (uint16_t)sizeof(s_drv_oled_tx),
                                DRV_OLED_TX_TIMEOUT) != HAL_OK)
    {
      return;
    }
  }
}

/* ==== 字模 ==== */
/* 6x8 ASCII 点阵，索引 = 字符码 - 0x20（覆盖 0x20-0x5A：空格/数字/大写字母/部分符号）
 * 每字符 6 字节列数据，bit0 为最上一行；第 6 列为字间距
 * @来源 标准 5x7 点阵字模居中于 6x8 格子 -待复核 */
static const uint8_t s_drv_oled_font[59][6] =
{
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* space */
  { 0x00, 0x00, 0x5F, 0x00, 0x00, 0x00 }, /* ! */
  { 0x00, 0x07, 0x00, 0x07, 0x00, 0x00 }, /* " */
  { 0x14, 0x7F, 0x14, 0x7F, 0x14, 0x00 }, /* # */
  { 0x24, 0x2A, 0x7F, 0x2A, 0x12, 0x00 }, /* $ */
  { 0x23, 0x13, 0x08, 0x64, 0x62, 0x00 }, /* % */
  { 0x36, 0x49, 0x55, 0x22, 0x50, 0x00 }, /* & */
  { 0x00, 0x05, 0x03, 0x00, 0x00, 0x00 }, /* quote */
  { 0x00, 0x1C, 0x22, 0x41, 0x00, 0x00 }, /* ( */
  { 0x00, 0x41, 0x22, 0x1C, 0x00, 0x00 }, /* ) */
  { 0x14, 0x08, 0x3E, 0x08, 0x14, 0x00 }, /* * */
  { 0x08, 0x08, 0x3E, 0x08, 0x08, 0x00 }, /* + */
  { 0x00, 0x50, 0x30, 0x00, 0x00, 0x00 }, /* , */
  { 0x08, 0x08, 0x08, 0x08, 0x08, 0x00 }, /* - */
  { 0x00, 0x60, 0x60, 0x00, 0x00, 0x00 }, /* . */
  { 0x20, 0x10, 0x08, 0x04, 0x02, 0x00 }, /* / */
  { 0x3E, 0x51, 0x49, 0x45, 0x3E, 0x00 }, /* 0 */
  { 0x00, 0x42, 0x7F, 0x40, 0x00, 0x00 }, /* 1 */
  { 0x42, 0x61, 0x51, 0x49, 0x46, 0x00 }, /* 2 */
  { 0x21, 0x41, 0x45, 0x4B, 0x31, 0x00 }, /* 3 */
  { 0x18, 0x14, 0x12, 0x7F, 0x10, 0x00 }, /* 4 */
  { 0x27, 0x45, 0x45, 0x45, 0x39, 0x00 }, /* 5 */
  { 0x3C, 0x4A, 0x49, 0x49, 0x30, 0x00 }, /* 6 */
  { 0x01, 0x71, 0x09, 0x05, 0x03, 0x00 }, /* 7 */
  { 0x36, 0x49, 0x49, 0x49, 0x36, 0x00 }, /* 8 */
  { 0x06, 0x49, 0x49, 0x29, 0x1E, 0x00 }, /* 9 */
  { 0x00, 0x36, 0x36, 0x00, 0x00, 0x00 }, /* : */
  { 0x00, 0x56, 0x36, 0x00, 0x00, 0x00 }, /* ; */
  { 0x08, 0x14, 0x22, 0x41, 0x00, 0x00 }, /* < */
  { 0x14, 0x14, 0x14, 0x14, 0x14, 0x00 }, /* = */
  { 0x00, 0x41, 0x22, 0x14, 0x08, 0x00 }, /* > */
  { 0x02, 0x01, 0x51, 0x09, 0x06, 0x00 }, /* ? */
  { 0x32, 0x49, 0x79, 0x41, 0x3E, 0x00 }, /* @ */
  { 0x7E, 0x11, 0x11, 0x11, 0x7E, 0x00 }, /* A */
  { 0x7F, 0x49, 0x49, 0x49, 0x36, 0x00 }, /* B */
  { 0x3E, 0x41, 0x41, 0x41, 0x22, 0x00 }, /* C */
  { 0x7F, 0x41, 0x41, 0x22, 0x1C, 0x00 }, /* D */
  { 0x7F, 0x49, 0x49, 0x49, 0x41, 0x00 }, /* E */
  { 0x7F, 0x09, 0x09, 0x09, 0x01, 0x00 }, /* F */
  { 0x3E, 0x41, 0x49, 0x49, 0x7A, 0x00 }, /* G */
  { 0x7F, 0x08, 0x08, 0x08, 0x7F, 0x00 }, /* H */
  { 0x00, 0x41, 0x7F, 0x41, 0x00, 0x00 }, /* I */
  { 0x20, 0x40, 0x41, 0x3F, 0x01, 0x00 }, /* J */
  { 0x7F, 0x08, 0x14, 0x22, 0x41, 0x00 }, /* K */
  { 0x7F, 0x40, 0x40, 0x40, 0x40, 0x00 }, /* L */
  { 0x7F, 0x02, 0x0C, 0x02, 0x7F, 0x00 }, /* M */
  { 0x7F, 0x04, 0x08, 0x10, 0x7F, 0x00 }, /* N */
  { 0x3E, 0x41, 0x41, 0x41, 0x3E, 0x00 }, /* O */
  { 0x7F, 0x09, 0x09, 0x09, 0x06, 0x00 }, /* P */
  { 0x3E, 0x41, 0x51, 0x21, 0x5E, 0x00 }, /* Q */
  { 0x7F, 0x09, 0x19, 0x29, 0x46, 0x00 }, /* R */
  { 0x46, 0x49, 0x49, 0x49, 0x31, 0x00 }, /* S */
  { 0x01, 0x01, 0x7F, 0x01, 0x01, 0x00 }, /* T */
  { 0x3F, 0x40, 0x40, 0x40, 0x3F, 0x00 }, /* U */
  { 0x1F, 0x20, 0x40, 0x20, 0x1F, 0x00 }, /* V */
  { 0x3F, 0x40, 0x38, 0x40, 0x3F, 0x00 }, /* W */
  { 0x63, 0x14, 0x08, 0x14, 0x63, 0x00 }, /* X */
  { 0x07, 0x08, 0x70, 0x08, 0x07, 0x00 }, /* Y */
  { 0x61, 0x51, 0x49, 0x45, 0x43, 0x00 }  /* Z */
};

/* ==== 绘图接口 ==== */
/**
 * @说明: 在指定页的指定列画一个 6x8 字符
 * @输入: x 起始列(0..122)；page 页号(0..7)；ch 字符（小写自动转大写）
 * @输出: 无
 * @注意: 超出边界的字符直接忽略
 */
void DRV_OLED_DrawChar(uint8_t x, uint8_t page, char ch)
{
  const uint8_t *glyph;
  uint8_t i;

  if ((uint8_t)(x + 6U) > DRV_OLED_WIDTH || page >= DRV_OLED_PAGES)
  {
    return;
  }

  if ((ch >= 'a') && (ch <= 'z'))
  {
    ch = (char)(ch - 'a' + 'A');
  }

  if ((ch < 0x20) || (ch > 0x5A))
  {
    ch = ' ';
  }

  glyph = s_drv_oled_font[(uint8_t)ch - 0x20U];
  for (i = 0U; i < 6U; i++)
  {
    s_drv_oled_buf[(page * DRV_OLED_WIDTH) + x + i] = glyph[i];
  }
}

/**
 * @说明: 在指定页画一个字符串（6 像素/字符，超出右边界自动换行到下一页）
 * @输入: x 起始列；page 起始页；str 字符串
 * @输出: 无
 */
void DRV_OLED_DrawString(uint8_t x, uint8_t page, const char *str)
{
  while ((*str != 0) && (page < DRV_OLED_PAGES))
  {
    if ((uint8_t)(x + 6U) > DRV_OLED_WIDTH)
    {
      x = 0U;
      page++;
      continue;
    }

    DRV_OLED_DrawChar(x, page, *str);
    x = (uint8_t)(x + 6U);
    str++;
  }
}
