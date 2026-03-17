#include "driver_hydrocore.h"
#include <stddef.h>

// O(1) 极速路由表：占用 256 * 4 = 1024 字节 RAM，换取绝对的实时性
static protocol_cmd_handler_t s_handlers[256] = {NULL};

// 内部函数：计算异或校验和
static uint8_t calculate_checksum(const uint8_t *data, uint16_t len) {
    uint8_t checksum = 0;
    for (uint16_t i = 0; i < len; i++) {
        checksum ^= data[i]; // 简单异或校验
    }
    return checksum;
}

// 注册协议命令处理函数
void Driver_Protocol_Register(uint8_t cmd_id, protocol_cmd_handler_t handler) {
    s_handlers[cmd_id] = handler;
}

// 串口数据包解包与分发
void Driver_Protocol_Dispatch(const uint8_t *raw_frame, uint16_t total_len) {
    // 最小包长度检查：2字节帧头 + 1字节命令 + 1字节长度 + 1字节校验 + 2字节帧尾 = 7字节
    if (total_len < 7 || raw_frame == NULL) return;

    // 帧头检查
    if (raw_frame[0] != PACKET_START_BYTE1 || raw_frame[1] != PACKET_START_BYTE2) return;

    // 包长度验证：帧头(2) + 命令(1) + 长度(1) + 数据(payload_len) + 校验(1) + 帧尾(2)
    if (total_len < (uint16_t)(raw_frame[3] + 7)) return; 

    // 帧尾检查
    if (raw_frame[total_len - 2] != PACKET_END_BYTE1 || raw_frame[total_len - 1] != PACKET_END_BYTE2) return;

    // 校验和验证
    uint8_t checksum = calculate_checksum(&raw_frame[2], raw_frame[3] + 2); // 从命令码开始到数据末尾的校验
    if (checksum != raw_frame[4 + raw_frame[3]]) {
        return;
    }

    // 查找并调用对应的处理函数
    protocol_cmd_handler_t handler = s_handlers[raw_frame[2]];
    if (handler != NULL) {
        handler(&raw_frame[4], raw_frame[3]); // 调用处理函数，传递数据载荷和长度
    }
}

