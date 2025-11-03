/**
 ******************************************************************************
 * @file    ymodem.c
 * @author  Bootloader Team
 * @brief   Ymodem协议实现
 ******************************************************************************
 */

#include "ymodem.h"
#include "stm32f10x_usart.h"
#include <string.h>
#include <stdlib.h>

/* 外部变量 */
extern uint32_t SystemCoreClock;

/* 私有变量 */
static uint32_t tick_start = 0;

/**
 * @brief  获取系统滴答计数(毫秒)
 */
static uint32_t GetTick(void)
{
    return tick_start;
}

/**
 * @brief  延时(毫秒)
 */
static void Delay_ms(uint32_t ms)
{
    uint32_t start = GetTick();
    while ((GetTick() - start) < ms);
}

/**
 * @brief  SysTick中断处理(需要在stm32f10x_it.c中调用)
 */
void Ymodem_IncTick(void)
{
    tick_start++;
}

/**
 * @brief  发送单个字节到UART
 */
void Ymodem_SendByte(uint8_t c)
{
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    USART_SendData(USART1, c);
}

/**
 * @brief  从UART接收单个字节(带超时)
 * @retval 0:成功, -1:超时
 */
int32_t Ymodem_ReceiveByte(uint8_t *c, uint32_t timeout)
{
    uint32_t start = GetTick();
    
    while (USART_GetFlagStatus(USART1, USART_FLAG_RXNE) == RESET)
    {
        if ((GetTick() - start) > timeout)
        {
            return -1; // 超时
        }
    }
    
    *c = USART_ReceiveData(USART1);
    return 0;
}

/**
 * @brief  计算CRC16校验 (CCITT标准)
 */
uint16_t Ymodem_CalcCRC16(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0;
    
    while (len--)
    {
        crc ^= (uint16_t)(*data++) << 8;
        
        for (uint8_t i = 0; i < 8; i++)
        {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc = crc << 1;
        }
    }
    
    return crc;
}

/**
 * @brief  接收一个Ymodem数据包
 * @param  data: 数据缓冲区
 * @param  length: 接收到的数据长度指针
 * @param  timeout: 超时时间(毫秒)
 * @retval 0:成功, <0:错误
 */
int32_t Ymodem_ReceivePacket(uint8_t *data, int32_t *length, uint32_t timeout)
{
    uint8_t c;
    uint32_t packet_size;
    int32_t i;
    
    *length = 0;
    
    // 接收包头
    if (Ymodem_ReceiveByte(&c, timeout) != 0)
        return YMODEM_TIMEOUT;
    
    switch (c)
    {
        case SOH:
            packet_size = PACKET_SIZE;
            break;
        case STX:
            packet_size = PACKET_1K_SIZE;
            break;
        case EOT:
            return YMODEM_OK;
        case CA:
            if (Ymodem_ReceiveByte(&c, timeout) == 0 && c == CA)
                return YMODEM_ABORT;
            else
                return YMODEM_ERROR;
        case CRC16:
            return YMODEM_OK;
        default:
            return YMODEM_ERROR;
    }
    
    // 接收序列号
    uint8_t seq[2];
    if (Ymodem_ReceiveByte(&seq[0], timeout) != 0)
        return YMODEM_TIMEOUT;
    if (Ymodem_ReceiveByte(&seq[1], timeout) != 0)
        return YMODEM_TIMEOUT;
    
    // 检查序列号
    if (seq[0] != (uint8_t)(~seq[1]))
        return YMODEM_ERROR;
    
    // 接收数据
    for (i = 0; i < packet_size; i++)
    {
        if (Ymodem_ReceiveByte(&data[i], timeout) != 0)
            return YMODEM_TIMEOUT;
    }
    
    // 接收CRC16
    uint8_t crc_h, crc_l;
    if (Ymodem_ReceiveByte(&crc_h, timeout) != 0)
        return YMODEM_TIMEOUT;
    if (Ymodem_ReceiveByte(&crc_l, timeout) != 0)
        return YMODEM_TIMEOUT;
    
    uint16_t crc_received = (crc_h << 8) | crc_l;
    uint16_t crc_calculated = Ymodem_CalcCRC16(data, packet_size);
    
    if (crc_received != crc_calculated)
        return YMODEM_ERROR;
    
    *length = packet_size;
    return YMODEM_OK;
}

/**
 * @brief  解析文件信息(从第一个包)
 */
int32_t Ymodem_ParseFileInfo(const uint8_t *packet, FileInfo_t *file_info)
{
    const uint8_t *file_ptr;
    uint32_t file_size = 0;

    // 获取文件名
    file_ptr = packet;
    if (*file_ptr == 0)
    {
        // 空包，传输结束
        return YMODEM_ERROR;
    }
    
    // 复制文件名
    strncpy((char *)file_info->file_name, (char *)file_ptr, FILE_NAME_LENGTH - 1);
    file_info->file_name[FILE_NAME_LENGTH - 1] = '\0';
    
    // 跳过文件名
    while (*file_ptr != 0)
        file_ptr++;
    file_ptr++; // 跳过NULL
    
    // 获取文件大小
    if (*file_ptr != 0)
    {
        file_size = atoi((char *)file_ptr);
    }
    
    file_info->file_size = file_size;
    
    return YMODEM_OK;
}

/**
 * @brief  Ymodem接收文件
 * @param  buf: 接收缓冲区
 * @param  file_info: 文件信息结构体指针
 * @retval 接收到的字节数，<0表示错误
 */
int32_t Ymodem_Receive(uint8_t *buf, FileInfo_t *file_info)
{
    uint8_t packet_data[PACKET_1K_SIZE + PACKET_OVERHEAD];
    int32_t packet_length;
    uint32_t errors = 0;
    uint32_t file_done = 0;
    uint32_t session_done = 0;
    uint32_t packets_received = 0;
    uint32_t file_size = 0;
    uint32_t size = 0;
    int32_t result;
    
    // 发送'C'表示准备接收(使用CRC16)
    for (uint32_t i = 0; i < 3; i++)
    {
        Ymodem_SendByte(CRC16);
        Delay_ms(100);
    }
    
    while (!session_done)
    {
        result = Ymodem_ReceivePacket(packet_data, &packet_length, YMODEM_PACKET_TIMEOUT);
        
        switch (result)
        {
            case YMODEM_OK:
                errors = 0;
                
                if (packet_length == 0)
                {
                    // 收到EOT
                    Ymodem_SendByte(NAK);
                    result = Ymodem_ReceivePacket(packet_data, &packet_length, YMODEM_PACKET_TIMEOUT);
                    if (result == YMODEM_OK && packet_length == 0)
                    {
                        Ymodem_SendByte(ACK);
                        file_done = 1;
                    }
                }
                else if (packet_length > 0)
                {
                    if (packets_received == 0)
                    {
                        // 第一个包，包含文件信息
                        if (Ymodem_ParseFileInfo(packet_data, file_info) == YMODEM_OK)
                        {
                            file_size = file_info->file_size;
                            Ymodem_SendByte(ACK);
                            Ymodem_SendByte(CRC16);
                        }
                        else
                        {
                            // 空包，会话结束
                            Ymodem_SendByte(ACK);
                            session_done = 1;
                            break;
                        }
                    }
                    else
                    {
                        // 数据包
                        uint32_t bytes_to_copy = packet_length;
                        
                        if (file_size > 0)
                        {
                            if (size + bytes_to_copy > file_size)
                                bytes_to_copy = file_size - size;
                        }
                        
                        memcpy(buf + size, packet_data, bytes_to_copy);
                        size += bytes_to_copy;
                        
                        Ymodem_SendByte(ACK);
                    }
                    
                    packets_received++;
                }
                
                if (file_done)
                {
                    session_done = 1;
                }
                break;
                
            case YMODEM_ABORT:
                Ymodem_SendByte(ACK);
                return YMODEM_ABORT;
                
            case YMODEM_TIMEOUT:
            case YMODEM_ERROR:
            default:
                errors++;
                if (errors > MAX_ERRORS)
                {
                    Ymodem_SendByte(CA);
                    Ymodem_SendByte(CA);
                    return YMODEM_ERROR;
                }
                Ymodem_SendByte(CRC16);
                break;
        }
    }
    
    return size;
}

