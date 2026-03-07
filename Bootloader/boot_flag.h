#ifndef __BOOT_FLAG_H
#define __BOOT_FLAG_H

#include "stm32f4xx.h"
#include "stm32f4xx_flash.h"

#define APP_FLAG_ADDR      ((uint32_t)0x08004000)
#define APP_FLAG_SIZE      ((uint32_t)0x4000)
#define APP_FLAG_SECTOR    FLASH_Sector_1
#define APP_VALID_FLAG     ((uint32_t)0x5A5A5A5A)

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

void BootFlag_Read(BootFlag_t *out);
int32_t BootFlag_Save(BootFlag_t *flag);
void BootFlag_MarkBootSuccess(void);
void BootFlag_RequestEnterBootloader(void);
uint8_t BootFlag_CheckAndClearEnterBootloader(void);

#endif

