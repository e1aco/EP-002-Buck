# ADC 采样链（TIM4_CC4 触发 + DMA 循环）
ADC_CH0 = IN0/PA0 输出电压 VCC_OUT-VSS_OUT  @Buck.ioc 规则组 Rank2  #代码核对  2026-10-28
ADC_CH1 = IN1/PA1 输出电流（25mΩ 分流）  @Buck.ioc 规则组 Rank1（先采）  #代码核对  2026-10-28
ADC_SAMPLE_POINT = 电流 Rank1 触发即采、电压 Rank2（+2.167µs）  @规则组换序推导  #推导  2026-10-28
ADC_SMP = 13.5 cycles/ch  @Core/Src/adc.c:62  #代码核对  2026-10-23
ADC_CONV = 2.167µs/ch  @(13.5+12.5)/12MHz  #推导  2026-10-23
ADC_SCAN = 4.333µs（2ch）  @2×ADC_CONV  #推导  2026-10-23
ADC_TRIG = TIM4_CC4 @2kHz，TIM4 CNT 零点锁定 PA8 上升沿  @adc.c:50 + tim.c SMS=100  #实测  2026-10-29
ADC_TRIG_PHASE = 导通中点前移 1.25µs（PB9 下降沿 = 触发沿）  @LA 实测 50%→11.25µs  #实测  2026-10-28
ADC_TRIG_CENTER = ≥15% 恰落导通中点；下限由 DRV_PWM_DUTY_CNT_MIN=540 强制  @扫描+guard 实测  #实测  2026-10-28
ADC_TRIG_MID = (CCR1+HIN_RISE)/2，HIN_RISE=216 计数（3.0µs）  @PWM_HIN_RISE  #推导  2026-10-30
ADC_CCR4_50PCT = 91  @(1800+216)/20−9，实测读回  #实测  2026-10-30
ADC_VGAIN_VERIFY = 实测 7.44V ↔ 读数 7.37V（误差 1%）→ 分压未焊错  @万用表+raw_v  #实测  2026-10-30
ADC_CCR4_PRELOAD = OC4PE=1（与 CCR1 同 UEV 提交，改占空比不脱钩）  @CCMR2  #代码核对  2026-10-30
ADC_COMP_IDEAL = 待实测（估 2.3µs；现用 1.25µs）  @t_latr+SMP 1.125µs  #待实测确认  2026-10-29
ADC_TRIG_CLAMP_EDGE = 15% 处 CCR4=18 与 CCR1/20-9=18 连续（下限无跳变）  @推演链  #推导  2026-10-28
ADC_DMA = DMA1_Ch1 circular 半字  @Core/Src/adc.c:105-111  #代码核对  2026-10-23
ADC_DMA_IT = TC|TE（HT 已显式关闭，避免半扫描空中断）  @module/drv/drv_adc.c  #代码核对  2026-10-28
V_GAIN = 0.18333 V/V  @原理图v1.0 R17/R18=12K→R19/R20=2.2K 差分  #推导  2026-10-24
V_FS = 18.0V  @3.3V/V_GAIN  #推导  2026-10-24
V_LSB = 4.395mV  @3.3/4096/V_GAIN（已被 ADC_CAL_V 取代）  #推导  2026-10-24
I_GAIN = 20 V/V  @原理图v1.0 R11/R12=1K→R13/R14=20K 差分  #推导  2026-10-24
I_SHUNT = 25mΩ  @原理图v1.0 R1（VSS_OUT 回线，原 50mΩ）  #推导  2026-10-24
I_SCALE = 0.5 V/A  @I_GAIN×I_SHUNT = 20×0.025（已被 ADC_CAL_I 取代）  #推导  2026-10-24
I_FS = 6.6A  @3.3V/I_SCALE  #推导  2026-10-24
I_LSB = 1.611mA  @3.3/4096/I_SCALE（已被 ADC_CAL_I 取代）  #推导  2026-10-24
ADC_CAL_V = 4.423mV/码（raw×4423/1000）  @三点 1076/4.76V、1885/8.35V、2934/12.97V  #实测  2026-11-04
ADC_CAL_I = 1.659mA/码（raw×1659/1000）  @三点 282/0.476A、503/0.840A、790/1.304A  #实测  2026-11-04
V_GAIN_REAL = 0.1821 V/V（实测反推，比推导值低 0.64%）  @4.423mV/码 ÷ 0.8057mV/码  #实测  2026-11-04
I_SCALE_REAL = 0.4857 V/A（实测反推，比推导值低 2.9%）  @0.8057mV/码 ÷ 1.659mA/码  #实测  2026-11-04
ADC_CAL_RESID = 电压 ≤±0.06%、电流 ≤±0.7%（8.35V/12.97V 两点复核）  @RTT TELE vs 万用表  #实测  2026-11-04
ADC_CAL_DCM_BIAS = 0.476A（深 DCM）点偏低 1.7%  @采样点落在 DCM 斜坡上，非标度误差  #实测  2026-11-04
ADC_TRIG_INSENSITIVE = raw_i 对采样相位不敏感（±42µs 仅差 1 码）  @CCR4 40→300 实测，前端强低通  #实测  2026-11-04
ADC_CAL_V_4PT = 已作废（11-07 重标）→ 现行映射见 pwm.md DUTY_MAP_CAL  @旧 6.456mV×CCR1−1.517V  #实测  2026-11-04
ADC_SCAN_RATE_MEAS = 1000/s  @RTT n 每秒 +1000  #实测  2026-10-23
V_SCALE_CHK = 2397mV->13.074V  @实测 2397*60/11  #实测  2026-10-24
I_SCALE_CHK = 3299mV->6.598A  @实测 3299/0.5  #实测  2026-10-24
ADC_SCAN_MAX_SYS = 120kSPS  @中断计数法实测（160kSPS 时 SysTick 停摆）  #实测  2026-10-24
ADC_ISR_MAX = 6.3~8.3µs  @由可持续率 120~160kSPS 界定（含 HAL_DMA_IRQHandler）  #实测  2026-10-24
ADC_TCONV_DIRECT = 不可直测（无 EOC/触发输出脚）  @实测 2026-10-24  #待实测确认
ADC_VPIN_3V3 = 3299mV（raw=4095 满量程）  @注入 3.3V 实测  #实测  2026-10-23
ADC_IPIN_3V3 = 3299mV（raw=4095，通道2 同验）  @注入 3.3V 实测  #实测  2026-10-23
ADC_I_FLOAT_RAW = 1265->2905（随 PA0 注入漂移，浮空无效）  @实测  #待实测确认  2026-10-23

## 定论（已否决 / 已解决 · 勿重开）
ADC_TRIG_ASYNC = 已解决: TIM4/TIM1 异步采样（随机相位混纹波）改从模式复位  依据: LA 偏差 0ns  日期: 2026-10-28  来源: 实测
ADC_TRIG_FOLLOW = 已解决: 改占空比需补调 → 收进 DRV_PWM_SetDutyCount  依据: 7 档扫描  日期: 2026-10-28  来源: 实测
