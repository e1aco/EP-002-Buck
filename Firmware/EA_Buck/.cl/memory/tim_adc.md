# TIM/ADC 配置（.ioc + Core/Src 推导）

TIM1_PSC = 0  @EA_Buck.ioc TIM1.IPParameters  #CubeMX  2026-11-12
TIM1_ARR = 3599  @EA_Buck.ioc TIM1.Period  #CubeMX  2026-11-12
TIM1_RCR = 1  @EA_Buck.ioc TIM1.RepetitionCounter  #CubeMX  2026-11-12
TIM1_PWM_FREQ = 20kHz = 72MHz/(0+1)/(3599+1)  @CLOCK_APB2_TIM/TIM1_*  #推导  2026-11-12
TIM1_TRGO = UPDATE 10kHz = PWM/(RCR+1)  @Core/Src/tim.c:66  #推导  2026-11-12
BUCK_FSW = 20kHz(T1 PWM)，ADC 触发 10kHz  @TIM1_PWM_FREQ  #你  2026-11-12
TIM4_PSC = 9  @EA_Buck.ioc TIM4.Prescaler  #CubeMX  2026-11-12
TIM4_ARR = 719  @EA_Buck.ioc TIM4.Period  #CubeMX  2026-11-12
TIM4_CC4_FREQ = 10kHz = 72MHz/(9+1)/(719+1)  @CLOCK_APB1_TIM/TIM4_*  #推导  2026-11-12
TIM4_SYNC = Reset/ITR0 from TIM1 TRGO  @Core/Src/tim.c:135  #CubeMX  2026-11-12
ADC1_RANK1 = IN1/PA1/ADC_I 采样 13.5cyc  @EA_Buck.ioc ADC1.*  #CubeMX  2026-11-12
ADC1_RANK2 = IN0/PA0/ADC_V 采样 13.5cyc  @EA_Buck.ioc ADC1.*  #CubeMX  2026-11-12
ADC1_TRIG = TIM4_CC4  @EA_Buck.ioc ADC1.ExternalTrigConv  #CubeMX  2026-11-12
ADC1_CONV = 2*(13.5+12.5)/12MHz = 4.33us  @CLOCK_ADC/ADC1_*  #推导  2026-11-12
DMA_ADC1 = DMA1_Ch1 循环 半字→半字  @Core/Src/adc.c:105  #CubeMX  2026-11-12
TIM4_CC4_CCR = (TIM1_CCR1 + 10)/20 - 4  @module/drv/drv_tim.c:27  #实测  2026-11-12
TIM4_CC4_CCR_50PCT = 86 = (1799+10)/20-4  @module/drv/drv_tim.c:27  #实测  2026-11-12
TIM4_CC4_TRIG_OFFSET = -4 TIM4 ticks = -0.556us  @module/drv/drv_tim.c:14  #实测  2026-11-12
TIM4_CC4_EVENT = CC4E=1 且 0<CCR4<=ARR；PB9 下降沿=CC4，上升沿=计数器复位  @RM0008  #实测  2026-11-12
TIM4_CC4_PHASE = 50%: PB9 下降沿落后 PA8 上升沿 11.959us  @逻辑 Ch5/7  #实测  2026-11-12
TIM4_CC4_CENTER = 50%: 采样中心≈12.52us，上管中点≈12.49us；跳过起始~150us 伪沿  @逻辑  #实测  2026-11-12
CTRL_LOOP_WINDOW = ADC TC≈16.3us；下次 TIM1 更新 100us；控制预算≈83.7us  @TIM1_RCR  #推导  2026-11-12
ADC_DMA_MODE = BUF_LEN=2 半字，Circular，仅 TC，HT 关  @drv_adc.c:36  #实测  2026-11-12
ADC_DMA_VERIFY = 旧构建(2026-11-12): seq≈9.99kHz, I=2266/V=2060  @J-Link  #实测  2026-11-12
TIM1_CCR1_50PCT = 1799 = TIM1_ARR/2（09:06 构建）  @drv_tim.c:79  #实测  2026-11-13
TIM1_PWM_START = HAL_TIM_PWM_Start(htim1, CH1) -> CC1E=1, MOE=1  @drv_tim.c:75  #实测  2026-11-12
PA8_VERIFY = 20.000kHz, 50.00%  @逻辑分析仪 Ch7  #实测  2026-11-12
PB9_VERIFY = 10.000kHz, 稳定高电平 11.959us (11.96%)；下降沿=ADC 触发  @逻辑分析仪 Ch5  #实测  2026-11-12
SHUNT_R1 = 25mR，VSS_OUT→GND 输出回流，低端 shunt 输出电流连续；上管中点采样有效  @原理图 Buck V1.0  #原理图  2026-11-12
TIM4_CC4_SWEEP = 10–90%@10%: 偏差均值 +0.042us，范围 -0.022..+0.105us  @逻辑 Ch5/7  #实测  2026-11-12
TIM4_CC4_SWEEP_CLK = PA8 19.9999–20.000kHz；PB9 10.000kHz；std 0.035–0.047us  @逻辑  #实测  2026-11-12
TIM4_CC4_SWEEP_METHOD = J-Link 直写 CCR1/CCR4 并回读校验；非运行时 SetDuty  @J-Link  #实测  2026-11-12
TIM4_CC4_SWEEP_RESTORE = 测试后恢复 50%：CCR1=1799、CCR4=86  @J-Link  #实测  2026-11-12
TIM4_CC4_SWEEP_ARTIFACT = .embeddedskills/capture/logic/sweep_results.json  #实测  2026-11-12
ADC_DMA_VERIFY_FINAL = 旧构建(2026-11-12): I=2305/V=2067, seq=106358  @J-Link  #实测  2026-11-12
ADC_ERROR_ACTION = DRV_ADC_ErrorIsr -> DRV_TIM_Stop()，无 fault  @drv_adc.c:66  #推导  2026-11-13
TIM_START_FAIL = StartSync 失败 -> DRV_TIM_Stop()，无 fault  @drv_tim.c:75  #推导  2026-11-13
TIM_STOP_FUNC = PWM_Stop 两路 + MOE 关闭，可重入  @drv_tim.c:58  #推导  2026-11-13
ADC_FRAME_ADDR = 0x20000010（4B）  @EA_Buck.map  #实测  2026-11-13
ADC_FRAME_VERIFY_20261113 = 09:06 构建连续读更新: I=2284/V=2059  @J-Link  #实测  2026-11-13
TIM_CCR_VERIFY_20261113 = 09:06 构建: CCR1=1799, CCR4=86  @J-Link  #实测  2026-11-13
TIM4_CC4_VERIFY_20261113 = 50%: 偏差均值 +0.0427us，范围 -0.0215..+0.0625us  @逻辑 Ch5/7  #实测  2026-11-13
TIM4_CC4_VERIFY_ARTIFACT = .embeddedskills/capture/logic/cleanup_20261113/  #实测  2026-11-13
TIM_CCR_LIVE_20261113 = 09:10+ 外部改 CCR1=300（8.3%）并烧录；frame I=0/V=10  @J-Link  #实测  2026-11-13
