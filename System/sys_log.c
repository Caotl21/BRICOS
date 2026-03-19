#include "sys_log.h"
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "semphr.h"
#include "bsp_uart.h"

// 不做本地存储；仅临时格式化缓冲
#define LOG_TMP_BUF_SIZE 192

static SemaphoreHandle_t s_log_mutex = NULL;

static const char *Log_Level_To_Prefix(log_level_t level)
{
    switch (level) {
        case LOG_LEVEL_ERROR:   return "[ERROR] ";
        case LOG_LEVEL_WARNING: return "[WARNING] ";
        case LOG_LEVEL_INFO:    return "[INFO] ";
        default:                return "[INFO] ";
    }
}

void Log_Init(void)
{
    if (s_log_mutex == NULL) {
        s_log_mutex = xSemaphoreCreateMutex();
    }
}

void Log_Print(log_level_t level, const char *fmt, ...)
{
    char msg_buf[LOG_TMP_BUF_SIZE];
    int prefix_len;
    int body_len;
    int total_len;
    va_list ap;

    if (fmt == NULL) {
        return;
    }

    if (s_log_mutex != NULL) {
        if (xSemaphoreTake(s_log_mutex, pdMS_TO_TICKS(5)) != pdTRUE) {
            return;
        }
    }

    prefix_len = snprintf(msg_buf, sizeof(msg_buf), "%s", Log_Level_To_Prefix(level));
    if (prefix_len < 0 || prefix_len >= (int)sizeof(msg_buf)) {
        goto exit_unlock;
    }

    va_start(ap, fmt);
    body_len = vsnprintf(&msg_buf[prefix_len], sizeof(msg_buf) - (size_t)prefix_len, fmt, ap);
    va_end(ap);

    if (body_len < 0) {
        goto exit_unlock;
    }

    total_len = prefix_len + body_len;
    if (total_len > (int)sizeof(msg_buf) - 3) {
        total_len = (int)sizeof(msg_buf) - 3;
    }

    // 统一行尾，方便串口工具显示
    msg_buf[total_len++] = '\r';
    msg_buf[total_len++] = '\n';
    msg_buf[total_len] = '\0';

    // 走 UART4 (BSP_UART_OPI_NRT)
    bsp_uart_send_buffer(BSP_UART_OPI_NRT, (const uint8_t *)msg_buf, (uint16_t)total_len);

exit_unlock:
    if (s_log_mutex != NULL) {
        xSemaphoreGive(s_log_mutex);
    }
}