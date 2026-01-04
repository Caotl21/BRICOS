#ifndef __MS5837_I2C_H
#define __MS5837_I2C_H

//extern u8 ms5837_flag;

void MS5837I2C_Init(void);
void MS5837I2C_Start(void);
void MS5837I2C_Stop(void);
void MS5837I2C_SendByte(uint8_t Byte);
uint8_t MS5837I2C_ReceiveByte(u8 ack);
void MS5837I2C_SendAck(uint8_t AckBit);
uint8_t MS5837I2C_ReceiveAck(void);

void MS5837_init(void);
void MS5837_Getdata(float * outTemp, float * outPress, float *outDepth);
void MS5837_GetTemp_1(void);
void MS5837_GetTemp_2(float * outTemp);
void MS5837_GetDepth_1(void);
void MS5837_GetDepth_2(float * outPress, float *outDepth);

#endif
