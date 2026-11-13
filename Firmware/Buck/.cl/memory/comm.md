# 通信事实
UART1_BAUD = 115200 8N1  @Core/Src/usart.c:43  #代码核对  2026-10-23
UART1_RX_DMA = DMA1_Ch5 circular  @Core/Src/usart.c:89-95  #代码核对  2026-10-23
I2C1_SPEED = 400kHz 7bit  @Core/Src/i2c.c:41  #代码核对  2026-11-08
I2C1_FALLBACK = 累计 12 次错误 → 单向回退 100kHz（板上 PB6/PB7 无上拉）  @module/drv/drv_oled.c  #推演链  2026-11-08
DBG_RTT = 可用（通道0，JLinkRTTClient 直读）  @jlink_rtt.py 实测  #实测  2026-10-23
DBG_BACKEND_SW = CL_DBG_BACKEND 0=RTT/1=UART  @module/config.h:18  #代码核对  2026-10-23
