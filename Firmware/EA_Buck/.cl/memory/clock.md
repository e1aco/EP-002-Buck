# 时钟树（CubeMX 生成，main.c 代码核对）

CLOCK_SYSCLK = 72MHz  @EA_Buck.ioc RCC.SYSCLKFreq_VALUE  #CubeMX  2026-11-12
CLOCK_HCLK = 72MHz  @EA_Buck.ioc RCC.HCLKFreq_Value  #CubeMX  2026-11-12
CLOCK_APB1 = 36MHz  @EA_Buck.ioc RCC.APB1Freq_Value  #CubeMX  2026-11-12
CLOCK_APB1_TIM = 72MHz  @EA_Buck.ioc RCC.APB1TimFreq_Value  #CubeMX  2026-11-12
CLOCK_APB2 = 72MHz  @EA_Buck.ioc RCC.APB2Freq_Value  #CubeMX  2026-11-12
CLOCK_APB2_TIM = 72MHz  @EA_Buck.ioc RCC.APB2TimFreq_Value  #CubeMX  2026-11-12
CLOCK_ADC = 12MHz = APB2/6  @EA_Buck.ioc RCC.ADCPresc/ADCFreqValue  #CubeMX  2026-11-12
