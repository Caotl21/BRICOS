#ifndef __SERIAL_H
#define __SERIAL_H

#include <stdio.h>
#include "stm32f4xx.h"
#include "Types.h"



// ���ݰ�Э�鶨��
#define PACKET_START_BYTE1     0xAA
#define PACKET_START_BYTE2     0xBB
#define PACKET_END_BYTE1       0xCC
#define PACKET_END_BYTE2       0xDD
#define DATA_TYPE_Thrusters    0x01
#define DATA_TYPE_SENSOR       0x02
#define DATA_TYPE_SERVO        0x03
#define DATA_TYPE_LIGHT        0x04
#define DATA_TYPE_FAST		   0x05
#define DATA_TYPE_SLOW		   0x06

// ��������
#define THRUSTER_DATA_LENGTH      0x0C    // 6��PWMֵ��ÿ��2�ֽ�
#define SENSOR_DATA_LENGTH   30    // ���������ݳ���
#define FAST_SENSOR_DATA_LENGTH   42
#define SLOW_SENSOR_DATA_LENGTH   10
#define SERVO_DATA_LENGTH    0x04    // 2�����PWMֵ��ÿ��2�ֽ�
#define LIGHT_DATA_LENGTH    0x04    // 2��LEDֵ��4�ֽ�
#define MAX_PACKET_SIZE      32    // ������ݰ���С


extern struct_UartFifo Uart1Fifo;
extern volatile uint8_t Serial_RxPWM_Control[64];

#define IM948_Fifo_in(RxByte) if (FifoSize > Uart1Fifo.Cnt)\
                        {\
                            Uart1Fifo.RxBuf[Uart1Fifo.In] = (RxByte);\
                            if(++Uart1Fifo.In >= FifoSize)\
                            {\
                                Uart1Fifo.In = 0;\
                            }\
                            ++Uart1Fifo.Cnt;\
                        }

// PWM�������ݽṹ
extern uint16_t Serial_RxPWM_Thruster[6];        // ���յ�6��PWMֵ
extern uint16_t Serial_RxPWM_Servo[2];           // ���յĶ��PWMֵ
extern uint16_t Serial_RxPWM_Light[2];               // ���յ�LEDֵ
extern uint8_t Serial_RxPacket[MAX_PACKET_SIZE];  // �������ݰ�������
extern uint8_t Serial_TxPacket[MAX_PACKET_SIZE];  // �������ݰ�������
extern uint8_t Serial_RxData;
extern volatile uint8_t g_event_im948_received;

extern uint8_t Serial_RxFlag;

// ���������ݽṹ
typedef struct {
    int16_t accel_x1, accel_y1, accel_z1;    // JY901B数据
    int16_t gyro_x1, gyro_y1, gyro_z1;        
    int16_t Wquat_1, Xquat_1, Yquat_1, Zquat_1;     
	int16_t accel_x2, accel_y2, accel_z2;    // IM948数据
    int16_t gyro_x2, gyro_y2, gyro_z2;       
    int16_t Wquat_2, Xquat_2, Yquat_2, Zquat_2;
    int16_t press;                        // 水压计数据
    int16_t temperature_water;             
    int16_t humidity;                     //DHT11数据
	int16_t temperature_AUV;
	int16_t voltage;
	int16_t current;
} SensorData_t;

extern SensorData_t Serial_SensorData;

void USART2_Init(uint32_t baudrate);
void USART1_Init(uint32_t baudrate);
void Serial_Init(void);
void Serial_SendByte(uint8_t Byte);
void Serial_SendArray(uint8_t *Array, uint16_t Length);
void Serial_SendString(char *String);
void Serial_SendNumber(uint32_t Number, uint8_t Length);
void Serial_Printf(char *format, ...);
int IM948_Write(const U8 *buf, int Len);

uint8_t Serial_GetRxFlag(void);
uint8_t Serial_GetRxData(void);
void Serial_SendPacket2PC(uint8_t DataType, uint8_t DataLength, uint8_t *DataPayload);
void Serial_SendSensorPacket(void);
uint8_t Serial_CRC8(uint8_t *data, uint8_t length);

void Fast_Serial_SendSensorPacket(void);
void Slow_Serial_SendSensorPacket(void);

#endif
