/*****************************************************************************
 * @文件: app_telemetry.c
 * @作者: cl
 * @日期: 2026-11-13
 * @版本: v1.0
 * @说明: RTT 遥测实现（1kHz 一行 CSV，格式对齐 pid-tune csv8）
 * @平台: STM32F103C8T6
 * @依赖: app_telemetry.h, drv_rtt.h
 ****************************************************************************/
#include "app_telemetry.h"
#include "drv_rtt.h"

/* ==== 模块常量 ==== */
/* 输出周期 1ms = 1kHz：电压环 fc≈50Hz、τ≈3.2ms，建立过程 10–16ms，
 * 1kHz 可给出 10–16 个点，足以定位超调峰值
 * @来源 memory CTRL_VOLT_KP=0.050 推导：fc=50Hz → τ=1/(2π×50)=3.18ms */
#define APP_TELEMETRY_PERIOD_US 1000U

/* 单行最长 62 字符（t 10 + sp 5 + pv 5 + ccr 4 + err 6 + kp 5 + ki 5
 * + d 1 + iout 6 + cascade 1 + fault 1 + output 1 + 12 个分隔符），留 72 字节余量
 * @来源 本设计：各字段按 int32 最大 11 位、uint16 最大 5 位累加 */
#define APP_TELEMETRY_LINE_MAX 72U

/* ==== 模块变量 ==== */
static uint32_t s_last_us;

/* ==== 内部函数 ==== */
/**
 * @说明: 把无符号整数按十进制写入缓冲区
 * @输入: buf 目标缓冲区；pos 写入起点；value 待写数值
 * @输出: 写入后的新位置
 */
static uint8_t app_telemetry_u32(char *buf, uint8_t pos, uint32_t value)
{
  char tmp[10];
  uint8_t n = 0U;

  do
  {
    tmp[n] = (char)('0' + (char)(value % 10U));
    value /= 10U;
    n++;
  } while ((value != 0U) && (n < 10U));

  while (n > 0U)
  {
    n--;
    buf[pos] = tmp[n];
    pos++;
  }

  return pos;
}

/**
 * @说明: 把有符号整数按十进制写入缓冲区
 * @输入: buf 目标缓冲区；pos 写入起点；value 待写数值
 * @输出: 写入后的新位置
 * @注意: 取负前先做 -1 再取反，避免 INT32_MIN 取负溢出
 */
static uint8_t app_telemetry_i32(char *buf, uint8_t pos, int32_t value)
{
  uint32_t magnitude;

  if (value < 0)
  {
    buf[pos] = '-';
    pos++;
    magnitude = (uint32_t)(-(value + 1)) + 1U;
  }
  else
  {
    magnitude = (uint32_t)value;
  }

  return app_telemetry_u32(buf, pos, magnitude);
}

/* ==== 接口函数 ==== */
/**
 * @说明: 初始化遥测模块，复位输出节拍
 * @输出: 无
 */
void APP_TELEMETRY_Init(void)
{
  s_last_us = 0U;
  DRV_RTT_Init();
}

/**
 * @说明: 按 1kHz 节拍输出一行 CSV 遥测
 * @输入: frame 本拍控制数据
 * @输出: 无
 * @注意: 未到节拍直接返回，调用开销仅为一次减法比较
 * @注意: 行格式 t_ms,sp_mv,pv_mv,ccr,err_mv,kp_milli,ki_milli,d,iout_ma,cascade,fault,output
 */
void APP_TELEMETRY_Push(const app_telemetry_frame_t *frame)
{
  char line[APP_TELEMETRY_LINE_MAX];
  uint8_t pos = 0U;
  int32_t err_mv;

  if (frame == 0)
  {
    return;
  }

  /* 回绕安全的节拍判断：t_us 为 32 位，71 分钟回绕一次 */
  if ((uint32_t)(frame->t_us - s_last_us) < APP_TELEMETRY_PERIOD_US)
  {
    return;
  }

  s_last_us = frame->t_us;
  err_mv = frame->sp_mv - frame->pv_mv;

  pos = app_telemetry_u32(line, pos, frame->t_us / 1000U);
  line[pos] = ',';
  pos++;
  pos = app_telemetry_i32(line, pos, frame->sp_mv);
  line[pos] = ',';
  pos++;
  pos = app_telemetry_i32(line, pos, frame->pv_mv);
  line[pos] = ',';
  pos++;
  pos = app_telemetry_u32(line, pos, frame->ccr);
  line[pos] = ',';
  pos++;
  pos = app_telemetry_i32(line, pos, err_mv);
  line[pos] = ',';
  pos++;
  pos = app_telemetry_u32(line, pos, frame->kp_milli);
  line[pos] = ',';
  pos++;
  pos = app_telemetry_u32(line, pos, frame->ki_milli);
  line[pos] = ',';
  pos++;
  line[pos] = '0';   /* d 项恒 0：本设计 kd=0 */
  pos++;
  line[pos] = ',';
  pos++;
  pos = app_telemetry_i32(line, pos, frame->iout_ma);
  line[pos] = ',';
  pos++;
  pos = app_telemetry_u32(line, pos, frame->cascade);
  line[pos] = ',';
  pos++;
  pos = app_telemetry_u32(line, pos, frame->fault);
  line[pos] = ',';
  pos++;
  pos = app_telemetry_u32(line, pos, frame->output);
  line[pos] = '\n';
  pos++;

  (void)DRV_RTT_Write(line, pos);
}
