<!-- 维护模式：本文件由 AI 维护，用户不手改。内容 = 稳定需求（硬件资源池 / 引脚映射 / 调试回传）。高频状态（任务/风险/时序）在 .cl/state.md；工具链/烧录在 .embeddedskills/config.json。**全局目标不落文件——由用户把握**（见 SKILL.md P0-5）。-->
# 硬件资源池 (Hardware Pool)
芯片: STM32F103C8T6 (LQFP48, Cortex-M3, 64KB Flash / 20KB SRAM)
晶振频率: HSE 8MHz 外部晶振（PLL×9 → SYSCLK 72MHz）
关键器件:
  - Q1–Q4 AOD4126（N-MOSFET，Buck 功率级；Qgd/Rds_on 待 datasheet）
  - U2 EG2131（半桥栅极驱动，HIN=PWM1、LIN=PWM2；自举 D3 FR107W + R2 4.7Ω；栅极 HO 串 R10=0Ω、LO 串 R4 4.7Ω + D4/D5 1N4148W）**R10 必须 0Ω**：4.7Ω 时上管关断慢于内置 250ns 死区 → 上下管重叠（2026-11-08 实测）
  - 电感 220µH（VCC 支路，标号 U5；VSS 支路同规格待核对）
  - R1 25mΩ（VSS_OUT 回线分流电阻，IO+/IO− 引出至差分放大；v1.0 由 50mΩ 改小）
  - C_out 220µF/50V（输出电容；用户给定 2026-11-06，调参瞬态评估用）
  - U6/U8 TLV9002IDR（双运放：差分放大 + 电压跟随，+5V 单电源）
  - U4 XL1509-12E1（+12V 降压）/ XL1509-5.0E1（+5V）/ RT9013-33GB（+3.3V LDO）
  - I2C1 仅引排针 H3/H4（PZ254V-11-04P）：H3 接 0.96" SSD1306 OLED（0x3C）；**板上无 I2C 上拉**（赖屏模块自带），故 400kHz 带固件侧 100kHz 回退（2026-11-08）
输入规格: 24V 恒定（用户给定 2026-10-28）
输出规格: 5–12V / 0–3A（用户给定 2026-10-28，原 0–12V）
输出档位与开关: 上电输出关闭、档位 5V；Key1(PB5) 按一次反转输出开关（开启先出 5V，关机即回 5V 档）；Key2(PB4) 在 5V/12V 间切换（关机时忽略）（用户给定 2026-11-06）
原理图: `Hardware/SCH_Buck_1-Buck_2026-10-24.png`（v1.0，2026-10-24 更新：R1 50mΩ→25mR、R19/R20 3K→2.2K）
  # 提示：器件参数（Qgd/Rds_on/驱动电流/电感饱和电流/负载范围）待 datasheet 放入 datasheet/ 后推导
> RAM/Flash/栈**不在此列**——由 `/cl init` 从链接脚本/启动文件读**工程实际分配**并写 `.cl/memory/`（`CHIP_RAM`/`CHIP_FLASH`/`STACK_SIZE`，见 `details/codegen.md`「受限纪律」）。

# 引脚固定映射表 (Pin Mapping)
> 必须为 Markdown 表格（首行表头 + `|---|` 分隔行），不用裸文本行。

| 功能网标 | 引脚号 | 外设功能 |
| :------- | :----- | :------- |
| PWM1 → EG2131 HIN | PA7 | **TIM3_CH2**（AF_PP，20kHz，从模式触发 ITR0=TIM1_TRGO）；单输入模式与 PA8 同相同宽，死区由 EG2131 内置产生（栅极实测 249ns） |
| PWM2 → EG2131 LIN# | PA8 | TIM1_CH1（AF_PP，20kHz）；单输入模式与 PA7 同相同宽，低有效 → 高电平期间下管关断 |
| ADC_V（输出电压采样） | PA0 | ADC1_IN0（T4_CC4 触发 + DMA1_Ch1） |
| ADC_I（输出电流采样） | PA1 | ADC1_IN1（同上，规则组 Rank1 先采） |
| ADC_TRIG_MON（触发相位观测） | PB9 | TIM4_CH4（AF_PP，1kHz 方波，下降沿 = ADC 触发时刻） |
| USART1_TX | PA9 | USART1_TX（115200 8N1） |
| USART1_RX | PA10 | USART1_RX + DMA1_Ch5（circular） |
| I2C1_SCL | PB6 | I2C1_SCL（400kHz，AF_OD；板上无上拉，累计错误后固件单向回退 100kHz） |
| I2C1_SDA | PB7 | I2C1_SDA（400kHz，AF_OD；同上） |
| BUTTON1 / Key1 | PB5 | GPIO 输入（板上 10K 上拉 R27，低有效） |
| BUTTON2 / Key2 | PB4 | GPIO 输入（板上 10K 上拉 R28，低有效） |
| SWDIO | PA13 | SYS_JTMS-SWDIO |
| SWCLK | PA14 | SYS_JTCK-SWCLK |
| OSC_IN / OSC_OUT | PD0 / PD1 | HSE 8MHz 外部晶振 |
| BOOT1 | PB2 | GPIO（板上 10K 下拉 R25） |

# 调试回传 (Debug Backchannel) — 稳定事实
调试回传接口: 双后端，编译期宏 `CL_DBG_BACKEND`（module/config.h）二选一，**互斥同窗口只能连一个**
  - `0` = J-Link RTT（默认，通道 0，走 SWD 不占串口；`jlink_rtt.py` 读）
  - `1` = USART1 115200 8N1（PA9/PA10 外接 USB-TTL，COM 自动探测）
启动横幅: Buck Ready（实测经 RTT 打出 `[BOOT] Buck Ready`）   # watcher --boot 匹配它计 boots

# 手册来源 (Datasheet Provenance) — 稳定事实
> 一行一份；**下载即登记**（见 `details/datasheet.md`）。`[原厂]` = 原厂站点直取；`[镜像-待复核]` = 原厂不可达时的可信镜像，须核对页数/版本号与官方一致。
> 格式：`<文件名> ← <url> <YYYY-MM-DD> [原厂|镜像-待复核]`
(待登记：datasheet/ 当前仅 `电感参数.jpg`（80125-220uH 7A，用户提供截图）；AOD4126 / TLV9002 / XL1509 / STM32F103 RM+DS 均缺)
EG2131 ← https://www.egmicro.com/static/doc/功率驱动芯片/单相半桥/EG2131 中压300V1A驱动芯片数据手册V1.0.pdf 2026-10-30 [原厂-仅取参数未落盘]
电感参数.jpg ← 用户提供截图 2026-10-23 [用户提供]

# 附录 A: 原理图与 .ioc 差异（已裁决 2026-10-23）
- **PWM1/PA7**：原理图 PA7→PWM1→EG2131 HIN，`.ioc` 未配置。**定论（2026-11-08 更新）**：改回**单输入形态**——PA8=TIM1_CH1→LIN#、PA7=TIM3_CH2→HIN 输出同一路同相 20kHz 波形（固件镜像实现，`DRV_PWM_SINGLE_INPUT=1`，无需改接线），HIN=LIN# → 驱动芯片内部互补 + 250ns 死区（栅极实测 249ns，无直通、电流不爬升）。BDTR.DTG 保持 0。EG2131 真值表 `HO=HIN&LIN#`、`LO=!HIN&!LIN#`，`(1,0)/(0,1)` 均为双关态。历史：2026-10-30~11-08 为双通道内脉冲（DT=36，725ns）。
- **PWM2/PA8**：`.ioc` 配为 TIM1_CH1，与原理图网标 PWM2 对应（同一物理网络，命名差异，不影响功能）。
- **TX1/RX1 网标**：原理图 PA10 标 TX1、PA9 标 RX1，与 STM32 固定复用（PA9=TX / PA10=RX）相反 → 判断为按转接板视角标注，**以 .ioc 为准**。
- **EG2131 数量**：原理图可见 U2 一片；功率管 4 只（Q1–Q4）分属两路半桥 → 第二路驱动位置待核对（若为同一片驱动的两路，则 HIN/LIN# 共用成立）。
