/*****************************************************************************
 * @文件: drv_oled.c
 * @作者: cl
 * @日期: 2026-11-08
 * @版本: v1.0
 * @说明: SSD1306 128x64 OLED 驱动（I2C1 400kHz，页寻址模式，1024B 帧缓冲，脏页局部刷新）
 * @依赖: Core 的 i2c.c / i2c.h
 ****************************************************************************/
#include <string.h>
#include "drv_oled.h"
#include "drv_oled_font.h"
#include "i2c.h"

/* ==== 私有宏 ==== */
#define DRV_OLED_CMD_MAX    32U
#define DRV_OLED_TX_LEN     (DRV_OLED_W + 1U)
#define DRV_OLED_FAIL_LIMIT 3U      /* 连续失败次数达到后复位 I2C1 */

/* 400kHz 累计出错 → 单向回退 100kHz（板上 PB6/PB7 无上拉，全靠 OLED 模块自带）
 * @来源 memory I2C1_SPEED=400kHz 推导：1 页 ≈(3+1+128)B×9bit÷400k ≈ 3ms；8 页 ≈ 24ms
 *        回退 100kHz 是 CubeMX 原默认，上升沿容限 1000ns（Fast-mode 仅 300ns） */
#define DRV_OLED_BUS_SLOW_HZ     100000U
#define DRV_OLED_DOWNGRADE_ERR   12U

/* SSD1306 初始化序列（页寻址模式 0x20,0x02；电荷泵必须开）
 * @来源 通用知识-待复核（SSD1306 控制器手册典型上电序列，项目无该手册）
 *        上电 VDD 稳定 ≥100ms 后调用（由 APP_DISPLAY_Init 保证） */
static const uint8_t s_init_seq[] =
{
  0xAE,           /* 关显示 */
  0xD5, 0x80,     /* 时钟分频/振荡频率 */
  0xA8, 0x3F,     /* 复用率 = 64 行 */
  0xD3, 0x00,     /* 显示垂直偏移 0 */
  0x40,           /* 显示起始行 0 */
  0x8D, 0x14,     /* 电荷泵开（模块无外部 VCC 时必需） */
  0x20, 0x02,     /* 页寻址模式 */
  0xA0,           /* 段重映射：SEG0→列 0（与 0xC0 组合 = 180° 旋转，装反看正） */
  0xC0,           /* COM 扫描：正向（用户 2026-11-08：从屏背面读数） */
  0xDA, 0x12,     /* COM 引脚配置（128x64 交替） */
  0x81, 0xCF,     /* 对比度 */
  0xD9, 0xF1,     /* 预充电周期 */
  0xDB, 0x40,     /* VCOMH 电压 */
  0xA4, 0xA6,     /* 显示跟随显存 + 正常（非反色） */
  0x2E,           /* 关滚动 */
  0xAF            /* 开显示 */
};

/* ==== 私有变量 ==== */
/* 显存：[页][列]，1 字节 = 纵向 8 像素，bit0 为页内最上一行 */
static uint8_t s_fb[DRV_OLED_PAGES][DRV_OLED_W];
static uint8_t s_tx[DRV_OLED_TX_LEN];
static uint8_t s_dirty;     /* bit n = 第 n 页待下发 */
static uint8_t s_ready;
static uint8_t s_addr;
static uint8_t s_fail;
static uint8_t s_bus_slow;  /* 1 = 已从 400kHz 单向回退到 100kHz */
static uint8_t s_gen;       /* 每次成功初始化 +1，上层据此强制重绘 */
static uint32_t s_err;

/* ==== 私有函数 ==== */
/**
 * @说明: 重建 I2C1 总线（先 DeInit 释放 PB6/PB7，再按速率重新 Init）
 * @输入: slow=1 → 100kHz；slow=0 → 恢复 CubeMX 配置（400kHz）
 * @返回: 1=总线就绪；0=重建失败（上层置未就绪，1Hz 周期重探测）
 * @注意: 不做 Error_Handler 停机——功率固件里停机会让 PWM 保持最后占空比
 */
static uint8_t oled_bus_reinit(uint8_t slow)
{
  HAL_I2C_DeInit(&hi2c1);
  if (slow != 0U)
  {
    hi2c1.Init.ClockSpeed = DRV_OLED_BUS_SLOW_HZ;
    return (HAL_I2C_Init(&hi2c1) == HAL_OK) ? 1U : 0U;
  }
  MX_I2C1_Init();
  return 1U;
}

/**
 * @说明: 发送命令流（首字节 0x00 = 后续均为命令）
 * @返回: 1=成功
 */
static uint8_t oled_cmd(const uint8_t *cmd, uint8_t n)
{
  static uint8_t buf[DRV_OLED_CMD_MAX + 1U];

  if (n == 0U || n > DRV_OLED_CMD_MAX)
  {
    return 0U;
  }
  buf[0] = 0x00U;
  memcpy(&buf[1], cmd, n);
  if (HAL_I2C_Master_Transmit(&hi2c1, (uint16_t)((uint16_t)s_addr << 1), buf,
                              (uint16_t)n + 1U, DRV_OLED_IO_TIMEOUT_MS) != HAL_OK)
  {
    return 0U;
  }
  return 1U;
}

/**
 * @说明: 探测从机地址是否有应答
 * @返回: 1=应答
 */
static uint8_t oled_probe(uint8_t addr)
{
  return (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)((uint16_t)addr << 1), 2U, 10U)
          == HAL_OK) ? 1U : 0U;
}

/**
 * @说明: 下发单页（先设页/列地址，再写 128B 显存）
 * @返回: 1=成功
 */
static uint8_t oled_flush_page(uint8_t page)
{
  uint8_t cmd[3];

  cmd[0] = (uint8_t)(0xB0U | page);
  cmd[1] = 0x00U;             /* 列地址低 4 位 = 0 */
  cmd[2] = 0x10U;             /* 列地址高 4 位 = 0 */
  if (!oled_cmd(cmd, 3U))
  {
    return 0U;
  }
  s_tx[0] = 0x40U;            /* 控制字节：后续全为显存数据 */
  memcpy(&s_tx[1], s_fb[page], DRV_OLED_W);
  if (HAL_I2C_Master_Transmit(&hi2c1, (uint16_t)((uint16_t)s_addr << 1), s_tx,
                              DRV_OLED_TX_LEN, DRV_OLED_IO_TIMEOUT_MS) != HAL_OK)
  {
    return 0U;
  }
  return 1U;
}

/* ==== 接口函数 ==== */
uint8_t DRV_OLED_Init(void)
{
  s_ready = 0U;
  if ((s_bus_slow != 0U) && (oled_bus_reinit(1U) == 0U))
  {
    return 0U;      /* 已降速的板子在重探测时保持 100kHz */
  }
  s_addr = DRV_OLED_ADDR_A;
  if (!oled_probe(DRV_OLED_ADDR_A))
  {
    s_addr = DRV_OLED_ADDR_B;
    if (!oled_probe(DRV_OLED_ADDR_B))
    {
      return 0U;
    }
  }
  if (!oled_cmd(s_init_seq, (uint8_t)sizeof(s_init_seq)))
  {
    return 0U;
  }
  memset(s_fb, 0, sizeof(s_fb));
  s_dirty = 0xFFU;            /* 初始化后整屏需一次全量下发 */
  s_fail = 0U;
  s_ready = 1U;
  s_gen++;
  return 1U;
}

uint8_t DRV_OLED_IsReady(void)
{
  return s_ready;
}

uint8_t DRV_OLED_GetAddr(void)
{
  return s_addr;
}

uint8_t DRV_OLED_GetGeneration(void)
{
  return s_gen;
}

uint32_t DRV_OLED_GetErrorCount(void)
{
  return s_err;
}

uint32_t DRV_OLED_GetBusHz(void)
{
  return hi2c1.Init.ClockSpeed;   /* 400000 正常 / 100000 已回退 */
}

void DRV_OLED_Clear(void)
{
  memset(s_fb, 0, sizeof(s_fb));
  s_dirty = 0xFFU;
}

void DRV_OLED_ClearArea(uint8_t x, uint8_t page, uint8_t w, uint8_t pages)
{
  uint16_t end;
  uint8_t p;

  if (x >= DRV_OLED_W || page >= DRV_OLED_PAGES)
  {
    return;
  }
  end = (uint16_t)x + w;
  if (end > DRV_OLED_W)
  {
    w = (uint8_t)(DRV_OLED_W - x);
  }
  end = (uint16_t)page + pages;
  for (p = page; (uint16_t)p < end && p < DRV_OLED_PAGES; p++)
  {
    memset(&s_fb[p][x], 0, w);
    s_dirty |= (uint8_t)(1U << p);
  }
}

void DRV_OLED_DrawText(uint8_t x, uint8_t y, const char *s)
{
  if (s == NULL || (x & 0x07U) != 0U)
  {
    return;
  }
  while (*s != '\0' && (uint16_t)x + DRV_OLED_FONT_W <= DRV_OLED_W)
  {
    uint8_t c = (uint8_t)(*s++);
    const uint8_t *glyph;
    uint8_t r;

    if (c < DRV_OLED_FONT_FIRST || c >= (DRV_OLED_FONT_FIRST + DRV_OLED_FONT_COUNT))
    {
      c = (uint8_t)'?';
    }
    glyph = DRV_OLED_FONT8X16[c - DRV_OLED_FONT_FIRST];
    for (r = 0U; r < DRV_OLED_FONT_H; r++)
    {
      uint16_t py = (uint16_t)y + r;
      uint8_t bits = glyph[r];
      uint8_t col;

      if (py >= DRV_OLED_H)
      {
        break;
      }
      /* 字库按「1 字节 = 1 行像素」存；显存按「1 字节 = 8 行纵向像素」，需转置 */
      for (col = 0U; col < DRV_OLED_FONT_W; col++)
      {
        if ((bits & (uint8_t)(1U << col)) != 0U)
        {
          s_fb[py >> 3][x + col] |= (uint8_t)(1U << (py & 0x07U));
        }
      }
      s_dirty |= (uint8_t)(1U << (py >> 3));
    }
    x = (uint8_t)(x + DRV_OLED_FONT_W);
  }
}

uint8_t DRV_OLED_Flush(void)
{
  uint8_t p;
  uint8_t ok = 1U;
  uint8_t reins;

  if (!s_ready)
  {
    return 0U;
  }
  for (p = 0U; p < DRV_OLED_PAGES; p++)
  {
    if ((s_dirty & (uint8_t)(1U << p)) == 0U)
    {
      continue;
    }
    if (!oled_flush_page(p))
    {
      reins = 0U;
      s_err++;
      s_fail++;
      if ((s_bus_slow == 0U) && (s_err >= DRV_OLED_DOWNGRADE_ERR))
      {
        s_bus_slow = 1U;    /* 400kHz 累计出错 → 单向降速，不再回快 */
        reins = 1U;
        if (oled_bus_reinit(1U) == 0U)
        {
          s_ready = 0U;
        }
      }
      if (s_fail >= DRV_OLED_FAIL_LIMIT)
      {
        s_fail = 0U;
        if ((reins == 0U) && (oled_bus_reinit(s_bus_slow) == 0U))
        {
          s_ready = 0U;
        }
        if ((s_ready != 0U) && !oled_probe(s_addr))
        {
          s_ready = 0U;     /* 屏不在线：交给上层 1Hz 重探测 */
        }
      }
      ok = 0U;
      break;                /* 保留 dirty 位，下周期重试 */
    }
    s_fail = 0U;
    s_dirty &= (uint8_t)~(1U << p);
  }
  return ok;
}
