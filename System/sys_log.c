#include "sys_log.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#include "bsp_uart.h"

#define LOG_TMP_BUF_SIZE   192u
#define LOG_QUEUE_DEPTH     16u
#define LOG_TASK_STACK      512u
#define LOG_TASK_PRIO       3u

typedef struct
{
    uint8_t len;
    char text[LOG_TMP_BUF_SIZE];
} log_msg_t;

static SemaphoreHandle_t s_log_mutex = NULL;
static QueueHandle_t s_log_queue = NULL;
static TaskHandle_t s_log_task = NULL;

static char s_log_work_buf[LOG_TMP_BUF_SIZE];
static log_msg_t s_log_msg;

static const char *Log_Level_To_Prefix(log_level_t level)
{
    switch (level) {
        case LOG_LEVEL_ERROR:   return "[ERROR] ";
        case LOG_LEVEL_WARNING: return "[WARNING] ";
        case LOG_LEVEL_INFO:    return "[INFO] ";
        case LOG_LEVEL_DEBUG:   return "[DEBUG] ";
        default:                return "[INFO] ";
    }
}

static void Log_Task_Core(void *pvParameters)
{
    log_msg_t msg;
    (void)pvParameters;

    while (1) {
        if (xQueueReceive(s_log_queue, &msg, portMAX_DELAY) == pdPASS) {
            bsp_uart_send_buffer(BSP_UART_DEBUG, (const uint8_t *)msg.text, msg.len);
        }
    }
}

void System_Log_Init(void)
{
    if (s_log_mutex == NULL) {
        s_log_mutex = xSemaphoreCreateMutex();
    }

    if (s_log_queue == NULL) {
        s_log_queue = xQueueCreate(LOG_QUEUE_DEPTH, sizeof(log_msg_t));
    }

    if ((s_log_queue != NULL) && (s_log_task == NULL)) {
        xTaskCreate(Log_Task_Core,
                    "LogTx",
                    LOG_TASK_STACK,
                    NULL,
                    LOG_TASK_PRIO,
                    &s_log_task);
    }
}

void Log_Print(log_level_t level, const char *fmt, ...)
{
    int prefix_len;
    int body_len;
    int total_len;
    va_list ap;
    BaseType_t mutex_taken = pdFALSE;

    if (fmt == NULL) {
        return;
    }

    if (s_log_mutex != NULL) {
        if (xSemaphoreTake(s_log_mutex, 0) != pdTRUE) {
            return;
        }
        mutex_taken = pdTRUE;
    }

    prefix_len = snprintf(s_log_work_buf, sizeof(s_log_work_buf), "%s", Log_Level_To_Prefix(level));
    if (prefix_len < 0 || prefix_len >= (int)sizeof(s_log_work_buf)) {
        goto exit_unlock;
    }

    va_start(ap, fmt);
    body_len = vsnprintf(&s_log_work_buf[prefix_len],
                         sizeof(s_log_work_buf) - (size_t)prefix_len,
                         fmt,
                         ap);
    va_end(ap);

    if (body_len < 0) {
        goto exit_unlock;
    }

    total_len = prefix_len + body_len;
    if (total_len > (int)sizeof(s_log_work_buf) - 3) {
        total_len = (int)sizeof(s_log_work_buf) - 3;
    }

    s_log_work_buf[total_len++] = '\r';
    s_log_work_buf[total_len++] = '\n';
    s_log_work_buf[total_len] = '\0';

    if (s_log_queue != NULL) {
        if (total_len > (int)(LOG_TMP_BUF_SIZE - 1u)) {
            total_len = (int)(LOG_TMP_BUF_SIZE - 1u);
        }

        s_log_msg.len = (uint8_t)total_len;
        memcpy(s_log_msg.text, s_log_work_buf, (size_t)total_len + 1u);

        if (xQueueSend(s_log_queue, &s_log_msg, 0) != pdPASS) {
            // Queue full: drop the log silently.
        }
    }

exit_unlock:
    if ((s_log_mutex != NULL) && (mutex_taken == pdTRUE)) {
        xSemaphoreGive(s_log_mutex);
    }
}
