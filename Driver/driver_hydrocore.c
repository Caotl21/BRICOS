#include "driver_hydrocore.h"

#include <stddef.h>
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

static protocol_cmd_handler_t s_handlers[256] = {NULL};
static SemaphoreHandle_t s_protocol_mutex = NULL;
static uint8_t s_tx_frame[262];

static void prv_protocol_mutex_init(void)
{
    taskENTER_CRITICAL();
    if (s_protocol_mutex == NULL) {
        s_protocol_mutex = xSemaphoreCreateMutex();
    }
    taskEXIT_CRITICAL();
}

static uint8_t calculate_checksum(const uint8_t *data, uint16_t len)
{
    uint8_t checksum = 0;

    for (uint16_t i = 0; i < len; i++) {
        checksum ^= data[i];
    }

    return checksum;
}

void Driver_Protocol_Register(uint8_t cmd_id, protocol_cmd_handler_t handler)
{
    s_handlers[cmd_id] = handler;
}

void Driver_Protocol_Dispatch(const uint8_t *raw_frame, uint16_t total_len)
{
    if (total_len < 7u || raw_frame == NULL) {
        return;
    }

    if (raw_frame[0] != PACKET_START_BYTE1 || raw_frame[1] != PACKET_START_BYTE2) {
        return;
    }

    if (total_len < (uint16_t)(raw_frame[3] + 7u)) {
        return;
    }

    if (raw_frame[total_len - 2u] != PACKET_END_BYTE1 || raw_frame[total_len - 1u] != PACKET_END_BYTE2) {
        return;
    }

    if (calculate_checksum(&raw_frame[2], (uint16_t)raw_frame[3] + 2u) != raw_frame[4u + raw_frame[3]]) {
        return;
    }

    protocol_cmd_handler_t handler = s_handlers[raw_frame[2]];
    if (handler != NULL) {
        handler(&raw_frame[4], raw_frame[3]);
    } else {
        Driver_Protocol_SendAck(BSP_UART_OPI_NRT, raw_frame[2], UNKNOWN_CMD, 0, USE_DMA);
    }
}

void Driver_Protocol_SendFrame(bsp_uart_port_t port, uint8_t cmd_id, const uint8_t *payload, uint8_t payload_len, protocol_send_mode_t send_mode)
{
    uint16_t total_len;

    prv_protocol_mutex_init();
    if (s_protocol_mutex != NULL) {
        if (xSemaphoreTake(s_protocol_mutex, portMAX_DELAY) != pdTRUE) {
            return;
        }
    }

    total_len = (uint16_t)payload_len + 7u;
    if (total_len > sizeof(s_tx_frame)) {
        if (s_protocol_mutex != NULL) {
            xSemaphoreGive(s_protocol_mutex);
        }
        return;
    }

    s_tx_frame[0] = PACKET_START_BYTE1;
    s_tx_frame[1] = PACKET_START_BYTE2;
    s_tx_frame[2] = cmd_id;
    s_tx_frame[3] = payload_len;

    if ((payload_len > 0u) && (payload != NULL)) {
        memcpy(&s_tx_frame[4], payload, payload_len);
    }

    s_tx_frame[4u + payload_len] = calculate_checksum(&s_tx_frame[2], (uint16_t)payload_len + 2u);
    s_tx_frame[5u + payload_len] = PACKET_END_BYTE1;
    s_tx_frame[6u + payload_len] = PACKET_END_BYTE2;

    if (send_mode == USE_DMA) {
        if (!bsp_uart_send_dma(port, s_tx_frame, (uint16_t)total_len)) {
            bsp_uart_send_buffer(port, s_tx_frame, total_len);
        }
    } else {
        bsp_uart_send_buffer(port, s_tx_frame, total_len);
    }

    if (s_protocol_mutex != NULL) {
        xSemaphoreGive(s_protocol_mutex);
    }
}

void Driver_Protocol_SendAck(bsp_uart_port_t port, uint8_t cmd_id, uint8_t ack_code, uint8_t seq_number, protocol_send_mode_t send_mode)
{
    uint8_t ack_payload[4] = {0};
    ack_payload[0] = DATA_TYPE_CMD_ACK;
    ack_payload[1] = cmd_id;
    ack_payload[2] = ack_code;
    ack_payload[3] = seq_number;

    Driver_Protocol_SendFrame(port, DATA_TYPE_CMD_ACK, ack_payload, sizeof(ack_payload), send_mode);
}
