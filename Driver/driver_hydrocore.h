#ifndef __DRIVER_HYDROCORE_H
#define __DRIVER_HYDROCORE_H

#include <stdio.h>
#include "stm32f4xx.h"
#include "bsp_uart.h"

// 协议帧格式定义
#define PACKET_START_BYTE1        0xAA
#define PACKET_START_BYTE2        0xBB
#define PACKET_END_BYTE1          0xCC
#define PACKET_END_BYTE2          0xDD

// 命令码定义
#define DATA_TYPE_Thrusters       0x01
#define DATA_TYPE_SENSOR          0x02
#define DATA_TYPE_SERVO           0x03
#define DATA_TYPE_LIGHT           0x04
#define DATA_TYPE_FAST		      0x05
#define DATA_TYPE_SLOW		      0x06
#define CONTROL_DATA 		      0x01

#define THRUSTER_DATA_LENGTH      0x0C    
#define SENSOR_DATA_LENGTH        30    
#define FAST_SENSOR_DATA_LENGTH   42
#define SLOW_SENSOR_DATA_LENGTH   10
#define SERVO_DATA_LENGTH         0x04
#define LIGHT_DATA_LENGTH         0x04
#define MAX_PACKET_SIZE           32
#define CONTROL_DATA_LENGTH       0x10

#endif // __DRIVER_HYDROCORE_H