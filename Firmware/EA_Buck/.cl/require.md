<!-- 维护模式：本文件由 AI 维护，用户不手改。内容 = 稳定需求（硬件资源池 / 引脚映射 / 调试回传）。高频状态（任务/风险/时序）在 .cl/state.md；工具链/烧录在 .embeddedskills/config.json。**全局目标不落文件——由用户把握**（见 SKILL.md P0-5）。-->
> **落地位置**：本模板的实例放 **`.cl/require.md`**（与 `.cl/` 一起版本化；工程根不再放 require.md）。
> **附录段约定**：`# 附录 <名>` 段为**项目参考文档**（协议/流程/移植说明等），放**文件末尾**，默认**不参与常规扫描**；超过 ~4KB 建议移出到 `.cl/refs/<名>.md`，此处只留一行指针。例：`# 附录 A: CAN 命令协议`。
# 硬件资源池 (Hardware Pool)
芯片: STM32F103C8T6 (LQFP48)
晶振频率: HSE 8MHz → PLL×9 → SYSCLK 72MHz
关键器件:
  - Buck 功率级: 24V→3.3–12V/0–3A，L=220µH，C=220µF，Fsw 20kHz；电压外环→电流内环，3A 恒流限幅，3.3A 跳闸，低占空比电压环启动
  - 电压采样 ADC_V: PA0 (ADC1_IN0) — 12K/2.2K 分压，4.4233mV/码（同板实测）
  - 电流采样 ADC_I: PA1 (ADC1_IN1) — 25mR 分流 + 20V/V 差分，1.6589mA/码（同板实测）
  - I2C1 从机: PB6/PB7 — 型号与从机地址待推导
  - 按键 KEY1/KEY2: PB5/PB4 — 功能待推导
> RAM/Flash/栈**不在此列**——由 `/cl init` 从链接脚本/启动文件读**工程实际分配**并写 `.cl/memory/`（`CHIP_RAM`/`CHIP_FLASH`/`STACK_SIZE`，见 `details/codegen.md`「受限纪律」）。

# 引脚固定映射表 (Pin Mapping)
> 必须为 Markdown 表格（首行表头 + `|---|` 分隔行），不用裸文本行。

| 功能网标 | 引脚号 | 外设功能 |
| :------- | :----- | :------- |
| ADC_V | PA0 | ADC1_IN0 |
| ADC_I | PA1 | ADC1_IN1 |
| BUCK_PWM | PA8 | TIM1_CH1 |
| USART1_TX | PA9 | USART1_TX |
| USART1_RX | PA10 | USART1_RX |
| KEY1 | PB5 | GPIO_Input |
| KEY2 | PB4 | GPIO_Input |
| I2C1_SCL | PB6 | I2C1_SCL |
| I2C1_SDA | PB7 | I2C1_SDA |
| ADC_TRIG | PB9 | TIM4_CH4 |
| SWDIO | PA13 | SYS_JTMS-SWDIO |
| SWCLK | PA14 | SYS_JTCK-SWCLK |
| HSE_IN | PD0 | RCC_OSC_IN |
| HSE_OUT | PD1 | RCC_OSC_OUT |

# 调试回传 (Debug Backchannel) — 稳定事实
调试回传接口: USART1 (PA9 TX / PA10 RX) 115200 8N1，USB-TTL 端口运行期自动探测
启动横幅: System Start!   # 板端上电打印的唯一标识行；watcher --boot 匹配它计 boots，生效门依赖此项

# 手册来源 (Datasheet Provenance) — 稳定事实
> 一行一份；**下载即登记**（见 `details/datasheet.md`）。`[原厂]` = 原厂站点直取；`[镜像-待复核]` = 原厂不可达时的可信镜像，须核对页数/版本号与官方一致。
> 格式：`<文件名> ← <url> <YYYY-MM-DD> [原厂|镜像-待复核]`
(待登记)
