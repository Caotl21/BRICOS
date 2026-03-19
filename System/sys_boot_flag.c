#include "sys_boot_flag.h"
#include "bsp_flash.h"
#include  <string.h>

static void BootFlag_Fill_Default(BootFlag_t *f)
{
    memset(f, 0, sizeof(BootFlag_t));
    f->valid_flag = BOOT_VALID_FLAG;
}

bool Sys_BootFlag_Read(BootFlag_t *out)
{
    if (out == NULL) return false;

    bsp_flash_read(BOOT_FLAG_ADDR, (uint8_t *)out, sizeof(BootFlag_t));
    if (out->valid_flag != BOOT_VALID_FLAG) {
        BootFlag_Fill_Default(out);
    }
    return true;
}

bool Sys_BootFlag_Save(const BootFlag_t *flag)
{
    if (flag == NULL) return false;

    if (!bsp_flash_erase(BOOT_FLAG_ADDR, BOOT_FLAG_SIZE)) return false;
    if (!bsp_flash_write(BOOT_FLAG_ADDR, (const uint8_t *)flag, sizeof(BootFlag_t))) return false;
    return true;
}

/**
 * @brief  标记启动成功（由APP调用）
 */
bool Sys_BootFlag_MarkBootSuccess(void)
{
    BootFlag_t flag;
    if (!Sys_BootFlag_Read(&flag)) return false;

    flag.boot_attempts = 0; // 重置尝试计数
    return Sys_BootFlag_Save(&flag);
}

/**
 * @brief  请求进入Bootloader
 */
bool Sys_BootFlag_RequestEnterBootloader(void)
{
    BootFlag_t flag;
    if (!Sys_BootFlag_Read(&flag)) return false;

    flag.enter_bootloader = 1; // 设置进入引导加载程序的标志
    return Sys_BootFlag_Save(&flag);
}

/**
 * @brief  检查并清除进入Bootloader的请求标志
 * @retval 1:需要进入Bootloader, 0:不需要
 */
bool Sys_BootFlag_CheckAndClearEnterBootloader(void)
{
    BootFlag_t flag;
    if (!Sys_BootFlag_Read(&flag)) return false;

    if (flag.enter_bootloader == 0) {
        return false;
    }

    flag.enter_bootloader = 0; // 清除标志
    return Sys_BootFlag_Save(&flag);
    
    return true;
}