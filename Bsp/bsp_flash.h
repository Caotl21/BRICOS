#ifndef __BSP_FLASH_H
#define __BSP_FLASH_H

#include <stdint.h>
#include <stdbool.h>

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

/* =======================================================
 * API：擦除 Flash 区域
 * 参数：start_addr - 起始物理地址 (如 0x08040000)
 * size       - 需要擦除的字节大小
 * 返回：true-成功，false-失败
 * ======================================================= */
bool bsp_flash_erase(uint32_t start_addr, uint32_t size);

/* =======================================================
 * API：写入 Flash 数据
 * 参数：addr - 写入目标地址
 * data - 数据源指针
 * len  - 写入长度 (字节)
 * 返回：true-成功，false-失败
 * ======================================================= */
bool bsp_flash_write(uint32_t addr, const uint8_t *data, uint32_t len);

/* =======================================================
 * API：读取 Flash 数据
 * 参数：addr - 读取目标地址
 * data - 存放数据的缓冲区指针
 * len  - 读取长度 (字节)
 * ======================================================= */
void bsp_flash_read(uint32_t addr, uint8_t *data, uint32_t len);

#endif // __BSP_FLASH_H