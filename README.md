# EP-002-Buck

基于 **STM32F103C8T6** 的 24V 输入、5–12V / 0–3A 数控同步 Buck 变换器。
固件包含级联 PID 闭环、软启动、过压/空载保护、按键与 OLED 显示、RTT/UART 遥测，以及用于调参和波形验收的测试代码。

> 硬件工作电压和电流较高，上电调试请使用限流电源并确认功率级接线后再运行。

## 主要指标

| 项目 | 参数 |
| :--- | :--- |
| 输入 | 24V DC |
| 输出 | 5V / 12V 两档，0–3A |
| 开关频率 | 20kHz（TIM1，死区由 EG2131 内部产生） |
| ADC 采样 | 双通道 2kHz，TIM4 触发 + DMA |
| MCU | STM32F103C8T6，HSE 8MHz → SYSCLK 72MHz |
| 控制方式 | 电压外环 + 电流内环级联 PID，可切换单电压环 |
| 保护 | 17V OVP、无输入保护、空载重试后自动关机 |
| 人机接口 | Key1 开关输出、Key2 切换 5V/12V、0.96" SSD1306 OLED |
| 调试回传 | J-Link RTT 或 USART1 115200 8N1（编译期二选一） |

## 功能特性

- 上电默认关机、档位 5V；Key1 开启输出，Key2 在 5V/12V 间切换。
- 软启动参考斜坡 + 启动段 KI 倍率提升，兼顾启动速度与过冲。
- 电压外环输出电流参考，电流内环输出 PWM 占空比；增益和模式可在 `module/config.h` 切换。
- 2kHz ADC 中断内完成控制步进，主循环只做遥测、按键、显示和超时保护。
- 1Hz 遥测输出原始 ADC、电压、电流、PWM、保护状态等运行信息。
- `CL_TUNE_LOG` 可输出 50ms CSV，配合阶跃序列做 PID 调参。
- `test/` 下提供时序探针、ADC 标定、开环扫档等一次性验收工具。

## 硬件概要

| 器件 | 说明 |
| :--- | :--- |
| Q1–Q4 | AOD4126 N-MOSFET，Buck 功率级 |
| U2 | EG2131 半桥栅极驱动，HIN/LIN# 输入，内置 250ns 死区 |
| L | 220µH 功率电感 |
| R1 | 25mΩ 输出电流采样电阻，差分放大后送 ADC |
| U6/U8 | TLV9002 双运放，差分放大 + 电压跟随 |
| U4 | XL1509 系列 12V/5V 辅助电源，RT9013-33 提供 3.3V |
| 显示 | 0.96" SSD1306 OLED，I2C 地址 0x3C |

原理图与实物资料见 `Hardware/`。
注意：上管栅极电阻 **R10 必须为 0Ω**；使用 4.7Ω 时上管关断慢于驱动内置死区，会造成上下管重叠。

## 引脚分配

| 功能 | 引脚 | 外设 |
| :--- | :--- | :--- |
| PWM1 → EG2131 HIN | PA7 | TIM3_CH2，20kHz，与 PA8 同相同宽 |
| PWM2 → EG2131 LIN# | PA8 | TIM1_CH1，20kHz |
| 输出电压采样 | PA0 | ADC1_IN0 |
| 输出电流采样 | PA1 | ADC1_IN1 |
| ADC 触发相位观测 | PB9 | TIM4_CH4 |
| USART1_TX / RX | PA9 / PA10 | 115200 8N1，RX 使用 DMA |
| I2C1_SCL / SDA | PB6 / PB7 | 400kHz，失败后回退 100kHz |
| Key1 / Key2 | PB5 / PB4 | GPIO 输入，低有效 |
| SWDIO / SWCLK | PA13 / PA14 | SWD 调试 |
| HSE | PD0 / PD1 | 8MHz 晶振 |

## 软件结构

```text
Firmware/Buck/
├── Core/                 CubeMX 生成的外设初始化与中断入口
├── module/
│   ├── bsp/              板级适配：PWM 引脚、按键、ADC 标定系数
│   ├── drv/              ADC / PWM / OLED / UART / RTT 驱动
│   ├── service/          调试输出服务
│   ├── app/              闭环控制、按键、显示、遥测
│   └── vendor/           SEGGER RTT
├── test/                 时序探针、ADC 标定、开环扫档、调参日志
├── MDK-ARM/              Keil MDK 工程
├── Buck.ioc              STM32CubeMX 工程
└── config.h              编译期功能开关
```

控制链路：

```text
TIM4 TRGO (2kHz)
  → ADC1 双通道 + DMA
  → HAL_ADC_ConvCpltCallback
  → DRV_ADC_ConvCpltIsr()
  → APP_CONTROL_Step()  (PID → CCR1)
  → DRV_PWM / TIM1
```

主循环负责 `APP_TELEMETRY_Loop()`、`APP_CONTROL_Loop()`、`APP_KEY_Loop()`、`APP_DISPLAY_Loop()` 和 `DRV_PWM_Loop()`。

## 编译与烧录

1. 使用 Keil MDK 5 打开 `Firmware/Buck/MDK-ARM/Buck.uvprojx`，选择 `Buck` target 后 Build。
2. 构建产物位于 `Firmware/Buck/MDK-ARM/Buck/`（`Buck.hex` / `Buck.axf`）。
3. 通过 J-Link SWD 烧录，器件选择 `STM32F103C8`。
4. 调试输出后端在 `Firmware/Buck/module/config.h` 中选择：
   - `CL_DBG_BACKEND_RTT`：J-Link RTT，不占串口。
   - `CL_DBG_BACKEND_UART`：USART1 115200 8N1。

## 编译期配置

| 宏 | 默认 | 说明 |
| :--- | :--- | :--- |
| `CL_DBG_BACKEND` | RTT | 调试输出后端 |
| `CL_CTRL_MODE` | 双环 | `CL_CTRL_MODE_VOLT_CURR` / `CL_CTRL_MODE_VOLT_ONLY` |
| `CL_CTRL_ENABLE` | 1 | 1 闭环接管 CCR1，0 开环固定占空比 |
| `CL_TELE_PERIOD_MS` | 1000 | 遥测周期 |
| `CL_TUNE_LOG` | 0 | 调参 CSV 与自动阶跃序列，生产版必须关闭 |
| `CL_PWM_SWEEP` | 0 | 开环占空比扫描 |
| `CL_ADC_CAL_SWEEP` | 0 | ADC 三点标定扫描 |
| `CL_TIMING_MEASURE` | 0 | 时序探针 |
| `CL_ADC_CALIB` | 0 | ADC 转换时间标定 |

## 验证工程

`Verify/PWMVerify/` 是独立的 CubeMX/Keil 工程，用于验证 TIM1 CH1（PA8）与 TIM3 CH2（PA7）的 20kHz 同相 PWM、占空比和触发相位。`.cl/tools/` 下附带 Saleae 采集与 `pwm_phase_check.py` 相位分析脚本。

## 调试与项目记录

- `.cl/`：本地调试记录（需求、硬件结论、采集日志），不纳入版本库。
- `.embeddedskills/config.json`：Keil、J-Link、串口、逻辑分析仪等工具配置。

采集日志、构建产物和工具运行状态已通过 `.gitignore` 排除，不进入版本库。
