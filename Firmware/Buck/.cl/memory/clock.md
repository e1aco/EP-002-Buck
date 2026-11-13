# 时钟树（.ioc 与 main.c SystemClock_Config 一致）
HSE_FREQ = 8MHz  @Buck.ioc PLLMUL=MUL9→72MHz；uvprojx CLOCK(8000000)  #代码核对  2026-10-23
SYSCLK = 72MHz  @Core/Src/main.c:134 PLL×9  #代码核对  2026-10-23
HCLK = 72MHz  @main.c AHB_DIV1  #代码核对  2026-10-23
PCLK1 = 36MHz  @main.c APB1_DIV2（APB1 定时器时钟 72MHz）  #代码核对  2026-10-23
PCLK2 = 72MHz  @main.c APB2_DIV1  #代码核对  2026-10-23
ADCCLK = 12MHz  @main.c RCC_ADCPCLK2_DIV6 = 72/6  #代码核对  2026-10-23
FLASH_LATENCY = 2WS  @main.c HAL_RCC_ClockConfig  #代码核对  2026-10-23
