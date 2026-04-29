#include "task_shell_transport_nrt.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "bsp_uart.h"
#include "sys_port.h"

#define SHELL_UART_RX_QUEUE_DEPTH  128u
#define SHELL_UART_RX_TASK_STACK   256u
#define SHELL_UART_RX_TASK_PRIO    3u

static shell_rx_cb_t s_shell_rx_cb = 0;
static QueueHandle_t s_shell_rx_queue = NULL;
static TaskHandle_t s_shell_rx_task = NULL;
static const shell_peer_t s_uart_peer = {
    .type = SHELL_TP_UART_STREAM,
    .session_id = 0u
};

static void prv_uart_debug_rx_isr(uint8_t *data, uint16_t len)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    uint16_t i;

    if ((data == NULL) || (len == 0u) || (s_shell_rx_queue == NULL)) {
        return;
    }

    for (i = 0u; i < len; i++) {
        (void)xQueueSendFromISR(s_shell_rx_queue, &data[i], &xHigherPriorityTaskWoken);
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static void prv_shell_uart_rx_task(void *pvParameters)
{
    uint8_t ch;
    shell_rx_cb_t cb;
    (void)pvParameters;

    while (1) {
        if (xQueueReceive(s_shell_rx_queue, &ch, portMAX_DELAY) == pdPASS) {
            SYS_ENTER_CRITICAL();
            cb = s_shell_rx_cb;
            SYS_EXIT_CRITICAL();

            if (cb != 0) {
                cb(&s_uart_peer, &ch, 1u);
            }
        }
    }
}

static int prv_uart5_init(void)
{
    if (s_shell_rx_queue == NULL) {
        s_shell_rx_queue = xQueueCreate(SHELL_UART_RX_QUEUE_DEPTH, sizeof(uint8_t));
    }

    if (s_shell_rx_queue == NULL) {
        return -1;
    }

    if (s_shell_rx_task == NULL) {
        if (xTaskCreate(prv_shell_uart_rx_task,
                        "ShellU5Rx",
                        SHELL_UART_RX_TASK_STACK,
                        NULL,
                        SHELL_UART_RX_TASK_PRIO,
                        &s_shell_rx_task) != pdPASS) {
            return -2;
        }
    }

    return 0;
}

static int prv_uart5_start(shell_rx_cb_t rx_cb)
{
    static const uint8_t s_boot_status[] = "startup_success\r\n";

    if (rx_cb == 0) {
        return -1;
    }

    SYS_ENTER_CRITICAL();
    s_shell_rx_cb = rx_cb;
    SYS_EXIT_CRITICAL();

    bsp_uart_register_rx_cb(BSP_UART_DEBUG, prv_uart_debug_rx_isr);
    bsp_uart_send_buffer(BSP_UART_DEBUG, s_boot_status, (uint16_t)(sizeof(s_boot_status) - 1u));

    return 0;
}

static int prv_uart5_send(const shell_peer_t *peer, const uint8_t *data, uint16_t len)
{
    (void)peer;

    if (data == 0) {
        return -1;
    }
    if (len == 0u) {
        return 0;
    }

    bsp_uart_send_buffer(BSP_UART_DEBUG, data, len);
    return 0;
}

static int prv_uart5_stop(void)
{
    bsp_uart_register_rx_cb(BSP_UART_DEBUG, NULL);

    SYS_ENTER_CRITICAL();
    s_shell_rx_cb = 0;
    SYS_EXIT_CRITICAL();

    if (s_shell_rx_queue != NULL) {
        xQueueReset(s_shell_rx_queue);
    }

    return 0;
}

static const shell_transport_vtable_t s_shell_uart5_vtable = {
    prv_uart5_init,
    prv_uart5_start,
    prv_uart5_send,
    prv_uart5_stop
};

const shell_transport_vtable_t *Task_ShellTransportNRT_GetVTable(void)
{
    return &s_shell_uart5_vtable;
}
