#include "sys_log.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#include "bsp_flash.h"
#include "bsp_uart.h"
#include "sys_flash_layout.h"

#define LOG_TMP_BUF_SIZE                 192u
#define LOG_QUEUE_DEPTH                   16u
#define LOG_TASK_STACK                   512u
#define LOG_TASK_PRIO                      3u
#define LOG_PERSIST_REPLAY_DEFAULT_MAX    64u

#define PLOG_MAGIC                        ((uint32_t)0x504C4F47u) /* "PLOG" */
#define PLOG_VERSION                      ((uint16_t)1u)
#define PLOG_MAX_MSG_LEN                  ((uint16_t)(LOG_TMP_BUF_SIZE - 1u))

typedef struct
{
    uint8_t len;
    uint8_t level;
    char text[LOG_TMP_BUF_SIZE];
} log_msg_t;

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint8_t level;
    uint8_t len;
    uint32_t seq;
    uint32_t tick;
    uint32_t checksum;
} persist_log_record_hdr_t;

typedef struct
{
    uint32_t base_addr;
    uint32_t write_offset;
    uint32_t last_seq;
    uint32_t valid_count;
    uint8_t has_data;
} persist_bank_scan_t;

static SemaphoreHandle_t s_log_mutex = NULL;
static QueueHandle_t s_log_queue = NULL;
static TaskHandle_t s_log_task = NULL;

static char s_log_work_buf[LOG_TMP_BUF_SIZE];
static log_msg_t s_log_msg;

static uint32_t s_plog_active_base = SYS_FLASH_PERSIST_LOG_BANK0_ADDR;
static uint32_t s_plog_write_offset = 0u;
static uint32_t s_plog_next_seq = 1u;
static uint8_t s_plog_ready = 0u;
static uint8_t s_plog_replayed = 0u;

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

static uint32_t Persist_Log_Checksum32(const uint8_t *data, uint32_t len)
{
    uint32_t sum = 0x13572468u;
    uint32_t i;

    for (i = 0u; i < len; i++) {
        sum ^= data[i];
        sum = (sum << 5) | (sum >> 27);
        sum += 0x9E3779B9u;
    }

    return sum;
}

static uint8_t Persist_Log_IsWarningOrError(log_level_t level)
{
    return (uint8_t)((level == LOG_LEVEL_WARNING) || (level == LOG_LEVEL_ERROR));
}

static uint32_t Persist_Log_GetOtherBankBase(uint32_t base_addr)
{
    return (base_addr == SYS_FLASH_PERSIST_LOG_BANK0_ADDR) ?
           SYS_FLASH_PERSIST_LOG_BANK1_ADDR :
           SYS_FLASH_PERSIST_LOG_BANK0_ADDR;
}

static uint8_t Persist_Log_RecordChecksumOk(uint32_t rec_addr, const persist_log_record_hdr_t *hdr)
{
    uint8_t checksum_buf[(sizeof(persist_log_record_hdr_t) - sizeof(uint32_t)) + PLOG_MAX_MSG_LEN];
    uint32_t checksum_len = (uint32_t)(sizeof(persist_log_record_hdr_t) - sizeof(uint32_t));
    uint32_t calc;

    if ((hdr == NULL) || (hdr->len == 0u) || (hdr->len > PLOG_MAX_MSG_LEN)) {
        return 0u;
    }

    memcpy(checksum_buf, hdr, checksum_len);
    bsp_flash_read(rec_addr + (uint32_t)sizeof(persist_log_record_hdr_t),
                   &checksum_buf[checksum_len],
                   hdr->len);

    calc = Persist_Log_Checksum32(checksum_buf, checksum_len + hdr->len);
    return (uint8_t)(calc == hdr->checksum);
}

static void Persist_Log_ScanBank(uint32_t base_addr, persist_bank_scan_t *scan)
{
    uint32_t offset = 0u;

    if (scan == NULL) {
        return;
    }

    memset(scan, 0, sizeof(*scan));
    scan->base_addr = base_addr;

    while ((offset + (uint32_t)sizeof(persist_log_record_hdr_t)) <= SYS_FLASH_PERSIST_LOG_BANK_SIZE) {
        uint32_t rec_addr = base_addr + offset;
        uint32_t magic = *(volatile const uint32_t *)rec_addr;
        persist_log_record_hdr_t hdr;
        uint32_t rec_size;

        if (magic == 0xFFFFFFFFu) {
            break;
        }

        bsp_flash_read(rec_addr, (uint8_t *)&hdr, sizeof(hdr));
        if ((hdr.magic != PLOG_MAGIC) || (hdr.version != PLOG_VERSION)) {
            break;
        }

        if ((hdr.len == 0u) || (hdr.len > PLOG_MAX_MSG_LEN)) {
            break;
        }

        rec_size = (uint32_t)sizeof(persist_log_record_hdr_t) + (uint32_t)hdr.len;
        if ((offset + rec_size) > SYS_FLASH_PERSIST_LOG_BANK_SIZE) {
            break;
        }

        if (Persist_Log_RecordChecksumOk(rec_addr, &hdr) == 0u) {
            break;
        }

        scan->has_data = 1u;
        scan->last_seq = hdr.seq;
        scan->valid_count++;
        offset += rec_size;
    }

    scan->write_offset = offset;
}

static void Persist_Log_InitState(void)
{
    persist_bank_scan_t bank0;
    persist_bank_scan_t bank1;
    const persist_bank_scan_t *active_scan = NULL;

    Persist_Log_ScanBank(SYS_FLASH_PERSIST_LOG_BANK0_ADDR, &bank0);
    Persist_Log_ScanBank(SYS_FLASH_PERSIST_LOG_BANK1_ADDR, &bank1);

    if ((bank0.has_data == 0u) && (bank1.has_data == 0u)) {
        s_plog_active_base = SYS_FLASH_PERSIST_LOG_BANK0_ADDR;
        s_plog_write_offset = 0u;
        s_plog_next_seq = 1u;
        s_plog_ready = 1u;
        return;
    }

    if ((bank0.has_data != 0u) && (bank1.has_data != 0u)) {
        active_scan = (bank0.last_seq >= bank1.last_seq) ? &bank0 : &bank1;
    } else {
        active_scan = (bank0.has_data != 0u) ? &bank0 : &bank1;
    }

    s_plog_active_base = active_scan->base_addr;
    s_plog_write_offset = active_scan->write_offset;
    s_plog_next_seq = active_scan->last_seq + 1u;
    s_plog_ready = 1u;
}

static uint8_t Persist_Log_SwitchBank(void)
{
    uint32_t other_base = Persist_Log_GetOtherBankBase(s_plog_active_base);

    if (!bsp_flash_erase(other_base, SYS_FLASH_PERSIST_LOG_BANK_SIZE)) {
        return 0u;
    }

    s_plog_active_base = other_base;
    s_plog_write_offset = 0u;
    return 1u;
}

static uint8_t Persist_Log_Append(log_level_t level, const char *text, uint16_t len)
{
    persist_log_record_hdr_t hdr;
    uint8_t checksum_buf[(sizeof(persist_log_record_hdr_t) - sizeof(uint32_t)) + PLOG_MAX_MSG_LEN];
    uint32_t checksum_len = (uint32_t)(sizeof(persist_log_record_hdr_t) - sizeof(uint32_t));
    uint32_t rec_size;

    if ((text == NULL) || (len == 0u)) {
        return 0u;
    }

    if (Persist_Log_IsWarningOrError(level) == 0u) {
        return 1u;
    }

    if (s_plog_ready == 0u) {
        Persist_Log_InitState();
    }

    if (len > PLOG_MAX_MSG_LEN) {
        len = PLOG_MAX_MSG_LEN;
    }

    hdr.magic = PLOG_MAGIC;
    hdr.version = PLOG_VERSION;
    hdr.level = (uint8_t)level;
    hdr.len = (uint8_t)len;
    hdr.seq = s_plog_next_seq;
    hdr.tick = (uint32_t)xTaskGetTickCount();
    hdr.checksum = 0u;

    memcpy(checksum_buf, &hdr, checksum_len);
    memcpy(&checksum_buf[checksum_len], text, len);
    hdr.checksum = Persist_Log_Checksum32(checksum_buf, checksum_len + len);

    rec_size = (uint32_t)sizeof(persist_log_record_hdr_t) + (uint32_t)len;
    if ((s_plog_write_offset + rec_size) > SYS_FLASH_PERSIST_LOG_BANK_SIZE) {
        if (Persist_Log_SwitchBank() == 0u) {
            return 0u;
        }
    }

    if ((!bsp_flash_write(s_plog_active_base + s_plog_write_offset,
                          (const uint8_t *)&hdr,
                          (uint32_t)sizeof(hdr))) ||
        (!bsp_flash_write(s_plog_active_base + s_plog_write_offset + (uint32_t)sizeof(hdr),
                          (const uint8_t *)text,
                          (uint32_t)len))) {
        /* Current bank may contain interrupted/partially-written record, retry on a fresh bank. */
        if (Persist_Log_SwitchBank() == 0u) {
            return 0u;
        }

        if (!bsp_flash_write(s_plog_active_base + s_plog_write_offset,
                             (const uint8_t *)&hdr,
                             (uint32_t)sizeof(hdr))) {
            return 0u;
        }

        if (!bsp_flash_write(s_plog_active_base + s_plog_write_offset + (uint32_t)sizeof(hdr),
                             (const uint8_t *)text,
                             (uint32_t)len)) {
            return 0u;
        }
    }

    s_plog_write_offset += rec_size;
    s_plog_next_seq++;
    return 1u;
}

static void Persist_Log_ReplayOneBank(uint32_t base_addr, uint16_t *remain)
{
    uint32_t offset = 0u;

    if ((remain == NULL) || (*remain == 0u)) {
        return;
    }

    while ((offset + (uint32_t)sizeof(persist_log_record_hdr_t)) <= SYS_FLASH_PERSIST_LOG_BANK_SIZE) {
        uint32_t rec_addr = base_addr + offset;
        uint32_t magic = *(volatile const uint32_t *)rec_addr;
        persist_log_record_hdr_t hdr;
        uint32_t rec_size;
        uint8_t msg_buf[PLOG_MAX_MSG_LEN + 1u];

        if (magic == 0xFFFFFFFFu) {
            break;
        }

        bsp_flash_read(rec_addr, (uint8_t *)&hdr, sizeof(hdr));
        if ((hdr.magic != PLOG_MAGIC) || (hdr.version != PLOG_VERSION)) {
            break;
        }

        if ((hdr.len == 0u) || (hdr.len > PLOG_MAX_MSG_LEN)) {
            break;
        }

        rec_size = (uint32_t)sizeof(persist_log_record_hdr_t) + (uint32_t)hdr.len;
        if ((offset + rec_size) > SYS_FLASH_PERSIST_LOG_BANK_SIZE) {
            break;
        }

        if (Persist_Log_RecordChecksumOk(rec_addr, &hdr) == 0u) {
            break;
        }

        bsp_flash_read(rec_addr + (uint32_t)sizeof(persist_log_record_hdr_t), msg_buf, hdr.len);
        msg_buf[hdr.len] = '\0';
        bsp_uart_send_buffer(BSP_UART_DEBUG, msg_buf, hdr.len);

        offset += rec_size;
        (*remain)--;
        if (*remain == 0u) {
            break;
        }
    }
}

void System_Log_PersistReplay(uint16_t max_count)
{
    persist_bank_scan_t bank0;
    persist_bank_scan_t bank1;
    uint16_t remain;

    if (max_count == 0u) {
        return;
    }

    Persist_Log_ScanBank(SYS_FLASH_PERSIST_LOG_BANK0_ADDR, &bank0);
    Persist_Log_ScanBank(SYS_FLASH_PERSIST_LOG_BANK1_ADDR, &bank1);

    remain = max_count;
    if ((bank0.has_data != 0u) && (bank1.has_data != 0u)) {
        if (bank0.last_seq <= bank1.last_seq) {
            Persist_Log_ReplayOneBank(bank0.base_addr, &remain);
            Persist_Log_ReplayOneBank(bank1.base_addr, &remain);
        } else {
            Persist_Log_ReplayOneBank(bank1.base_addr, &remain);
            Persist_Log_ReplayOneBank(bank0.base_addr, &remain);
        }
    } else if (bank0.has_data != 0u) {
        Persist_Log_ReplayOneBank(bank0.base_addr, &remain);
    } else if (bank1.has_data != 0u) {
        Persist_Log_ReplayOneBank(bank1.base_addr, &remain);
    }
}

bool System_Log_PersistClear(void)
{
    if (!bsp_flash_erase(SYS_FLASH_PERSIST_LOG_BANK0_ADDR, SYS_FLASH_PERSIST_LOG_BANK_SIZE)) {
        return false;
    }

    if (!bsp_flash_erase(SYS_FLASH_PERSIST_LOG_BANK1_ADDR, SYS_FLASH_PERSIST_LOG_BANK_SIZE)) {
        return false;
    }

    s_plog_active_base = SYS_FLASH_PERSIST_LOG_BANK0_ADDR;
    s_plog_write_offset = 0u;
    s_plog_next_seq = 1u;
    s_plog_ready = 1u;
    return true;
}

static void Log_Task_Core(void *pvParameters)
{
    log_msg_t msg;
    (void)pvParameters;

    if (s_plog_replayed == 0u) {
        System_Log_PersistReplay(LOG_PERSIST_REPLAY_DEFAULT_MAX);
        s_plog_replayed = 1u;
    }

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

    Persist_Log_InitState();
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

    (void)Persist_Log_Append(level, s_log_work_buf, (uint16_t)total_len);

    if (s_log_queue != NULL) {
        if (total_len > (int)(LOG_TMP_BUF_SIZE - 1u)) {
            total_len = (int)(LOG_TMP_BUF_SIZE - 1u);
        }

        s_log_msg.len = (uint8_t)total_len;
        s_log_msg.level = (uint8_t)level;
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
