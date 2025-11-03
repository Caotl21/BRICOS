/**
 ******************************************************************************
 * @file    ymodem.h
 * @author  Bootloader Team
 * @brief   Ymodem协议头文件
 ******************************************************************************
 */

#ifndef __YMODEM_H
#define __YMODEM_H

#include "stm32f10x.h"

/* Ymodem协议定义 */
#define PACKET_SEQNO_INDEX      (1)
#define PACKET_SEQNO_COMP_INDEX (2)

#define PACKET_HEADER           (3)
#define PACKET_TRAILER          (2)
#define PACKET_OVERHEAD         (PACKET_HEADER + PACKET_TRAILER)
#define PACKET_SIZE             (128)
#define PACKET_1K_SIZE          (1024)

#define FILE_NAME_LENGTH        (64)
#define FILE_SIZE_LENGTH        (16)

/* Ymodem控制字符 */
#define SOH                     (0x01)  /* 128字节数据包开始 */
#define STX                     (0x02)  /* 1K字节数据包开始 */
#define EOT                     (0x04)  /* 传输结束 */
#define ACK                     (0x06)  /* 应答 */
#define NAK                     (0x15)  /* 否定应答 */
#define CA                      (0x18)  /* 取消传输(连续5个) */
#define CRC16                   (0x43)  /* 'C' == 0x43, 请求CRC16校验 */

/* Ymodem接收状态 */
#define YMODEM_OK               (0)
#define YMODEM_ERROR            (-1)
#define YMODEM_ABORT            (-2)
#define YMODEM_TIMEOUT          (-3)
#define YMODEM_LIMIT            (-4)

/* 超时定义 (毫秒) */
#define YMODEM_TIMEOUT_MS       (1000)
#define YMODEM_PACKET_TIMEOUT   (1000)
#define YMODEM_NAK_TIMEOUT      (100000)

/* 最大错误次数 */
#define MAX_ERRORS              (5)

/* 文件信息结构体 */
typedef struct
{
    uint8_t  file_name[FILE_NAME_LENGTH];
    uint32_t file_size;
} FileInfo_t;

/* 函数声明 */

/**
 * @brief  Ymodem接收文件
 * @param  buf: 接收缓冲区
 * @param  file_info: 文件信息结构体指针
 * @retval 接收到的字节数，<0表示错误
 */
int32_t Ymodem_Receive(uint8_t *buf, FileInfo_t *file_info);

/**
 * @brief  发送单个字节到UART
 * @param  c: 要发送的字节
 * @retval None
 */
void Ymodem_SendByte(uint8_t c);

/**
 * @brief  从UART接收单个字节(带超时)
 * @param  c: 接收字节的指针
 * @param  timeout: 超时时间(毫秒)
 * @retval 0:成功, -1:超时
 */
int32_t Ymodem_ReceiveByte(uint8_t *c, uint32_t timeout);

/**
 * @brief  计算CRC16校验
 * @param  data: 数据指针
 * @param  len: 数据长度
 * @retval CRC16值
 */
uint16_t Ymodem_CalcCRC16(const uint8_t *data, uint32_t len);

/**
 * @brief  接收一个Ymodem数据包
 * @param  data: 数据缓冲区
 * @param  length: 接收到的数据长度指针
 * @param  timeout: 超时时间(毫秒)
 * @retval 0:成功, <0:错误
 */
int32_t Ymodem_ReceivePacket(uint8_t *data, int32_t *length, uint32_t timeout);

/**
 * @brief  解析文件信息包
 * @param  packet_data: 数据包内容
 * @param  file_info: 文件信息结构体指针
 * @retval 0:成功, <0:错误
 */
int32_t Ymodem_ParseFileInfo(const uint8_t *packet_data, FileInfo_t *file_info);

/**
 * @brief  SysTick中断处理(需要在stm32f10x_it.c中调用)
 * @retval None
 */
void Ymodem_IncTick(void);

#endif /* __YMODEM_H */

