#ifndef __FLASH_IF_H
#define __FLASH_IF_H

#include "stm32f4xx.h"
#include "flash_layout.h"

/* ============ 函数声明 ============ */

/**
 * @brief  初始化Flash（解锁）
 */
void FLASH_If_Init(void);

/**
 * @brief  反初始化Flash（上锁）
 */
void FLASH_If_DeInit(void);

/**
 * @brief  擦除指定地址范围内的所有扇区
 * @param  start_addr: 起始地址
 * @param  size:       需要擦除的大小 (字节)
 * @retval FLASH_COMPLETE: 成功, 其他: 失败
 */
uint32_t FLASH_If_Erase(uint32_t start_addr, uint32_t size);

/**
 * @brief  擦除单个扇区（通过地址）
 * @param  addr: 扇区内任意地址
 * @retval FLASH_COMPLETE: 成功, 其他: 失败
 */
uint32_t FLASH_If_EraseSector(uint32_t addr);

/**
 * @brief  写入数据到Flash（按字写入，32bit）
 * @param  addr: 目标地址（必须4字节对齐）
 * @param  data: 数据指针
 * @param  len:  数据长度（字节数，会自动对齐到4字节）
 * @retval FLASH_COMPLETE: 成功, 其他: 失败
 */
uint32_t FLASH_If_Write(uint32_t addr, uint32_t *data, uint32_t len);

/**
 * @brief  从Flash读取数据
 * @param  addr: 源地址
 * @param  data: 目标缓冲区
 * @param  len:  读取长度（字节数）
 * @retval 实际读取的字节数
 */
uint32_t FLASH_If_Read(uint32_t addr, uint32_t *data, uint32_t len);



#endif /* __FLASH_IF_H */



