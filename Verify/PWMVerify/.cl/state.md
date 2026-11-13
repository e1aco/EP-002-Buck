# 项目状态 (State) — AI 维护，状态单源

> 说明：本项目 `.cl/` 为精简状态位（未跑过完整 `/cl init`，无 `.cl/require.md`/`.cl/memory/`，
> 稳定需求见 `.workbuddy-ai/memory/` 项目日志）。任务队列/风险/时序表格式沿用 `templates/state.md`。

## 任务队列 (Task Queue)

- [✓] TIM1 主控 TIM3 CH2(PA7) 输出 PWM：烧录并验证波形（起 2026-10-12 | 止 2026-10-12）验收:自动
     依据: 编译 0E0W；jlink flash verified=true；
           RCC_CR=0x03035B83(HSERDY=1,PLLRDY=1)、RCC_CFGR=0x001D040A(72MHz)；
           TIM1 CR1=1/CR2=0x20(MMS=010)/ARR=3599/CCR1=1800/BDTR=0xA000；
           TIM3 SMCR=0x6(SMS=110,TS=ITR0)/SR=0x5F(TIF=1)/ARR=3599/CCR2=1790/CCER=0x10；
           逻辑分析仪实测: 20.000kHz / PA8 占空比 50.00% / PA7 49.75% / 上升沿滞后 83ns=5.98cnt
- [✓] 独立驱动 Saleae Logic 2 采集波形（起 2026-10-12 | 止 2026-10-12）验收:自动
     依据: .cl/tools/saleae_capture.sh 一键跑通，两次采集结果一致（CH5/CH7 自动识别）

## 未决风险 (Open Risks)

| 日期 | 任务 | 类别 | 位置 | 风险点 | 可追溯到? | 状态 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| 2026-10-12 | TIM3 相位锁定 | 时序 | Core/Inc/tim.h PWM_DEADTIME_CNT | 设计死区 5 cnt，**实测 6 cnt**（主→从触发链路固有 1 cycle 延迟，两种独立方法互证）→ 内缩不对称：上升沿滞后 ≈6 cnt、下降沿超前 ≈3~4 cnt（设计意图 5/5 对称）；脉冲宽度仍是设计值 1790 cnt | 是（SWD 冻结法 ×4 + 逻辑分析仪 ×2） | [ ] 待用户裁决是否补偿 |

## 时序测量表 (Timing Budget)

### 前置配置
- 时序测试点 IO: Saleae **CH5=PA8、CH7=PA7**（注意：探头接在 CH5/CH7，不是 CH0/CH1）
- 探针默认位置: 不适用（本任务为波形输出验证，非软件耗时测量）

### 条目表
| 操作 | 位置/探针位置 | 理论值(依据) | 预算/判据 | 测试 | 实测值(日期) | 利用率 | 状态 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| (无软件时序条目) | | | | | | | |
