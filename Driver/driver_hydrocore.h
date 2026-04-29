#ifndef __DRIVER_HYDROCORE_H
#define __DRIVER_HYDROCORE_H

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "bsp_uart.h"

typedef enum {
    USE_CPU = 0,
    USE_DMA,
} protocol_send_mode_t;

// 协议帧格式定义
#define PACKET_START_BYTE1        0xAA
#define PACKET_START_BYTE2        0xBB
#define PACKET_END_BYTE1          0xCC
#define PACKET_END_BYTE2          0xDD

// 命令码定义
#define DATA_TYPE_THRUSTER        0x01

#define DATA_TYPE_STATE_BODY      0x02
#define DATA_TYPE_STATE_SYS       0x03
#define DATA_TYPE_STATE_ACTUATOR  0x04
#define DATA_TYPE_LOG             0x05
#define DATA_TYPE_CMD_ACK         0xFF

#define DATA_TYPE_OTA             0x10
#define DATA_TYPE_SET_PID_PARAM   0x11
#define DATA_TYPE_SET_SYS_MODE    0x12
#define DATA_TYPE_SET_MOTION_MODE 0x13
#define DATA_TYPE_SET_SERVO       0x14
#define DATA_TYPE_SET_TAM         0x15
#define DATA_TYPE_READ_PID_PARAM  0x16

// SERVICE ACK状态码定义
#define ACK_SUCCESS               0x01
#define INVALID_PARAM             0x02
#define UNKNOWN_CMD               0x03  
#define LENGTH_ERROR              0x04


typedef void (*protocol_cmd_handler_t)(const uint8_t *payload, uint16_t len);

void Driver_Protocol_Register(uint8_t cmd_id, protocol_cmd_handler_t handler);
void Driver_Protocol_Dispatch(const uint8_t *raw_frame, uint16_t total_len);
void Driver_Protocol_SendFrame(bsp_uart_port_t port, uint8_t cmd_id, const uint8_t *payload, uint8_t payload_len, protocol_send_mode_t send_mode);
void Driver_Protocol_SendAck(bsp_uart_port_t port, uint8_t cmd_id, uint8_t ack_code, uint8_t seq_number, protocol_send_mode_t send_mode);

#endif // __DRIVER_HYDROCORE_H

