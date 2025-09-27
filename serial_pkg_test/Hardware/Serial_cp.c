#include "stm32f10x.h"
#include <stdio.h>
#include <stdarg.h>

// 数据包相关变量
uint16_t Serial_RxPWM[6] = {0};                    // 接收的PWM值数组
uint8_t Serial_RxPacket[MAX_PACKET_SIZE];          // 接收数据包缓冲区
uint8_t Serial_TxPacket[MAX_PACKET_SIZE];          // 发送数据包缓冲区
uint8_t Serial_RxFlag = 0;                         // 接收标志位

// 传感器数据（模拟数据）
SensorData_t Serial_SensorData = {
    .accel_x = 100, .accel_y = 200, .accel_z = 300,
    .gyro_x = 10, .gyro_y = 20, .gyro_z = 30,
    .depth = 1500, .temperature = 250
};

/**
  * 函    数：CRC8校验计算
  * 参    数：data 数据指针，length 数据长度
  * 返 回 值：CRC8校验值
  */
uint8_t Serial_CRC8(uint8_t *data, uint8_t length)
{
    uint8_t crc = 0x00;
    uint8_t i, j;
    
    for (i = 0; i < length; i++) {
        crc ^= data[i];
        for (j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x07;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

/**
  * 函    数：串口发送传感器数据包
  * 参    数：无
  * 返 回 值：无
  */
void Serial_SendSensorPacket(void)
{
    uint8_t packet[22];  // 起始+类型+长度+数据(16)+校验+结束
    uint8_t index = 0;
    uint8_t crc;
    
    // 构建数据包
    packet[index++] = PACKET_START_BYTE;        // 起始字节
    packet[index++] = DATA_TYPE_SENSOR;         // 数据类型
    packet[index++] = SENSOR_DATA_LENGTH;       // 数据长度
    
    // 传感器数据
    packet[index++] = (uint8_t)(Serial_SensorData.accel_x >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.accel_x & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.accel_y >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.accel_y & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.accel_z >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.accel_z & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.gyro_x >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.gyro_x & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.gyro_y >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.gyro_y & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.gyro_z >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.gyro_z & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.depth >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.depth & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.temperature >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.temperature & 0xFF);
    
    // 计算校验位（从数据类型开始到数据结束）
    crc = Serial_CRC8(&packet[1], index - 1);
    packet[index++] = crc;                      // 校验位
    packet[index++] = PACKET_END_BYTE;          // 结束字节
    
    // 发送数据包
    Serial_SendArray(packet, index);
}

/**
  * 函    数：USART1中断函数
  * 参    数：无
  * 返 回 值：无
  */
void USART1_IRQHandler(void)
{
    static uint8_t RxState = 0;         // 状态机状态
    static uint8_t DataType = 0;        // 数据类型
    static uint8_t DataLength = 0;      // 数据长度
    static uint8_t pRxPacket = 0;       // 数据包位置指针
    static uint8_t ReceivedCRC = 0;     // 接收到的校验值
    
    if (USART_GetITStatus(USART1, USART_IT_RXNE) == SET)
    {
        uint8_t RxData = USART_ReceiveData(USART1);
        
        switch (RxState)
        {
            case 0: // 等待起始字节
                if (RxData == PACKET_START_BYTE) {
                    RxState = 1;
                    pRxPacket = 0;
                }
                break;
                
            case 1: // 接收数据类型
                DataType = RxData;
                Serial_RxPacket[pRxPacket++] = RxData;
                RxState = 2;
                break;
                
            case 2: // 接收数据长度
                DataLength = RxData;
                Serial_RxPacket[pRxPacket++] = RxData;
                if (DataLength == PWM_DATA_LENGTH && DataType == DATA_TYPE_PWM) {
                    RxState = 3;
                } else {
                    RxState = 0; // 数据长度不匹配，重新开始
                }
                break;
                
            case 3: // 接收数据负载
                Serial_RxPacket[pRxPacket++] = RxData;
                if (pRxPacket >= (DataLength + 2)) { // +2是因为包含了数据类型和长度
                    RxState = 4;
                }
                break;
                
            case 4: // 接收校验位
                ReceivedCRC = RxData;
                RxState = 5;
                break;
                
            case 5: // 接收结束字节
                if (RxData == PACKET_END_BYTE) {
                    // 验证校验位
                    uint8_t CalculatedCRC = Serial_CRC8(Serial_RxPacket, pRxPacket);
                    if (CalculatedCRC == ReceivedCRC) {
                        // 解析PWM数据
                        if (DataType == DATA_TYPE_PWM) {
                            for (uint8_t i = 0; i < 6; i++) {
                                Serial_RxPWM[i] = (Serial_RxPacket[2 + i*2] << 8) | Serial_RxPacket[2 + i*2 + 1];
                                // 数据范围检查
                                if (Serial_RxPWM[i] > 5000) {
                                    Serial_RxPWM[i] = 5000;
                                }
                            }
                            Serial_RxFlag = 1; // 设置接收完成标志
                        }
                    }
                }
                RxState = 0; // 重新开始
                break;
                
            default:
                RxState = 0;
                break;
        }
        
        USART_ClearITPendingBit(USART1, USART_IT_RXNE);
    }
}
