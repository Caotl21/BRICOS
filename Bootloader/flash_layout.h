#ifndef __FLASH_LAYOUT_H
#define __FLASH_LAYOUT_H

#include "stm32f4xx.h"
#include "stm32f4xx_flash.h"

/* STM32F407 Flash 扇区信息 */
typedef struct {
    uint32_t sector_id;     /* FLASH_Sector_x 常量值 */
    uint32_t start_addr;    /* 扇区起始地址 */
    uint32_t size;          /* 扇区大小 (字节) */
} FlashSectorInfo_t;

/* 扇区信息表 */
static const FlashSectorInfo_t flash_sectors[] = {
    { FLASH_Sector_0,  0x08000000,  16 * 1024 },   /* 16KB  */
    { FLASH_Sector_1,  0x08004000,  16 * 1024 },   /* 16KB  */
    { FLASH_Sector_2,  0x08008000,  16 * 1024 },   /* 16KB  */
    { FLASH_Sector_3,  0x0800C000,  16 * 1024 },   /* 16KB  */
    { FLASH_Sector_4,  0x08010000,  64 * 1024 },   /* 64KB  */
    { FLASH_Sector_5,  0x08020000, 128 * 1024 },   /* 128KB */
    { FLASH_Sector_6,  0x08040000, 128 * 1024 },   /* 128KB */
    { FLASH_Sector_7,  0x08060000, 128 * 1024 },   /* 128KB */
    { FLASH_Sector_8,  0x08080000, 128 * 1024 },   /* 128KB */
    { FLASH_Sector_9,  0x080A0000, 128 * 1024 },   /* 128KB */
    { FLASH_Sector_10, 0x080C0000, 128 * 1024 },   /* 128KB */
    { FLASH_Sector_11, 0x080E0000, 128 * 1024 },   /* 128KB */
};

#define FLASH_SECTOR_COUNT  (sizeof(flash_sectors) / sizeof(flash_sectors[0]))

/**
 * @brief  根据 Flash 地址获取扇区编号
 * @param  addr: Flash 地址
 * @retval 扇区编号 (FLASH_Sector_x)，失败返回 0xFFFFFFFF
 */
static inline uint32_t Flash_GetSectorFromAddr(uint32_t addr)
{
    uint32_t i;
    for (i = 0; i < FLASH_SECTOR_COUNT; i++)
    {
        if (addr >= flash_sectors[i].start_addr &&
            addr < flash_sectors[i].start_addr + flash_sectors[i].size)
        {
            return flash_sectors[i].sector_id;
        }
    }
    return 0xFFFFFFFF;  /* 无效地址 */
}

/**
 * @brief  根据 Flash 地址获取扇区大小
 * @param  addr: Flash 地址
 * @retval 扇区大小 (字节)，失败返回 0
 */
static inline uint32_t Flash_GetSectorSize(uint32_t addr)
{
    uint32_t i;
    for (i = 0; i < FLASH_SECTOR_COUNT; i++)
    {
        if (addr >= flash_sectors[i].start_addr &&
            addr < flash_sectors[i].start_addr + flash_sectors[i].size)
        {
            return flash_sectors[i].size;
        }
    }
    return 0;
}

/**
 * @brief  获取下一个扇区编号
 */
static inline uint32_t Flash_GetNextSector(uint32_t sector)
{
    /* STM32F4 库中 FLASH_Sector_x = x * 8 */
    if (sector < FLASH_Sector_11)
        return sector + 8;
    return 0xFFFFFFFF;
}

#endif /* __FLASH_LAYOUT_H */

