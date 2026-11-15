# 定论

## 定论（已否决 / 已解决 · 勿重开）
- RCR_TARGET = 已解决: TIM1 RCR=1→ADC 触发 10kHz 为设计目标  依据: 用户确认 2026-11-12  来源: 你
- ADC_CAL = 已解决: 同板V=4.4233mV/码 I=1.6589mA/码 依据:实测 日期:2026-11-13 来源:你
- VOLT_LOOP = 已否决: 仅电压环、3A跳闸不恒流 依据:用户改为级联控制 日期:2026-11-13 来源:你
- CASCADE = 已决策: 电压外环→电流内环，3A恒流限幅、3.3A跳闸、<10%占空比电压环启动 依据:你 日期:2026-11-13 来源:你
- ISR_TIMING = 已实测: DMA ISR 5.6us max/5.15us avg @10kHz（PID 移出后）；旧 36.8us 作废 依据:DWT 2026-11-14
- CTRL_TIME = 已实测: 定点版 ISR 全程 12.87us avg/39.5us max @10kHz，占 12.9% 依据:DWT 2026-11-14
- FIXED_POINT = 已实现: PID 改定点（比例 Q16 / 积分 Q24）、控制移入 ADC ISR，软浮点清零 依据:map
- FIXED_ROUNDING = 已解决: P/I 各自取整致 ±1 码抖动；Q24 域合成单次取整后 sd 9.3→5.2mV 依据:实测
- FIXED_PERF = 已验证: 7 种上升定点 vs float 超调差≤0.18%、建立差≤2ms、稳态≤2mV 依据:实测
- CURRENT_SENSE = 已解决: 带载 317mA@3.3V、1211mA@12V 线性，采样正常 依据:RTT 2026-11-13
- I2284_CAUSE = 已解决: 早前 3.79A 系 50% 开环、采样点落上管导通瞬间的开关噪声 依据:RTT
- RTT_TOOLING = 已实测: read-mem 调用会 halt 目标不恢复；用 RTTLogger 采集 1kHz 无丢帧
- VOLT_LOOP_TUNED = 已达标: Kp=0.050/Ki=15.0，四工作点超调<0.8%、建立18–19ms 依据:阶跃
- VOLT_LOOP_BIGSTEP = 已达标: 3.3→12V 超调1.13%、建立31ms、无OVP 依据:阶跃 2026-11-13
- ADC_NOISE_FLOOR = 已实测: 稳态 pv 抖动 ±13mV、1 码=4.42mV；误差过零率 40.5% 属量化噪声非振荡 依据:RTT 2026-11-13
- KEY_FUNC = 已实测: KEY1 开关输出、KEY2 循环 3.3/5/9/12V、关输出时可改档 依据:按键 2026-11-13
- KEY_POLARITY = 已实测: PB4/PB5 外部上拉，按下为低（IDR=0xFDFB 空闲高） 依据:2026-11-13
- OLED_SSD1306 = 已实测: I2C1 0x3C @400kHz，128x64，1KB 显存，全亮验证通过 依据:2026-11-14
- KEY_LONGPRESS = 已实测: KEY1 长按开关、KEY2 长按交替增减 0.1V@0.5V/s 依据:2026-11-14
- RISE_ALL = 已实测: 7 种上升超调≤0.74%、建立 23–61ms；上升时间由软启动 240mV/ms 主导 依据:2026-11-13
- SCOPE_TRIG_LVL = 已定论: SDS824X `:TRIGger:EDGE:LEVel` 写绝对电压，与 offset 无关 依据:实测 2026-11-15
  工具 scope_transient 的 `level_abs - voff` 换算错 → offset≠0 时电平落到信号外永不触发
- GDB_CALL_UNSAFE = 已定论: 10kHz ISR 下 GDB `call` 抢栈 → HardFault 依据:实测 2026-11-15
  改控制须用 `set var` 写 s_output_on/s_vref_mv/s_vref_target_mv，不执行代码
- JLINK_HALT_STICKY = 已定论: Commander connect 退出后 CPU 停在 __main 依据:实测 2026-11-15
  GDB 操作后须 `monitor go`（PC=0xdeadbeee=运行），异常时用 jlink_exec reset 兜底
- SOFTSTART_RAMP = 已实测: 上升 19.6/24.4/35.6/44.4ms@3.3/5/9/12V 依据:示波器 2026-11-15
  等效斜率 171/208/254/271mV/ms；**包络超调 1.00/0.66/0.74/1.39%**（瞬时口径 15.0/9.9/7.4/5.6% 含尖峰）
- OVS_SPIKE = 已定论: "3.3V 超调 12.87%" 是测量伪影，真实包络超调 <2% 依据:实测 2026-11-15
  开关尖峰幅度 0.47–0.66V 四档恒定（与电压无关）→ 探头地环路/电容 ESL 耦合，非控制环
  判据：瞬时口径超调随电压递减而尖峰恒定 = 伪影；scope skill analyze_rise 已改包络峰值
- TURNOFF_RING = 已实测: 硬关断负向过冲 -2.31/-3.42/-4.96/-5.84V@3.3/5/9/12V 依据:示波器 2026-11-15
  振铃 733–784Hz 与 LC 理论 723Hz 吻合，ζ≈0.05（10Ω 轻载欠阻尼）；随电压单调增大
  初测 5V/12V 的 -2.51/-4.32V 系底边削顶低估，已用 clip=0 重测更正
- SOFT_STOP = 已实测: 软关断把关断下冲降到 -0.34/-0.45/-0.46/-0.76V 依据:示波器 2026-11-15
  降 85–91%，振铃 osc=0；实现=参考电压按 240mV/ms 降到 0 再停 PWM（vout≤300mV 或 300ms 超时）
  代价关断耗时 22–43ms；设定值保留、重新开启正常软启动
- RIPPLE_ALL = 已实测: 20M 限带主带纹波 47.2/74.0/103.4/120.0mV@3.3/5/9/12V 依据:示波器 2026-11-15
  纹波系数 1.00–1.48%；经 DC 电平(3.21/4.92/8.83/11.80V) + 20kHz 开关峰双校验
- SCOPE_SKILL_V3 = 已定论: scope skill 修 4 项——触发绝对电平 / Ready 完成态 /
  削波自动升档 / --exec-on-arm 异步；原生命令已实测跑通上升与下降沿 依据:实测 2026-11-15
- SCOPE_NOISE_TRAP = 已定论: 输出未开(0V)时 AC 耦合测到的是噪声底 13–17mV 依据:实测 2026-11-15
  初版 15mV"纹波"即此；现已加 DC 电平硬门禁 + 开关峰校验，数据无效须重测
- LOOP_CONVERGE = 已实测: 3.3V 稳态 pv=3299/sp=3300（误差1mV）依据:GDB 2026-11-15
  cascade=1、fault=0、ccr=524、iout=318mA；此前的"-1.9% 稳态误差"是过渡态误读，非 bug
- RTT_FREEZES = 已定论: RTT 采集期间 GDBServer halt CPU → 控制环停摆 依据:实测 2026-11-15
  遥测不能用于观测动态；动态用示波器，状态用 GDB 定点读（jlink_rtt 一跑输出就归零）
- SCOPE_RANGE_DC_ERR = 已定论: 示波器 DC 读数随垂直量程变化，误差≈0.1–0.3格×vdiv 依据:实测 2026-11-15
  同一12V输出：5V/div→11.60、2V/div→11.80、1V/div→11.90（极差2.6%）；固件11995
  档越细越准 → 是本机 offset 校准残差，非增益误差；示波器 DC 不能当 1% 基准
- ADC_VERIFY = 已实测: ADC 标定两点验证通过，旧板 scale 4.4233mV/码 对本板适用 依据:实测 2026-11-15
  3.3V 固件 3299–3303 / 万用表 3.30；12V 固件 11995 / 万用表 11.99–12.00；偏差 ≤0.1%
  5/9V 为中点由两端线性覆盖；此前"偏低 1.6%"系示波器量程档 DC 误差，非 ADC 问题
- OVP_VERIFIED = 已实测: OVP 13.5V 注入通过 依据:实测 2026-11-15
  目标写 13800mV → fault=1、ovp_count=3（3 点去抖）、输出跌至 −0.19V
- FAULT_SHUTDOWN = 已实测: DRV_TIM_Stop() 故障停机路径有效 依据:寄存器 2026-11-15
  OVP 触发后 CCER=0（CC1E/CC1NE OFF）、BDTR=0x2000（MOE OFF）、CR1=0（CEN OFF）
- OC_UNVERIFIED = 未验证: OC 3.3A 无法用 10Ω 负载触发 依据:分析 2026-11-15
  10Ω@12V 仅 1.2A；且电流内环限幅 3A 先于 OC 3.3A 生效
  需低阻负载(≤3Ω) 或钩子注入；属备份保护，仅电流环失效时才应动作
- PROD_TELEMETRY = 已定论: 生产版遥测 10Hz（APP_TELEMETRY_PERIOD_US=100000） 依据:实测 2026-11-15
  1kHz 时约 70KB/s 溢出 RTT 缓冲；10Hz 实测 t 间隔 100ms（7705/7805/7905）
  调试需波形级观测时临时改回 1000U
- FLASH_FAIL_NO_POWER = 已定论: 烧录报 "Target DLL has been cancelled" 先查目标板是否上电 依据:实测 2026-11-15
  未上电时 jlink_exec info 仍报 vtarget=3.303V（VTref 悬空读数不可信），
  但 connect/reset/flash 全失败；上电后同命令直接成功，非 DLL 损坏
  判据：上电后 JLink.exe 手动 connect 能打出 "Found Cortex-M3"
