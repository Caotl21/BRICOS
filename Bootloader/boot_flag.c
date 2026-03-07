#include "boot_flag.h"

/**
 * @brief  擦除 Boot Flag 扇区 (Sector 1)
 * @retval FLASH_COMPLETE: 成功
 */
static FLASH_Status Flag_EraseSector(void)
{
    FLASH_Status status;
    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                    FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
    status = FLASH_EraseSector(APP_FLAG_SECTOR, VoltageRange_3);
    FLASH_Lock();
    return status;
}

/**
 * @brief  写入 Boot Flag 结构体到 Flash
 * @param  flag: 要写入的标志结构体指针
 * @retval FLASH_COMPLETE: 成功
 */
static FLASH_Status Flag_Write(BootFlag_t *flag)
{
    FLASH_Status status = FLASH_COMPLETE;
    uint32_t *src = (uint32_t *)flag;
    uint32_t addr = APP_FLAG_ADDR;
    uint32_t words = sizeof(BootFlag_t) / 4;
    uint32_t i;

    /* BootFlag_t 大小需要4字节对齐 */
    if (sizeof(BootFlag_t) % 4 != 0)
        words++;

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                    FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    for (i = 0; i < words; i++)
    {
        status = FLASH_ProgramWord(addr + i * 4, src[i]);
        if (status != FLASH_COMPLETE)
            break;
    }

    FLASH_Lock();
    return status;
}

/**
 * @brief  读取当前 Boot Flag（安全读取，防止未初始化）
 * @param  out: 输出的标志结构体
 */
void BootFlag_Read(BootFlag_t *out)
{
    BootFlag_t *stored = (BootFlag_t *)APP_FLAG_ADDR;

    if (stored->valid_flag == APP_VALID_FLAG)
    {
        *out = *stored;
    }
    else
    {
        /* 未初始化，填充默认值 */
        out->valid_flag         = APP_VALID_FLAG;
        out->boot_attempts      = 0;
        out->need_copy          = 0;
        out->ota_complete       = 0;
        out->enter_bootloader   = 0;
        out->app1_version       = 0;
        out->app2_version       = 0;
        out->app1_crc           = 0;
        out->app2_crc           = 0;
        out->boot_count         = 0;
        out->reserved2          = 0;
    }
}

/**
 * @brief  保存 Boot Flag（擦除+写入）
 * @param  flag: 要保存的标志结构体
 * @retval 0:成功, -1:失败
 */
int32_t BootFlag_Save(BootFlag_t *flag)
{
    if (Flag_EraseSector() != FLASH_COMPLETE)
        return -1;
    if (Flag_Write(flag) != FLASH_COMPLETE)
        return -1;
    return 0;
}

/**
 * @brief  标记启动成功（由APP调用）
 */
void BootFlag_MarkBootSuccess(void)
{
    BootFlag_t boot_flag;
    BootFlag_t *stored = (BootFlag_t *)APP_FLAG_ADDR;

    if (stored->valid_flag == APP_VALID_FLAG)
    {
        boot_flag = *stored;
        boot_flag.boot_attempts = 0;
        BootFlag_Save(&boot_flag);
    }
}

/**
 * @brief  检查并清除进入Bootloader的请求标志
 * @retval 1:需要进入Bootloader, 0:不需要
 */
uint8_t BootFlag_CheckAndClearEnterBootloader(void)
{
    BootFlag_t boot_flag;

    BootFlag_Read(&boot_flag);
    if (boot_flag.enter_bootloader == 0)
    {
        return 0;
    }

    boot_flag.enter_bootloader = 0;
    BootFlag_Save(&boot_flag);
    return 1;
}

/**
 * @brief  请求进入Bootloader
 */
void BootFlag_RequestEnterBootloader(void)
{
    BootFlag_t boot_flag;
    
    BootFlag_Read(&boot_flag);
    boot_flag.enter_bootloader = 1;
    BootFlag_Save(&boot_flag);
}



