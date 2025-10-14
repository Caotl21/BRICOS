#ifndef __SERIAL_H
#define __SERIAL_H

#include <stdio.h>
#include "stm32f10x.h"

#define CMD_UartNum        1  // 模块通信口

void Serial_Init(void);
void Serial_SendByte(uint8_t Byte);
void Serial_SendArray(uint8_t *Array, uint16_t Length);
void Serial_SendString(char *String);
void Serial_SendNumber(uint32_t Number, uint8_t Length);
void Serial_Printf(char *format, ...);
int UART_Write(U8 n, const U8 *buf, int Len);

uint8_t Serial_GetRxFlag(void);
uint8_t Serial_GetRxData(void);

#endif
