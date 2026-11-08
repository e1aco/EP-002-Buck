/*****************************************************************************
 * @文件: drv_pwm.h
 * @作者: cl
 * @日期: 2026-10-30
 * @版本: v2.0
 * @说明: 双通道互补 PWM 驱动接口（PA8=TIM1_CH1 外脉冲→LIN#，PA7=TIM3_CH2 内脉冲→HIN）
 * @平台: STM32F103C8T6
 ****************************************************************************/
#ifndef __DRV_PWM_H
#define __DRV_PWM_H

#include <stdint.h>

/* 占空比计数标度：直接写 TIM1_CH1 的 CCR1 计数值（1 LSB = 1/3600 = 0.0278%）
 * 有效范围 0..3599（=ARR）；上限由函数按运行时 ARR 截断
 * 50.00% = 1800 计数（推导：CCR1 = (ARR+1)/2 = 3600/2）
 * @来源 memory TIM1_PWM_FREQ=20kHz 推导：72MHz/((0+1)*(3599+1))=20kHz */
#define DRV_PWM_DUTY_CNT_50PCT    1800U

/* 驱动拓扑：1 = 单输入（HIN 与 LIN# 同相），死区由 EG2131 内部互锁产生
 *           0 = 双通道（外脉冲 LIN# + 内脉冲 HIN），死区由 CCR2 内缩 DT 产生
 * 单输入实现：两个定时器输出同一宽度波形（PA7=TIM3_CH2、PA8=TIM1_CH1 同相），
 *   驱动芯片看到 HIN=LIN# → HO=HIN&LIN、LO=!HIN&!LIN → 互补 + 内部死区，无需改硬件接线
 * 回退：把本宏改 0（或 git checkout cl/dt36-verified -- module/）
 * @来源 memory EG2131_TRUTH（HIN=LIN# 时互补）、EG2131_DT_INT=250ns；用户 2026-11-08 要求 */
#define DRV_PWM_SINGLE_INPUT      1

#if (DRV_PWM_SINGLE_INPUT != 0)
/* 死区 = 0：HIN=LIN# 同相，EG2131 内部互锁（250ns 典型 + 闭锁）保证输出不共导
 * 导通关系：HIN=LIN#=1 → HO 开；HIN=LIN#=0 → LO 开；切换沿由芯片插死区
 * 实测预期：栅极两沿 ≈ 内置 250ns + 驱动/开关延时（须重新实测两沿 + 电流爬升）
 * @来源 memory EG2131_DT_INT=250ns（典型）+ 闭锁：输出不可能同时导通 */
#define DRV_PWM_DEADTIME_CNT      0U
#else
/* 死区：0.50µs / 13.889ns = 36 计数（两侧相同，由内脉冲两端各缩 DT 产生）
 * 内脉冲 = CCR1 − 2×DT → 上管导通窗 = [DT, CCR1−DT)，两侧死区都等于 DT
 * 历史（不删）：2026-10-30 实测 DT=36/200 时输入电流 50~110mA 持续爬升、Q1 发烫，
 *   DT≥220 才稳定在 28mA → 当时判定"存在上下管重叠导通，需 ~3µs 死区"。
 * 2026-11-08 复测：该结论形成于 CCR2 补写 bug（2026-11-04 修复）之前；
 *   逐档实测 —— 150→2.24/2.23µs、100→1.56/1.56µs、36→725/702ns，电流 5min 均不爬升。
 * 代价：2×DT 计数被死区吃掉（220→440=12.2%，100→200=5.6%，36→72=2.0%）
 * @来源 memory PWM_DT=36（2026-11-08 验收），历史见 memory PWM_DT_LADDER */
#define DRV_PWM_DEADTIME_CNT      36U
#endif

/* 占空比上限：3400 计数 = 94.4%
 * 自举电容只能在下管导通期间充电：下管窗 = T − CCR1 + DT
 *   CCR1=3400 → 下管 308 计数 = 4.3µs（充裕）
 *   CCR1=3599 → 下管 109 计数 = 1.5µs（偏紧）
 * 本项目 24V→12V 只需 50%，此限幅主要防瞬态/调试误设
 * @来源 memory PWM_DT_RULE 推导：下管窗 = 3600 − CCR1 + DT ≥ 4µs → CCR1 ≤ 3400 */
#define DRV_PWM_DUTY_CNT_MAX      3400U

/* 占空比下限：15% = 540 计数 + 2×DT（双通道时内脉冲两端各缩 DT）
 * 有效占空比 = (CCR1−2×DT)/3600，要保住 15% 须 CCR1 = 540 + 2×DT（DT=0 → 540）
 * 取 0 不会输出 0V（要停波须走 DRV_PWM_Stop 关 MOE）
 * @来源 memory ADC_TRIG_CENTER：夹取下限 → 触发点恒为导通中点 */
#define DRV_PWM_DUTY_CNT_MIN      (540U + 2U * DRV_PWM_DEADTIME_CNT)

/* 启动期可放开的地板：双通道 = 2×DT（内脉冲 = CCR1−2×DT = 0，防 uint16 下溢）；
 * 单输入 = 0（CCR2 = CCR1，不存在下溢，0 即"下管常通、输出 0V"） */
#if (DRV_PWM_SINGLE_INPUT != 0)
#define DRV_PWM_DUTY_CNT_BOOT_MIN 0U
#else
#define DRV_PWM_DUTY_CNT_BOOT_MIN (2U * DRV_PWM_DEADTIME_CNT)
#endif

void DRV_PWM_Init(void);
/* 设定占空比并同步重算 ADC 触发相位（内部调 DRV_ADC_TrigFollowPwm，上层无需配对调用）
 * 入参越界按 [当前下限, DRV_PWM_DUTY_CNT_MAX] 截断；当前下限默认 DRV_PWM_DUTY_CNT_MIN，
 * 启动期可由 DRV_PWM_SetDutyMin 临时放开到 DRV_PWM_DUTY_CNT_BOOT_MIN；
 * 要停波请用 DRV_PWM_Stop */
void DRV_PWM_SetDutyCount(uint16_t count);

/**
 * @说明: 设置正常运行的下限（启动期把地板放开到 2×DT，让软启动能从 0V 建压）
 * @输入: count 期望下限；内部夹到 [DRV_PWM_DUTY_CNT_BOOT_MIN, DRV_PWM_DUTY_CNT_MIN]
 * @注意: 只约束 SetDutyCount，不影响 DRV_PWM_Stop（安全态仍是 CCR1=CCR2=0）；
 *        闭环在 ISR 内每拍按软启动阶段调用，无需上层保存状态
 * @来源 memory PWM_DT=220 推导：地板 < 2×DT 会让 CCR2 下溢
 */
void DRV_PWM_SetDutyMin(uint16_t count);

/**
 * @说明: 停波（安全态）：外脉冲 CCR1=0 → PA8(LIN#) 恒低 = 下管常通；
 *        内脉冲 CCR2=0 → PA7(HIN) 恒低 = 上管关断。输出不再开关，电感经下管续流
 * @注意: 与 SetDutyCount(0) 不同——后者会被截断到 DRV_PWM_DUTY_CNT_MIN（下限是
 *        **控制范围**约束，不是安全态）；故障/失联必须走本函数
 * @来源 memory EG2131 真值表 HO=HIN&LIN#、LO=!HIN&!LIN#：HIN=0,LIN#=0 → 仅下管开
 */
void DRV_PWM_Stop(void);
/* 主循环调用：补写"等一拍才安全"的内脉冲（见 .c 里 SetDutyCount 的安全限幅说明）
 * 闭环 2kHz 连续调用时由下一次 SetDutyCount 自然跟上，本函数只在单次调用后生效 */
void DRV_PWM_Loop(void);

#endif /* __DRV_PWM_H */
