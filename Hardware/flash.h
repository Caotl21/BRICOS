#ifndef __BSP_FLASH_H
#define __BSP_FLASH_H

#include "stm32f4xx.h"

// 只是为了定义地址，方便查表
#define ADDR_FLASH_SECTOR_0     ((uint32_t)0x08000000) 	// 16KB
#define ADDR_FLASH_SECTOR_1     ((uint32_t)0x08004000) 	// 16KB
#define ADDR_FLASH_SECTOR_2     ((uint32_t)0x08008000) 	// 16KB
#define ADDR_FLASH_SECTOR_3     ((uint32_t)0x0800C000) 	// 16KB
#define ADDR_FLASH_SECTOR_4     ((uint32_t)0x08010000) 	// 64KB
#define ADDR_FLASH_SECTOR_5     ((uint32_t)0x08020000) 	// 128KB
#define ADDR_FLASH_SECTOR_6     ((uint32_t)0x08040000) 	// 128KB
#define ADDR_FLASH_SECTOR_7     ((uint32_t)0x08060000) 	// 128KB
#define ADDR_FLASH_SECTOR_8     ((uint32_t)0x08080000) 	// 128KB
#define ADDR_FLASH_SECTOR_9     ((uint32_t)0x080A0000) 	// 128KB
#define ADDR_FLASH_SECTOR_10    ((uint32_t)0x080C0000) 	// 128KB
#define ADDR_FLASH_SECTOR_11    ((uint32_t)0x080E0000) 	// 128KB

#define SECTOR_ADDR_START  0x080E0000  // Sector 11 起始
#define SECTOR_ADDR_END    0x08100000  // Sector 11 结束 (也是 Sector 12 起始)
#define EMPTY_VAL          0xFFFFFFFF  // Flash 擦除后的默认值

#define VALID_STATUS       0x00000000  // Flash写入有效值标志位
#define INVALID_STATUS     0xFFFFFFFF  // Flash写入有效值标志位

// 写入数据结构体
typedef struct {
    uint32_t data;
    uint32_t status; // 0x00000000 代表有效，0xFFFFFFFF 代表空
} Flash_Unit_t;

// 常用读取和写入函数
uint32_t STMFLASH_ReadWord(uint32_t faddr);
void STMFLASH_Write(uint32_t WriteAddr, uint32_t *pBuffer, uint32_t NumToWrite);
uint32_t Config_Read(void);
void Config_AppendWrite(uint32_t val);
void STMFLASH_EraseSector(uint32_t WriteAddr);

#endif
