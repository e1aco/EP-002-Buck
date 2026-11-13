# PWM 与采样节拍（TIM1 功率级 / TIM4 触发）
TIM1_PSC = 0  @Core/Src/tim.c:47；Buck.ioc TIM1.Prescaler=0  #代码核对  2026-10-28
TIM1_ARR = 3599  @Core/Src/tim.c:49；Buck.ioc TIM1.Period=3599  #代码核对  2026-10-28
TIM1_PWM_FREQ = 20kHz  @72MHz/(0+1)/(3599+1)=20kHz（PSC/ARR 改动后频率不变）  #推导  2026-10-28
TIM1_PWM_RES = 3600 计数/周期（1 计数 = 1/3600 = 0.0278%）  @ARR+1  #推导  2026-10-28
TIM1_DEADTIME = 0（死区交 EG2131 内部产生）  @BDTR 实测 0xA000: MOE=1,DTG=0  #实测  2026-10-23
TIM1_CCR1_50PCT = 1800  @(ARR+1)/2 = 3600/2  #实测  2026-10-28（SWD 读回 0x708）
TIM1_DUTY_MAX = 3599（=ARR；CCR≥ARR+1=3600 恒高，非 PWM）  @ARR  #推导  2026-10-28
TIM1_DUTY_API = DRV_PWM_SetDutyCount(540..3599)，越界双向截断  @module/drv/drv_pwm.c  #代码核对  2026-10-28
TIM1_DUTY_MIN = 540+2×DT（单输入→540；双通道 DT=36→612）  @DRV_PWM_DUTY_CNT_MIN  #推导  2026-11-08
TIM1_DUTY_SYNC = SetDutyCount 内自动重算 ADC 触发相位  @drv_pwm.c v1.3  #代码核对  2026-10-28
TIM1_RCR = 9（UEV/TRGO 10 分频后 2kHz；PWM 输出仍 20kHz）  @tim.c:51 RCR  #代码核对  2026-10-29
TIM1_TRGO = UPDATE（CR2.MMS=010）  @tim.c 实测 CR2=0x20  #实测  2026-10-28
PA8_PWM_MEAS = 20.0000kHz，占空比 1/5/10/25/50/75/90% 全档一致  @逻辑分析仪 CH5 24MS/s 实测  #实测  2026-10-28
TIM4_SYNC = 从模式复位 SMS=100/TS=ITR0，CNT 零点 = PA8 上升沿  @tim.c TIM4_Init 2 实测 SMCR=0x4  #实测  2026-10-28
TIM4_CCR4_RULE = CCR4 = CCR1/20 - 9（<27 时夹 18）  @TIM1 计数 10×细于 TIM4 + 1.25µs 补偿  #推导  2026-10-28
TIM4_CCR4_50PCT = 81（11.25µs）  @1800/20-9 实测 PB9 高 11.25µs  #实测  2026-10-28
TIM4_CCR4_SWEEP = 1/5/10/25/50/75/90% → 18/18/18/36/81/126/153  @LA CH7 实测  #实测  2026-10-28
TIM4_TRIG_FLOOR = 27 计数 = 3.75µs（下限 18 + 补偿 9）  @扫描实测 PB9 高最小 2.50µs  #实测  2026-10-28
TIM4_PSC = 9  @Core/Src/tim.c:116（10-1）  #代码核对  2026-10-23
TIM4_ARR = 3599  @Core/Src/tim.c:119（3600-1）  #代码核对  2026-10-29
TIM4_TRIG_FREQ = 2kHz  @72MHz/(9+1)/(3599+1)→ADC 外部触发 T4_CC4  #推导  2026-10-29
TIM4_ADV_MEAS = 1250ns 恒（15/50/90% @2kHz：PA8高/2 − PB9高）  @LA CH5/CH7  #实测  2026-10-29
EG2131_TRUTH = HO=HIN&LIN#、LO=!HIN&!LIN#（(1,0)/(0,1) 均双关）  @你截图  #datasheet  2026-10-29
EG2131_VIH = 2.8V（HIN 高电平阈值，内置 200k 下拉）  @联网 egmicro 手册V1.0  #datasheet  2026-10-30
EG2131_VIL = 1.5V（LIN 低电平阈值，内置 5V 上拉）  @同上  #datasheet  2026-10-30
EG2131_TON = 400ns(HO)/410ns(LO)、TOFF = 150ns(HO)/140ns(LO)  @同上  #datasheet  2026-10-30
EG2131_DT_INT = 250ns（内置死区典型）+ 闭锁，输出不可能同时导通  @同上  #datasheet  2026-10-30
EG2131_FMAX = 500kHz、VCC 11–20V、UVLO 9.4/8.8V、IO 1A/1.5A  @同上  #datasheet  2026-10-30
PWM_TOPO = 单输入：PA8→LIN#、PA7→HIN 同相同宽 20kHz，死区全交 EG2131；开关 DRV_PWM_SINGLE_INPUT=1  @你  #你  2026-11-08
PWM_DT = 0（单输入：两路同相同宽，无软件死区）  @DRV_PWM_SINGLE_INPUT=1；双通道定档 36 见 PWM_DT_LADDER  #实测  2026-11-08
PWM_DT_MEAS = 249ns（单输入，量栅极）= EG2131 内置死区 250ns  @示波器栅极  #实测  2026-11-08（史：DT=36→725ns）
PWM_DT_LADDER = 栅极两沿：150→2.24µs、100→1.56µs、36→0.72µs；附加 0.17µs，电流不爬升  @示波器  #实测  2026-11-08
PWM_INNER_CCR2 = 单输入 CCR2=CCR1；双通道 CCR2=CCR1−2×DT  @drv_pwm.c SetDutyCount  #代码核对  2026-11-08
PWM_SINGLE_IMPL = TIM3 从模式 TRIGGER(ITR0)、CNT 预置 0；相位差仅触发 skew 1-2 计数  @Tim3Init  #代码核对  2026-11-08
PWM_HW_R10 = R10(HO 栅阻)=0Ω；4.7Ω 时上管关断慢于 250ns → 重叠；R2 保持 4.7Ω  @你实测  #实测  2026-11-08
PWM_CCM_ICRIT = 0.65A（ΔI/2，220µH@20kHz）→ 负载须 <14Ω 才进 CCM  @推导  #推导  2026-10-30
PWM_DT_OVERLAP = 作废: "DT=200 直通"源于 CCR2 补写 bug（11-04 修复）；修复后 DT=36 不爬升  @复测  #实测  2026-11-08
PWM_Q1_TOFF = 作废: "上管关断慢百倍/需 ~4µs 死区"不成立；实测死区 ≈ 下发 DT + 0.17µs  @同上  #实测  2026-11-08
PWM_LA_MAP = CH5=PA7(TIM3_CH2)、CH7=PA8(TIM1_CH1)  @50%/44% 与 CCR 对应  #实测  2026-10-30

## 待推导（缺 datasheet，写代码前补）AOD4126_QGD = 待推导  @缺 AOD4126 datasheet  #待实测确认  2026-10-23
AOD4126_RDSON = 待推导  @缺 AOD4126 datasheet  #待实测确认  2026-10-23
EG2131_IO_DRIVE = 待推导  @缺 EG2131 datasheet（死区/栅阻推导依赖）  #待实测确认  2026-10-23
L220UH_VAL = 220µH 50T 铁硅铝125  @datasheet/电感参数.jpg（80125-220uH 7A）  #datasheet  2026-10-23
L220UH_ISAT = 7A（厂标额定电流）  @datasheet/电感参数.jpg  #datasheet  2026-10-23
L220UH_DCR = 40mΩ  @datasheet/电感参数.jpg  #datasheet  2026-10-23
L220UH_RIPPLE = 1.36A（最大，Vout=12V/D=0.5）；5V 档 0.93A  @24×0.5×0.5/(220µ×20k)  #推导  2026-10-28
VOUT_MAX = 12V  @你（输出规格 5–12V/0–3A，2026-10-28 由 0–12V 改）  #你  2026-10-28
VOUT_MIN = 5V  @同上  #你  2026-10-28
VIN_NOM = 24V 恒定  @你  2026-10-28
DUTY_MAP = Vout=(CCR1−2×DT)/3600×24；单输入 DT=0 → CCR1/3600×24  @DUTY_MAP_CAL  #推导  2026-11-08
DUTY_MAP_CAL = CCR1≈0.15264×Vout[mV]+OFFSET；单输入 26.5，DT36→98.5  @10Ω 残差≤4mV  #实测  2026-11-08
DUTY_MAP_MEAS = 10Ω CCR1 1229.9/1534.7/1839.8/2145.1/2298.8（5/7/9/11/12V）  #实测  2026-11-07
DUTY_BOOT_MIN = 单输入 0（CCR1=0 即下管常通）；双通道 2×DT（DT36→72）  @DRV_PWM_DUTY_CNT_BOOT_MIN  #推导  2026-11-08
DUTY_FLOOR_SYNC = 三处同步：drv_pwm s_duty_min / ISR 钳位 / PID out_min，漏则失效  @app_control  #推导  2026-11-07
PWM_INNER_STALE = 已解决: 上调步长时 CCR2 停上一档  证据: CCR1=1520/CCR2=540→4.76V，修后8.35V  日期: 2026-11-04  来源: 实测
DUTY_WIN = CCR1 790–1858（空载 5–12V，DT=0）/ 819–1888（3A）；DT=36 整体 +72  @D=Vout/24V+2DT  #推导  2026-11-08
IOUT_MAX = 3A  @你（同上，2026-10-24 由 1A 上调）  #你  2026-10-24
IOUT_PEAK = 3.68A  @IOUT_MAX+L220UH_RIPPLE/2 = 3+0.68  #推导  2026-10-28
L_ISAT_MARGIN = 1.9x  @L220UH_ISAT 7A / IOUT_PEAK 3.68A  #推导  2026-10-28
R1_SHUNT_P = 0.225W  @IOUT_MAX^2*25mΩ = 3^2*0.025  #推导  2026-10-24
ADC_I_HEADROOM = 120%  @I_FS 6.6A vs IOUT_MAX 3A（余量 2.2x）  #推导  2026-10-24
VIN_RANGE = 24V 恒定  @你（2026-10-28 确认）  #你  2026-10-28
CTRL_MODE = 级联双环（V 外环→Iref、I 内环→CCR1）  @你（2026-11-06 加入电流环）  #你  2026-11-06
CTRL_IREF_MAX = 3300mA（3A+10%）  @你  2026-11-06
CTRL_KP_V = 0.02mA/mV、KI_V = 0.001/步（外环 10ms）  @推导 环路增益0.2@10Ω  #推导  2026-11-06
CTRL_KP_I = 0.15计数/mA、KI_I = 0.05/步（内环 1.5ms）  @推导 环路增益0.6@1.67Ω  #推导  2026-11-06
CTRL_KP_V_TUNED = 0.01、KI_V = 0.0005  @实测 0.02/0.001 时上切过冲 1.95%、回跌 5.2%  #实测  2026-11-06
CTRL_STEP_UP = 5→12V：峰 +1.47%、22ms 边沿、52ms 进 ±1%（10Ω/220µF，12mA/步）  @2ms RTT  #实测  2026-11-07
CTRL_STEP_DN = 12→5V：谷 −0.58%、24ms 边沿、114ms 进 ±1%（过渡带 500mV）  @2ms RTT  #实测  2026-11-07
CTRL_BOOT_5V = 0→5V：峰 +0.74%、30~32ms、74ms 进 ±1%、纹波 53mV（提速前 112/216ms）  @2ms RTT  #实测  2026-11-07
CTRL_BOOT_12V = 0→12V：峰 +0.33%、76ms、110ms 进 ±1%、纹波 62~75mV（提速前 294/400ms）  @2ms RTT  #实测  2026-11-07
CTRL_BOOT_DIP = 已解决：斜坡按「输出跟随」门控（LAG=800mV）→ 单调升压无过冲；慢代价由 KI_MULT 收回  @app_control.c  #实测  2026-11-07
CTRL_RAMP_LAG = 800mV  @10Ω：400mV→290/660ms 太慢；预置 i_acc→过冲 +5.6%；按距离缩放→尾部 626ms  #实测  2026-11-07
CTRL_BOOT_KI_MULT = 4（外环 KI×4，vout 距目标 >50mV 时生效）  @扫参 3/4/5/6→40/30/24/24ms  #实测  2026-11-07
CTRL_STEP_UP_V2 = 5→12V：峰 +1.54%、22ms 边沿、50ms 进 ±1%（换档时禁 BOOST_MULT）  @2ms RTT  #实测  2026-11-07
CTRL_STEP_DN_V2 = 12→5V：谷 −0.32~−0.76%、22ms 边沿、128ms 进 ±1%（不触发 boost）  @2ms RTT  #实测  2026-11-07
CTRL_SAMPLE_MS = 50ms 采样会漏峰（真实 12.74V/3.35V），瞬态评估须 2ms  @实测  #实测  2026-11-06
