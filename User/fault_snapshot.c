#include "fault_snapshot.h"

#include <stddef.h>
#include <string.h>

#include "bsp_flash.h"
#include "sys_flash_layout.h"

#define FAULT_SNAPSHOT_MAGIC         ((uint32_t)0x46534F56u)  /* "FSOV" */
#define FAULT_SNAPSHOT_VERSION       ((uint16_t)1u)
#define FAULT_SNAPSHOT_FLASH_ADDR    SYS_FLASH_FAULT_SNAPSHOT_ADDR
#define FAULT_SNAPSHOT_FLASH_SIZE    SYS_FLASH_FAULT_SNAPSHOT_SIZE
#define FAULT_SNAPSHOT_NAME_SIZE     ((uint16_t)32u)

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t task_name_len;
    char task_name[FAULT_SNAPSHOT_NAME_SIZE];
    uint32_t checksum;
} fault_snapshot_t;

static uint16_t prv_strnlen(const char *text, uint16_t max_len)
{
    uint16_t len = 0u;

    if (text == NULL) {
        return 0u;
    }

    while ((len < max_len) && (text[len] != '\0')) {
        len++;
    }

    return len;
}

static uint32_t prv_checksum32(const uint8_t *data, uint32_t len)
{
    uint32_t sum = 0x13572468u;
    uint32_t i;

    for (i = 0; i < len; i++) {
        sum ^= data[i];
        sum = (sum << 5) | (sum >> 27);
        sum += 0x9E3779B9u;
    }

    return sum;
}

bool System_FaultSnapshot_SaveStackOverflowTask(const char *task_name)
{
    fault_snapshot_t snapshot;
    uint16_t task_name_len;

    memset(&snapshot, 0, sizeof(snapshot));
    task_name_len = prv_strnlen(task_name, (uint16_t)(FAULT_SNAPSHOT_NAME_SIZE - 1u));

    snapshot.magic = FAULT_SNAPSHOT_MAGIC;
    snapshot.version = FAULT_SNAPSHOT_VERSION;
    snapshot.task_name_len = task_name_len;
    memcpy(snapshot.task_name, task_name, task_name_len);
    snapshot.task_name[task_name_len] = '\0';
    snapshot.checksum = prv_checksum32((const uint8_t *)&snapshot, (uint32_t)offsetof(fault_snapshot_t, checksum));

    if (!bsp_flash_erase(FAULT_SNAPSHOT_FLASH_ADDR, FAULT_SNAPSHOT_FLASH_SIZE)) {
        return false;
    }

    if (!bsp_flash_write(FAULT_SNAPSHOT_FLASH_ADDR, (const uint8_t *)&snapshot, sizeof(snapshot))) {
        return false;
    }

    return true;
}

bool System_FaultSnapshot_LoadLastStackOverflowTask(char *task_name, uint32_t task_name_size)
{
    fault_snapshot_t snapshot;
    uint32_t checksum;
    uint32_t copy_len;

    if ((task_name == NULL) || (task_name_size == 0u)) {
        return false;
    }

    memset(&snapshot, 0, sizeof(snapshot));
    bsp_flash_read(FAULT_SNAPSHOT_FLASH_ADDR, (uint8_t *)&snapshot, sizeof(snapshot));

    if ((snapshot.magic != FAULT_SNAPSHOT_MAGIC) || (snapshot.version != FAULT_SNAPSHOT_VERSION)) {
        return false;
    }

    if (snapshot.task_name_len >= FAULT_SNAPSHOT_NAME_SIZE) {
        return false;
    }

    checksum = prv_checksum32((const uint8_t *)&snapshot, (uint32_t)offsetof(fault_snapshot_t, checksum));
    if (checksum != snapshot.checksum) {
        return false;
    }

    copy_len = snapshot.task_name_len;
    if (copy_len >= task_name_size) {
        copy_len = task_name_size - 1u;
    }

    memcpy(task_name, snapshot.task_name, copy_len);
    task_name[copy_len] = '\0';
    return true;
}
