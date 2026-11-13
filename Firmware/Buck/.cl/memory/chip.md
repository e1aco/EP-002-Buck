# 芯片资源（工程实际分配）
CHIP_RAM = 20KB (0x20000000-0x20004FFF)  @Buck.uvprojx Cpu IRAM  #代码核对  2026-10-23
CHIP_FLASH = 64KB (0x08000000-0x0800FFFF)  @Buck.uvprojx Cpu IROM  #代码核对  2026-10-23
CHIP_LIMIT = 64KB Flash / 20KB SRAM（datasheet 上限，待手册复核）  @缺 datasheet  #待实测确认  2026-10-23
STACK_SIZE = 0x400 (1024B)  @MDK-ARM/startup_stm32f103xb.s:32  #代码核对  2026-10-23
HEAP_SIZE = 0x200 (512B)  @MDK-ARM/startup_stm32f103xb.s:43  #代码核对  2026-10-23
