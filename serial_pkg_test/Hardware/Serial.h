#ifndef __SERIAL_H
#define __SERIAL_H

#include <stdio.h>

// 数据包协议定义
#define PACKET_START_BYTE1     0xAA
#define PACKET_START_BYTE2     0xBB
#define PACKET_END_BYTE1       0xCC
#define PACKET_END_BYTE2       0xDD
#define DATA_TYPE_PWM          0x01
#define DATA_TYPE_SENSOR       0x02


// 数据类型
#define PWM_DATA_LENGTH      0x0C    // 6个PWM值，每个2字节
#define SENSOR_DATA_LENGTH   16    // 传感器数据长度
#define MAX_PACKET_SIZE      32    // 最大数据包大小

// PWM接收数据结构
extern uint16_t Serial_RxPWM[6];        // 接收的6个PWM值
extern uint8_t Serial_RxPacket[MAX_PACKET_SIZE];  // 接收数据包缓冲区
extern uint8_t Serial_TxPacket[MAX_PACKET_SIZE];  // 发送数据包缓冲区

// 传感器数据结构
typedef struct {
    int16_t accel_x, accel_y, accel_z;    // 加速度计
    int16_t gyro_x, gyro_y, gyro_z;       // 陀螺仪
    int16_t depth;                        // 深度
    int16_t temperature;                  // 温度
} SensorData_t;

extern SensorData_t Serial_SensorData;

void Serial_Init(void);
void Serial_SendByte(uint8_t Byte);
void Serial_SendArray(uint8_t *Array, uint16_t Length);
void Serial_SendString(char *String);
void Serial_SendNumber(uint32_t Number, uint8_t Length);
void Serial_Printf(char *format, ...);

void Serial_SendSensorPacket(void);

uint8_t Serial_GetRxFlag(void);
uint8_t Serial_CRC8(uint8_t *data, uint8_t length);

#endif
