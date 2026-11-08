/*****************************************************************************
 * @文件: tune_log.c
 * @作者: cl
 * @日期: 2026-11-07
 * @版本: v1.2
 * @说明: 调参采样：2ms 打印外环/内环 CSV；从 RTT 下行接收命令控制输出/档位/增益/启动提速
 * @平台: STM32F103C8T6
 * @依赖: app_control（参考/反馈/下发值）、service_dbg（RTT 收发）
 * @注意: 仅调参版启用（CL_TUNE_LOG=1）；生产版该宏必须为 0
 ****************************************************************************/
#include "config.h"

#if CL_TUNE_LOG

#include "tune_log.h"
#include "app_control.h"
#include "service_dbg.h"
#include "main.h"

/* 采样周期：2ms 一帧（500Hz）——50ms 会平滑掉内环瞬态峰谷
 * @来源 details/pid.md 判据契约（窗口约 100 样本 × 50ms = 5s） */
#define TUNE_LOG_PERIOD_MS      2U

/* 命令缓冲：RTT 下行缓冲只有 16B，主机按行短命令分多次发
 * @来源 通用知识：SEGGER_RTT_ConfDefaults.h BUFFER_SIZE_DOWN=16 */
#define TUNE_CMD_MAX            32U

static uint32_t s_t0;
static uint32_t s_last_ms;
static char     s_cmd[TUNE_CMD_MAX];
static uint32_t s_cmd_len;
static float    s_kp_v;
static float    s_ki_v;
static float    s_kp_i;
static float    s_ki_i;
static float    s_boot_mult;
static uint32_t s_lag_mv;

/* ==== 私有函数（极简解析，避免引入 string/scanf 的开销） ==== */

/**
 * @说明: 判断 s 是否以 p 开头
 */
static uint8_t tune_has_prefix(const char *s, const char *p)
{
  while (*p != '\0')
  {
    if (*s != *p)
    {
      return 0U;
    }
    s++;
    p++;
  }
  return 1U;
}

/**
 * @说明: 解析无符号十进制整数（遇到非数字停止）
 */
static uint32_t tune_parse_u32(const char *s)
{
  uint32_t v = 0U;

  while ((*s >= '0') && (*s <= '9'))
  {
    v = (v * 10U) + (uint32_t)(*s - '0');
    s++;
  }
  return v;
}

/**
 * @说明: 解析非负定点小数（如 0.15），最多 6 位小数
 */
static float tune_parse_f32(const char *s)
{
  float v = 0.0f;
  float frac = 0.1f;

  while ((*s >= '0') && (*s <= '9'))
  {
    v = (v * 10.0f) + (float)(*s - '0');
    s++;
  }
  if (*s == '.')
  {
    s++;
    while ((*s >= '0') && (*s <= '9'))
    {
      v += frac * (float)(*s - '0');
      frac *= 0.1f;
      s++;
    }
  }
  return v;
}

/**
 * @说明: 打印当前输出状态与增益（增益 ×10000 以整数回显）
 */
static void tune_report(void)
{
  SERVICE_DBG_Printf("[TUNE] out=%u sp=%lumV vout=%lumV iout=%umA iref=%umA\r\n",
                     (unsigned)APP_CONTROL_GetOutputEnabled(),
                     (unsigned long)APP_CONTROL_GetVrefTargetMv(),
                     (unsigned long)APP_CONTROL_GetVoutMv(),
                     (unsigned)APP_CONTROL_GetIoutMa(),
                     (unsigned)APP_CONTROL_GetIrefMa());
  SERVICE_DBG_Printf("[TUNE] gains x10000: kp_v=%d ki_v=%d kp_i=%d ki_i=%d\r\n",
                     (int)(s_kp_v * 10000.0f + 0.5f),
                     (int)(s_ki_v * 10000.0f + 0.5f),
                     (int)(s_kp_i * 10000.0f + 0.5f),
                     (int)(s_ki_i * 10000.0f + 0.5f));
  SERVICE_DBG_Printf("[TUNE] boot: ki_mult x100=%d lag=%lumV\r\n",
                     (int)(s_boot_mult * 100.0f + 0.5f),
                     (unsigned long)s_lag_mv);
}

/**
 * @说明: 执行一条命令（不带换行）
 * @输入: cmd 命令串
 * @注意: 未知命令只回显 ERR，不改变任何状态
 */
static void tune_exec(const char *cmd)
{
  uint32_t val;

  if (cmd[0] == '?')
  {
    tune_report();
  }
  else if (tune_has_prefix(cmd, "OUT:"))
  {
    val = tune_parse_u32(cmd + 4);
    APP_CONTROL_SetOutput((val != 0U) ? 1U : 0U);
    SERVICE_DBG_Printf("[TUNE] OUT=%lu\r\n", (unsigned long)val);
  }
  else if (tune_has_prefix(cmd, "SP:"))
  {
    val = tune_parse_u32(cmd + 3);
    APP_CONTROL_SetVref(val);
    SERVICE_DBG_Printf("[TUNE] SP=%lumV\r\n", (unsigned long)val);
  }
  else if (tune_has_prefix(cmd, "KPI:"))
  {
    s_kp_i = tune_parse_f32(cmd + 4);
    APP_CONTROL_SetGains(s_kp_v, s_ki_v, s_kp_i, s_ki_i);
    tune_report();
  }
  else if (tune_has_prefix(cmd, "KII:"))
  {
    s_ki_i = tune_parse_f32(cmd + 4);
    APP_CONTROL_SetGains(s_kp_v, s_ki_v, s_kp_i, s_ki_i);
    tune_report();
  }
  else if (tune_has_prefix(cmd, "KP:"))
  {
    s_kp_v = tune_parse_f32(cmd + 3);
    APP_CONTROL_SetGains(s_kp_v, s_ki_v, s_kp_i, s_ki_i);
    tune_report();
  }
  else if (tune_has_prefix(cmd, "KI:"))
  {
    s_ki_v = tune_parse_f32(cmd + 3);
    APP_CONTROL_SetGains(s_kp_v, s_ki_v, s_kp_i, s_ki_i);
    tune_report();
  }
  else if (tune_has_prefix(cmd, "BM:"))
  {
    s_boot_mult = tune_parse_f32(cmd + 3);
    APP_CONTROL_SetBootTune(s_boot_mult, s_lag_mv);
    tune_report();
  }
  else if (tune_has_prefix(cmd, "BL:"))
  {
    s_lag_mv = tune_parse_u32(cmd + 3);
    APP_CONTROL_SetBootTune(s_boot_mult, s_lag_mv);
    tune_report();
  }
  else
  {
    SERVICE_DBG_Printf("[TUNE] ERR unknown cmd\r\n");
  }
}

/* ==== 接口函数 ==== */

/**
 * @说明: 记录起点、读当前增益、打印命令表
 * @注意: 在 APP_CONTROL_Init 之后调用
 */
void TUNE_LOG_Init(void)
{
  s_t0 = HAL_GetTick();
  s_last_ms = s_t0;
  s_cmd_len = 0U;
  APP_CONTROL_GetGains(&s_kp_v, &s_ki_v, &s_kp_i, &s_ki_i);
  APP_CONTROL_GetBootTune(&s_boot_mult, &s_lag_mv);

  SERVICE_DBG_Printf("[TUNE] csv begin; cmds: OUT:1|0 SP:<mV> KP: KI: KPI: KII: BM: BL: ?\r\n");
  tune_report();
}

/**
 * @说明: 主循环调用——解析 RTT 下行命令，并 50ms 打印两路 CSV
 * @来源 pid-tune/references/interface.md §八：csv8 = t,setpoint,input,pwm,error,p,i,d
 * @注意: 外环 sp=vref(mV)/pv=vout(mV)；内环 sp=iref(mA)/pv=iout(mA)；p/i/d 本工程不外传，填 0
 */
void TUNE_LOG_Loop(void)
{
  uint32_t now = HAL_GetTick();
  char rx[16];
  uint32_t n;
  uint32_t i;

  /* ==== 命令接收（按行） ==== */
  n = SERVICE_DBG_Read(rx, sizeof(rx));
  for (i = 0U; i < n; i++)
  {
    char ch = rx[i];

    if (ch == '\r')
    {
      continue;
    }
    if (ch == '\n')
    {
      s_cmd[s_cmd_len] = '\0';
      if (s_cmd_len > 0U)
      {
        tune_exec(s_cmd);
      }
      s_cmd_len = 0U;
      continue;
    }
    if (s_cmd_len < (TUNE_CMD_MAX - 1U))
    {
      s_cmd[s_cmd_len] = ch;
      s_cmd_len++;
    }
    else
    {
      s_cmd_len = 0U;   /* 超长丢弃，等下一个换行重新同步 */
    }
  }

  /* ==== 50ms CSV ==== */
  if ((uint32_t)(now - s_last_ms) < TUNE_LOG_PERIOD_MS)
  {
    return;
  }
  s_last_ms = now;

  /* 外环：电压跟踪（pid-tune 用 sp/pv 算超调/下冲/稳态）
   * sp 记「档位目标」而不是斜坡值：阶跃点清晰，pid-tune 才不会被斜坡中间值污染 */
  SERVICE_DBG_Printf("[OUT] %lu,%lu,%lu,%u,%ld,0,0,0\r\n",
                     (unsigned long)(uint32_t)(now - s_t0),
                     (unsigned long)APP_CONTROL_GetVrefTargetMv(),
                     (unsigned long)APP_CONTROL_GetVoutMv(),
                     (unsigned)APP_CONTROL_GetCcr1(),
                     (long)APP_CONTROL_GetVrefTargetMv() - (long)APP_CONTROL_GetVoutMv());

  /* 内环：电流跟踪（sp=iref 外环输出） */
  SERVICE_DBG_Printf("[IN] %lu,%u,%u,%u,%ld,0,0,0\r\n",
                     (unsigned long)(uint32_t)(now - s_t0),
                     (unsigned)APP_CONTROL_GetIrefMa(),
                     (unsigned)APP_CONTROL_GetIoutMa(),
                     (unsigned)APP_CONTROL_GetCcr1(),
                     (long)APP_CONTROL_GetIrefMa() - (long)APP_CONTROL_GetIoutMa());
}

#endif /* CL_TUNE_LOG */
