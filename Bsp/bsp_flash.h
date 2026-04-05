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

/**
 * @brief  擦除指定地址所在的 Flash 
 * @param  addr 目标地址
 * @param  size 目标地址范围大小 (字节)，用于判断是否跨扇区
 * @return 是否成功擦除
 * @note   1. STM32F4 的 Flash 擦除是以扇区为单位的，且不同扇区大小不同。
 *         2. 擦除前必须先解锁 Flash，擦除后要锁定 Flash。
 *         3. 擦除时要清除相关的状态标志，防止之前的错误状态影响当前操作。
 *         4. 该函数会自动判断是否跨扇区，并擦除所有涉及的扇区。
 *         5. 该函数不检查地址合法性，调用前请确保地址在 Flash 范围内。
 */
bool bsp_flash_erase(uint32_t start_addr, uint32_t size);

/**
 * @brief 写入 Flash 数据
 * @param addr 目标地址
 * @param data 存放数据的缓冲区指针
 * @param len 写入长度 (字节)
 * @return 是否成功写入
 * @note 该函数直接向 Flash 地址写入数据，适用于任何地址范围内的写入操作。调用前请确保地址合法且缓冲区足够大。
 */
bool bsp_flash_write(uint32_t addr, const uint8_t *data, uint32_t len);

/**
 * @brief 读取 Flash 数据
 * @param addr 目标地址
 * @param data 存放数据的缓冲区指针
 * @param len 读取长度 (字节)
 * @return 无
 * @note 该函数直接从 Flash 地址读取数据，适用于任何地址范围内的读取操作。调用前请确保地址合法且缓冲区足够大。
 */
void bsp_flash_read(uint32_t addr, uint8_t *data, uint32_t len);

#endif // __BSP_FLASH_H

