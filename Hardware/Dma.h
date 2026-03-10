#ifndef __DMA_H
#define __DMA_H

#include "stm32f4xx.h"
#include "stm32f4xx_conf.h"
#include "Types.h"
#include "Serial.h"

extern struct_UartFifo Uart1Fifo;
extern struct_JY901BFifo JY901BFifo;

void DMA_User_Init(void);

#endif

