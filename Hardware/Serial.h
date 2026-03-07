#ifndef __SERIAL_H
#define __SERIAL_H

#include <stdio.h>
#include "stm32f4xx.h" 

extern uint8_t Serial_RxData;
extern uint8_t Serial_RxFlag;


void UART_Init(void);
void Serial_SendByte(uint8_t Byte);
void Serial_SendArray(uint8_t *Array, uint16_t Length);
void Serial_SendString(char *String);
void Serial_SendNumber(uint32_t Number, uint8_t Length);
void Serial_Printf(char *format, ...);

uint8_t Serial_GetRxFlag(void);
uint8_t Serial_GetRxData(void);
void Serial_SendPacket2PC(uint8_t DataType, uint8_t DataLength, uint8_t *DataPayload);
void Serial_SendSensorPacket(void);
uint8_t Serial_CRC8(uint8_t *data, uint8_t length);

void UART_SendString(const char *str);
void UART_SendByte(uint8_t byte);
void UART_SendInt(int32_t value);
void UART_SendHex(uint32_t value);


#endif
