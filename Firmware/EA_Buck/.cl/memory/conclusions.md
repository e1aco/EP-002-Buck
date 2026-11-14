# 定论

## 定论（已否决 / 已解决 · 勿重开）
- RCR_TARGET = 已解决: TIM1 RCR=1→ADC 触发 10kHz 为设计目标  依据: 用户确认 2026-11-12  来源: 你
- ADC_CAL = 已解决: 同板V=4.4233mV/码 I=1.6589mA/码 依据:实测 日期:2026-11-13 来源:你
- VOLT_LOOP = 已否决: 仅电压环、3A跳闸不恒流 依据:用户改为级联控制 日期:2026-11-13 来源:你
- CASCADE = 已决策: 电压外环→电流内环，3A恒流限幅、3.3A跳闸、<10%占空比电压环启动 依据:你 日期:2026-11-13 来源:你
- ISR_TIMING = 已实测: DMA ISR 5.6us max/5.15us avg @10kHz（PID 移出后）；旧 36.8us 作废 依据:DWT 2026-11-14
- CTRL_TIME = 已实测: APP_CONTROL_Update 34.1us avg/61.2us max @10kHz，占 34% 依据:DWT 2026-11-14
- CURRENT_SENSE = 已解决: 带载 317mA@3.3V、1211mA@12V 线性，采样正常 依据:RTT 2026-11-13
- I2284_CAUSE = 已解决: 早前 3.79A 系 50% 开环、采样点落上管导通瞬间的开关噪声 依据:RTT
- RTT_TOOLING = 已实测: read-mem 调用会 halt 目标不恢复；用 RTTLogger 采集 1kHz 无丢帧
- VOLT_LOOP_TUNED = 已达标: Kp=0.050/Ki=15.0，四工作点超调<0.8%、建立18–19ms 依据:阶跃
- VOLT_LOOP_BIGSTEP = 已达标: 3.3→12V 超调1.13%、建立31ms、无OVP 依据:阶跃 2026-11-13
- ADC_NOISE_FLOOR = 已实测: 稳态 pv 抖动 ±13mV、1 码=4.42mV；误差过零率 40.5% 属量化噪声非振荡 依据:RTT 2026-11-13
- KEY_FUNC = 已实测: KEY1 开关输出、KEY2 循环 3.3/5/9/12V、关输出时可改档 依据:按键 2026-11-13
- KEY_POLARITY = 已实测: PB4/PB5 外部上拉，按下为低（IDR=0xFDFB 空闲高） 依据:2026-11-13
- RISE_ALL = 已实测: 7 种上升超调≤0.74%、建立 23–61ms；上升时间由软启动 240mV/ms 主导 依据:2026-11-13
