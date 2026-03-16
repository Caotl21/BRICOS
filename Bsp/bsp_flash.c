#include "bsp_flash.h"
#include "stm32f4xx.h"
#include "stm32f4xx_flash.h"

/************************************************************
 * 统一的 Flash 驱动实现文件
 * 提供了擦除、写入和读取 Flash 的高层 API
 * 适配了配置数据存储和固件升级的需求
 ************************************************************/

 static uint32_t bsp_get_flash_sector(uint32_t Address)
{
    if      (Address < ADDR_FLASH_SECTOR_1)  return FLASH_Sector_0;
    else if (Address < ADDR_FLASH_SECTOR_2)  return FLASH_Sector_1;
    else if (Address < ADDR_FLASH_SECTOR_3)  return FLASH_Sector_2;
    else if (Address < ADDR_FLASH_SECTOR_4)  return FLASH_Sector_3;
    else if (Address < ADDR_FLASH_SECTOR_5)  return FLASH_Sector_4;
    else if (Address < ADDR_FLASH_SECTOR_6)  return FLASH_Sector_5;
    else if (Address < ADDR_FLASH_SECTOR_7)  return FLASH_Sector_6;
    else if (Address < ADDR_FLASH_SECTOR_8)  return FLASH_Sector_7;
    else if (Address < ADDR_FLASH_SECTOR_9)  return FLASH_Sector_8;
    else if (Address < ADDR_FLASH_SECTOR_10) return FLASH_Sector_9;
    else if (Address < ADDR_FLASH_SECTOR_11) return FLASH_Sector_10;
    else return FLASH_Sector_11;
}

/* -------------------------------------------------------------------------
 * 函数名：bsp_flash_erase
 * 功  能：擦除 Flash 区域
 * 参  数：start_addr - 起始物理地址 (如 0x08040000)
 * size - 需要擦除的字节大小
 * 返回：true-成功，false-失败
 * ------------------------------------------------------------------------- */
bool bsp_flash_erase(uint32_t start_addr, uint32_t size)
{
    uint32_t end_addr = start_addr + size;
    uint32_t sector_start = bsp_get_flash_sector(start_addr);
    uint32_t sector_end = bsp_get_flash_sector(end_addr - 1); // -1 确保包含最后一个地址

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR | 
                    FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    for(uint32_t sector=sector_start;sector<=sector_end;sector+=8){
        // STM32F4 擦除时电压范围设置 (通常是 VoltageRange_3 即 2.7V-3.6V)
        if (FLASH_EraseSector(sector, VoltageRange_3) != FLASH_COMPLETE) {
            FLASH_Lock();
            return false;
        }
    }

    FLASH_Lock();
    return true;
}

/* -------------------------------------------------------------------------
 * 函数名：bsp_flash_write
 * 功  能：写入 Flash 数据
 * 参  数：addr - 写入目标地址
 * data - 数据源指针
 * len  - 写入长度 (字节)
 * 返回：true-成功，false-失败
 * ------------------------------------------------------------------------- */
bool bsp_flash_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR | 
                    FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
    
    // STM32F4 支持按字节、半字 (16-bit) 或字 (32-bit) 写入
    // 这里我们按字节 (8-bit) 写入，确保地址和长度都是
    for (uint32_t i = 0; i < len; i++) {
        if (FLASH_ProgramByte(addr + i, data[i]) != FLASH_COMPLETE) {
            FLASH_Lock();
            return false;
        }
    }

    FLASH_Lock();
    return true;    
}

/* -------------------------------------------------------------------------
 * 函数名：bsp_flash_read
 * 功  能：读取 Flash 数据
 * 参  数：addr - 读取目标地址
 * data - 存放数据的缓冲区指针
 * len  - 读取长度 (字节)
 * ------------------------------------------------------------------------- */
void bsp_flash_read(uint32_t addr, uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        data[i] = *(volatile uint8_t*)(addr + i);
    }
}

