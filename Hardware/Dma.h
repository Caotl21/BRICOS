#ifndef __DMA_H
#define __DMA_H

#include "stm32f4xx.h"
#include "stm32f4xx_conf.h"
#include "Types.h"
#include "Serial.h"

extern struct_UartFifo Uart1Fifo;
extern struct_JY901BFifo JY901BFifo;

void USART1_DMA_Init(void);
void USART2_DMA_Init(void);
void USART3_DMA_Init(void);

#endif

