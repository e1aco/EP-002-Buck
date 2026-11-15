/*****************************************************************************
 * @文件: app_cmd.c
 * @作者: cl
 * @日期: 2026-11-15
 * @版本: v1.0
 * @说明: RTT 命令通道实现 —— 先设电压再开输出，走完整 SetOutput 语义
 * @平台: STM32F103C8T6
 * @依赖: app_cmd.h, app_control.h, drv_rtt.h
 ****************************************************************************/
#include "app_cmd.h"
#include "app_control.h"
#include "drv_rtt.h"

/* ==== 模块常量 ==== */
#define APP_CMD_LINE_MAX     24U   /* 单行命令最大长度 */
#define APP_CMD_CHUNK        16U   /* 每次从 RTT 取多少字节 */
#define APP_CMD_MIN_MV       3300U
#define APP_CMD_MAX_MV       12000U

/* 固件版本标识：随启动横幅输出，便于确认板子上跑的是哪一版固件
 * @来源 本设计（2026-11-15）：生产版需可追溯标识；横幅仍以 "RDY" 开头，
 * 既有按 "RDY" 前缀匹配的调试脚本不受影响 */
#define APP_CMD_FW_VERSION   "v1.0"
#define APP_CMD_BANNER       "RDY " APP_CMD_FW_VERSION "\n"

/* ==== 模块变量 ==== */
static char s_line[APP_CMD_LINE_MAX];
static uint8_t s_len;

/* ==== 内部函数 ==== */
/**
 * @说明: 无符号整数转十进制文本
 * @输入: value 数值；buf 输出缓冲；cap 缓冲容量
 * @输出: 写入的字符数（不含结束符）
 * @注意: 不依赖 stdio，避免引入 printf 增大代码体积
 */
static uint8_t app_cmd_u32_to_str(uint32_t value, char *buf, uint8_t cap)
{
  char tmp[11];
  uint8_t n = 0U;
  uint8_t i;

  if (cap == 0U)
  {
    return 0U;
  }

  do
  {
    tmp[n] = (char)('0' + (value % 10U));
    value /= 10U;
    n++;
  } while ((value != 0U) && (n < sizeof(tmp)));

  if (n > (uint8_t)(cap - 1U))
  {
    n = (uint8_t)(cap - 1U);
  }

  for (i = 0U; i < n; i++)
  {
    buf[i] = tmp[n - 1U - i];
  }
  buf[n] = '\0';

  return n;
}

/**
 * @说明: 追加一段文本到输出缓冲
 * @输入: dst 目标；pos 当前写入位置；cap 容量；src 源文本
 * @输出: 新的写入位置
 * @注意: 容量不足时截断，不越界
 */
static uint8_t app_cmd_append(char *dst, uint8_t pos, uint8_t cap, const char *src)
{
  while ((*src != '\0') && (pos < (uint8_t)(cap - 1U)))
  {
    dst[pos] = *src;
    pos++;
    src++;
  }
  dst[pos] = '\0';
  return pos;
}

/**
 * @说明: 上报当前状态
 * @输入: 无
 * @输出: 无
 * @注意: 格式 OK V=<目标mV> OUT=<0|1> F=<0|1> VOUT=<mV> IOUT=<mA>
 */
static void app_cmd_report(void)
{
  char out[80];
  char num[12];
  int32_t vout_mv;
  int32_t iout_ma;
  uint32_t vref_mv;
  uint8_t output_on;
  uint8_t fault;
  uint8_t pos = 0U;
  uint8_t cap = (uint8_t)sizeof(out);

  APP_CONTROL_GetSnapshot(&vout_mv, &iout_ma, &vref_mv, &output_on, &fault);

  pos = app_cmd_append(out, pos, cap, "OK V=");
  (void)app_cmd_u32_to_str(APP_CONTROL_GetVoltageMv(), num, (uint8_t)sizeof(num));
  pos = app_cmd_append(out, pos, cap, num);

  pos = app_cmd_append(out, pos, cap, " OUT=");
  pos = app_cmd_append(out, pos, cap, (output_on != 0U) ? "1" : "0");
  pos = app_cmd_append(out, pos, cap, " F=");
  pos = app_cmd_append(out, pos, cap, (fault != 0U) ? "1" : "0");

  pos = app_cmd_append(out, pos, cap, " VOUT=");
  (void)app_cmd_u32_to_str((uint32_t)((vout_mv > 0) ? vout_mv : 0), num,
                           (uint8_t)sizeof(num));
  pos = app_cmd_append(out, pos, cap, num);

  pos = app_cmd_append(out, pos, cap, " IOUT=");
  (void)app_cmd_u32_to_str((uint32_t)((iout_ma > 0) ? iout_ma : 0), num,
                           (uint8_t)sizeof(num));
  pos = app_cmd_append(out, pos, cap, num);

  pos = app_cmd_append(out, pos, cap, "\n");

  (void)DRV_RTT_Write(out, pos);
}

/**
 * @说明: 解析一行命令并执行
 * @输入: line 以 '\0' 结尾的命令文本
 * @输出: 无
 * @注意: V<mV> 设目标；ON/OFF 开关输出；? 上报；其它回 ERR
 * @注意: ON 走 APP_CONTROL_SetOutput(1)，内部会把 s_vref_mv 清 0 触发软启动，
 *        因此输出是从 0 V 平滑爬升，不会出现直接写标志位导致的占空比猛冲
 */
static void app_cmd_exec(char *line)
{
  uint32_t mv = 0U;
  uint8_t i;

  if ((line[0] == 'V') || (line[0] == 'v'))
  {
    for (i = 1U; (line[i] >= '0') && (line[i] <= '9'); i++)
    {
      mv = (mv * 10U) + (uint32_t)(line[i] - '0');
    }
    if (mv < APP_CMD_MIN_MV)
    {
      mv = APP_CMD_MIN_MV;
    }
    if (mv > APP_CMD_MAX_MV)
    {
      mv = APP_CMD_MAX_MV;
    }
    APP_CONTROL_SetVoltageMv(mv);
    app_cmd_report();
    return;
  }

  if ((line[0] == 'O') || (line[0] == 'o'))
  {
    if ((line[1] == 'N') || (line[1] == 'n'))
    {
      APP_CONTROL_SetOutput(1U);
      app_cmd_report();
      return;
    }
    if ((line[1] == 'F') || (line[1] == 'f'))
    {
      APP_CONTROL_SetOutput(0U);
      app_cmd_report();
      return;
    }
  }

  if (line[0] == '?')
  {
    app_cmd_report();
    return;
  }

  (void)DRV_RTT_Write("ERR\n", 4U);
}

/* ==== 接口函数 ==== */
/**
 * @说明: 初始化 RTT 命令通道
 * @输出: 无
 * @注意: 必须在 APP_CONTROL_Init 之后调用
 */
void APP_CMD_Init(void)
{
  DRV_RTT_Init();
  s_len = 0U;
  (void)DRV_RTT_Write(APP_CMD_BANNER, (uint16_t)(sizeof(APP_CMD_BANNER) - 1U));
}

/**
 * @说明: 轮询 RTT 下行通道，按行解析命令
 * @输出: 无
 * @注意: 非阻塞；每次最多取 APP_CMD_CHUNK 字节，主循环开销可忽略
 */
void APP_CMD_Task(void)
{
  char chunk[APP_CMD_CHUNK];
  uint16_t got;
  uint16_t i;

  got = DRV_RTT_Read(chunk, (uint16_t)sizeof(chunk));
  if (got == 0U)
  {
    return;
  }

  for (i = 0U; i < got; i++)
  {
    char c = chunk[i];

    if ((c == '\n') || (c == '\r'))
    {
      if (s_len > 0U)
      {
        s_line[s_len] = '\0';
        app_cmd_exec(s_line);
        s_len = 0U;
      }
    }
    else if (s_len < (uint8_t)(APP_CMD_LINE_MAX - 1U))
    {
      s_line[s_len] = c;
      s_len++;
    }
    else
    {
      s_len = 0U;   /* 超长行丢弃 */
    }
  }
}
