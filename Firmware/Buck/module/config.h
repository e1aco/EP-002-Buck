/*****************************************************************************
 * @文件: config.h
 * @作者: cl
 * @日期: 2026-10-28
 * @版本: v1.1
 * @说明: 跨层编译期配置（调试后端选择 / 遥测周期 / 验收开关）
 * @注意: 只放编译期特性开关与版本，外设参数一律放 .cl/memory/
 ****************************************************************************/
#ifndef __CONFIG_H
#define __CONFIG_H

/* 调试后端（互斥，同窗口只能用一支）
 * 0 = J-Link RTT：走 SWD 调试口，不占串口，需 JLinkRTTClient 读
 * 1 = USART1：115200 8N1，PA9/PA10 外接 USB-TTL
 * @来源 memory UART1_BAUD=115200 8N1（usart.c 已由 CubeMX 配好） */
#define CL_DBG_BACKEND_RTT     0
#define CL_DBG_BACKEND_UART    1
#define CL_DBG_BACKEND         CL_DBG_BACKEND_RTT

/* 遥测打印周期（ms）：ADC 2kHz 采样 -> 打印降频 1Hz，避免刷屏与 RTT 缓冲溢出
 * @来源 memory TIM4_TRIG_FREQ=2kHz（采样节拍）-> 每 500µs 采一次，打印每 1s 一次 */
#define CL_TELE_PERIOD_MS      1000U

/* 时序探针开关（/cl tim 宏版置 1，测完必须恢复 0）
 * @来源 details/probe.md 双版本构建：用取值判定，不用 #ifdef */
#ifndef CL_TIMING_MEASURE
#define CL_TIMING_MEASURE      0
#endif /* CL_TIMING_MEASURE */

/* ADC 转换时间标定（一次性扫描，仅 /cl tim 用；生产版 0 = 空宏零开销） */
#ifndef CL_ADC_CALIB
#define CL_ADC_CALIB           0
#endif /* CL_ADC_CALIB */

/* 开环占空比扫描（验收用：逐档改占空比并复核 ADC 触发相位；生产版 0）
 * @来源 details/probe.md 双版本构建：用取值判定，不用 #ifdef */
#ifndef CL_PWM_SWEEP
#define CL_PWM_SWEEP           0
#endif /* CL_PWM_SWEEP */

/* 闭环控制结构选择（0 = 单环电压；1 = 双环：电压外环 + 电流内环）
 * 两种模式共用 module/app/app_control.c 里的同一个 APP_PID_T 内核
 * @来源 details/codegen-domain.md「新增编译期特性开关」：取值判定，不用 #ifdef 存在性 */
#define CL_CTRL_MODE_VOLT_ONLY 0
#define CL_CTRL_MODE_VOLT_CURR 1

/* 默认级联双环（电压外环 → 电流内环），电流参考上限 3300mA
 * 单环仍保留：把下行改成 CL_CTRL_MODE_VOLT_ONLY 即可回退（两套增益分别定义）
 * @来源 你（2026-11-06 加入电流环） */
#ifndef CL_CTRL_MODE
#define CL_CTRL_MODE           CL_CTRL_MODE_VOLT_CURR
#endif /* CL_CTRL_MODE */

/* 闭环使能（0 = 开环固定占空比，main 里 SetDutyCount 生效；1 = 闭环接管 CCR1） */
#ifndef CL_CTRL_ENABLE
#define CL_CTRL_ENABLE         1
#endif /* CL_CTRL_ENABLE */

/* 调参采样（50ms CSV 经调试口输出 + 自动阶跃序列；生产版必须 0）
 * 打开后：2s 开输出 5V → 6s 切 12V → 11s 回 5V → 16s 关输出 → 20s 停止打印
 * 上行格式对齐 pid-tune csv8：t_ms,setpoint,input,pwm,error,p,i,d
 * @来源 details/pid.md；pid-tune/references/interface.md §八 */
#ifndef CL_TUNE_LOG
#define CL_TUNE_LOG            0
#endif /* CL_TUNE_LOG */

/* ADC 采样标定（三点开环扫描，每档 15s；生产版 0）
 * 与开环扫描/闭环互斥：标定必须跑开环，闭环会把 CCR1 抢回去
 * @来源 details/build.md「双版本宏注入」兜底行：无 Timing Target 时改本宏 -> 构建 -> 恢复 */
#ifndef CL_ADC_CAL_SWEEP
#define CL_ADC_CAL_SWEEP       0
#endif /* CL_ADC_CAL_SWEEP */

#if (CL_ADC_CAL_SWEEP && CL_PWM_SWEEP)
#error "CL_ADC_CAL_SWEEP and CL_PWM_SWEEP are mutually exclusive"
#endif
#if (CL_ADC_CAL_SWEEP && CL_CTRL_ENABLE)
#error "CL_ADC_CAL_SWEEP needs CL_CTRL_ENABLE=0 (open loop)"
#endif

#endif /* __CONFIG_H */
