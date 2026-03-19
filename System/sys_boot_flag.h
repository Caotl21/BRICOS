#ifndef SYS_BOOT_FLAG_H
#define SYS_BOOT_FLAG_H

#include <stdint.h>
#include <stdbool.h>

#define BOOT_FLAG_ADDR      ((uint32_t)0x08004000)
#define BOOT_FLAG_SIZE      ((uint32_t)0x4000)
#define BOOT_VALID_FLAG     ((uint32_t)0x5A5A5A5A)

typedef struct
{
    uint32_t valid_flag;
    uint8_t  boot_attempts;
    uint8_t  need_copy;
    uint8_t  ota_complete;
    uint8_t  enter_bootloader;
    uint32_t app1_version;
    uint32_t app2_version;
    uint32_t app1_crc;
    uint32_t app2_crc;
    uint32_t boot_count;
    uint32_t reserved2;
} BootFlag_t;

bool Sys_BootFlag_Read(BootFlag_t *out);
bool Sys_BootFlag_Save(const BootFlag_t *flag);
bool Sys_BootFlag_MarkBootSuccess(void);
bool Sys_BootFlag_RequestEnterBootloader(void);
bool Sys_BootFlag_CheckAndClearEnterBootloader(bool *need_enter);

#endif