# 芯片资源（工程实际分配）

CHIP = STM32F103C8T6 LQFP48  @EA_Buck.ioc Mcu.CPN  #CubeMX  2026-11-12
CHIP_FLASH = 64KB 0x08000000-0x0800FFFF  @EA_Buck.sct LR_IROM1  #代码核对  2026-11-12
CHIP_RAM = 20KB 0x20000000-0x20004FFF  @EA_Buck.sct RW_IRAM1  #代码核对  2026-11-12
STACK_SIZE = 1024B 0x400  @startup_stm32f103xb.s Stack_Size  #代码核对  2026-11-12
HEAP_SIZE = 512B 0x200  @startup_stm32f103xb.s Heap_Size  #代码核对  2026-11-12
HSE_VALUE = 8MHz  @EA_Buck.ioc RCC.VCOOutput2Freq_Value  #CubeMX  2026-11-12
