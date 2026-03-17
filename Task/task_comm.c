#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "bsp_uart.h"
#include "driver_hydrocore.h"

// 定义双缓冲 (Ping-Pong Buffers)
#define RX_BUF_SIZE 128
static uint8_t s_ping_buf[RX_BUF_SIZE];
static uint8_t s_pong_buf[RX_BUF_SIZE];
static uint8_t *s_active_dma_buf = s_ping_buf; // DMA 当前正在写的数组

// RTOS 消息队列：零拷贝传递的是数据地址
static QueueHandle_t s_rx_ptr_queue = NULL;

// BSP底层中断回调函数
static void Opi_Uart_Rx_Callback(uint8_t *completed_buf, uint16_t len) {
    BaseType_t xWoken = pdFALSE;

    xQueueSendFromISR(s_rx_ptr_queue, &completed_buf, &xWoken);

    if (s_active_dma_buf == s_ping_buf) {
        s_active_dma_buf = s_pong_buf;
    } else {
        s_active_dma_buf = s_ping_buf;
    }

    bsp_uart_start_dma_rx_normal(BSP_UART_OPI, s_active_dma_buf, RX_BUF_SIZE);

    portYIELD_FROM_ISR(xWoken);
}

// 通信主任务：阻塞死等 0 CPU占用
void Task_Comm_Opi(void *pvParameters) {
    s_rx_ptr_queue = xQueueCreate(2, sizeof(uint8_t*));

    //启动DMA
    bsp_uart_register_rx_cb(BSP_UART_OPI, Opi_Uart_Rx_Callback);
    bsp_uart_start_dma_rx_normal(BSP_UART_OPI, s_active_dma_buf, RX_BUF_SIZE);

    uint8_t *ready_parse_buf;

    while (1) {
        if (xQueueReceive(s_rx_ptr_queue, &ready_parse_buf, portMAX_DELAY) == pdPASS) {
            Driver_Protocol_Dispatch(ready_parse_buf, RX_BUF_SIZE);
        }
    }
}