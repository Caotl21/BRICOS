#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "bsp_uart.h"
#include "driver_hydrocore.h"

// 定义双缓冲 (Ping-Pong Buffers)
#define RX_BUF_SIZE 128
static uint8_t s_ping_buf[RX_BUF_SIZE];
static uint8_t s_pong_buf[RX_BUF_SIZE];
static uint8_t *s_active_rt_dma_buf = s_ping_buf; // DMA 当前正在写的数组
static uint8_t s_active_nrt_dma_buf[RX_BUF_SIZE]; // 非实时通信专用 DMA 缓冲区

volatile uint32_t g_rt_drop_cnt = 0; // 实时通信丢包计数器
volatile uint32_t g_nrt_drop_cnt = 0; // 非实时通信丢包计数器

// RTOS 消息队列：零拷贝传递的是数据地址
static QueueHandle_t s_rt_rx_ptr_queue = NULL;
static QueueHandle_t s_nrt_rx_ptr_queue = NULL;

typedef struct {
    uint8_t *buf;
    uint16_t len;
} opi_rx_msg_t;

// BSP底层中断回调函数
static void Opi_RT_Comm_Rx_Callback(uint8_t *completed_buf, uint16_t len) {
    BaseType_t xWoken = pdFALSE;

    opi_rx_msg_t msg = { 
        .buf = completed_buf, 
        .len = len 
    };

    if(xQueueSendFromISR(s_rt_rx_ptr_queue, &msg, &xWoken) != pdPASS) {
        g_rt_drop_cnt++; // 实时通信丢包计数器递增
    }

    if (s_active_rt_dma_buf == s_ping_buf) {
        s_active_rt_dma_buf = s_pong_buf;
    } else {
        s_active_rt_dma_buf = s_ping_buf;
    }

    bsp_uart_start_dma_rx_normal(BSP_UART_OPI_RT, s_active_rt_dma_buf, RX_BUF_SIZE);
    portYIELD_FROM_ISR(xWoken);
}

static void Opi_NRT_Comm_Rx_Callback(uint8_t *data, uint16_t len) {
    BaseType_t xWoken = pdFALSE;

    opi_rx_msg_t msg = { 
        .buf = data, 
        .len = len 
    };

    if(xQueueSendFromISR(s_nrt_rx_ptr_queue, &msg, &xWoken) != pdPASS) {
        g_nrt_drop_cnt++; // 非实时通信丢包计数器递增
    }

    bsp_uart_start_dma_rx_normal(BSP_UART_OPI_NRT, s_active_nrt_dma_buf, RX_BUF_SIZE);
    portYIELD_FROM_ISR(xWoken);
}

void Task_Comm_Init(void){
    s_rt_rx_ptr_queue = xQueueCreate(2, sizeof(opi_rx_msg_t));
    s_nrt_rx_ptr_queue = xQueueCreate(4, sizeof(opi_rx_msg_t));

    configASSERT(s_rt_rx_ptr_queue != NULL);
    configASSERT(s_nrt_rx_ptr_queue != NULL);

    // 串口3 实时任务通信
    bsp_uart_register_rx_cb(BSP_UART_OPI_RT, Opi_RT_Comm_Rx_Callback);
    bsp_uart_start_dma_rx_normal(BSP_UART_OPI_RT, s_active_rt_dma_buf, RX_BUF_SIZE);

    // 串口4 非实时任务通信 (如 OTA)，如果需要也可以启用 DMA
    bsp_uart_register_rx_cb(BSP_UART_OPI_NRT, Opi_NRT_Comm_Rx_Callback);
    bsp_uart_start_dma_rx_normal(BSP_UART_OPI_NRT, s_active_nrt_dma_buf, RX_BUF_SIZE);
}

// 实时通信任务
void Task_RT_Comm_Opi(void *pvParameters) {
    opi_rx_msg_t rx_msg;

    while (1) {
        if (xQueueReceive(s_rt_rx_ptr_queue, &rx_msg, portMAX_DELAY) == pdPASS) {
            Driver_Protocol_Dispatch(rx_msg.buf, rx_msg.len);
        }
    }
}

// 非实时通信任务
void Task_NRT_Comm_Opi(void *pvParameters) {
    opi_rx_msg_t rx_msg;

    while (1) {
        if (xQueueReceive(s_nrt_rx_ptr_queue, &rx_msg, portMAX_DELAY) == pdPASS) {
            Driver_Protocol_Dispatch(rx_msg.buf, rx_msg.len);
        }
    }
}
