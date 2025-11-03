#ifndef __FLASH_IF_H
#define __FLASH_IF_H

#include "stm32f10x.h"

/* 函数声明 */
void FLASH_If_Init(void);
uint32_t FLASH_If_Erase(uint32_t start_addr, uint32_t end_addr);
uint32_t FLASH_If_ErasePage(uint32_t page_addr);
uint32_t FLASH_If_Write(uint32_t addr, uint32_t *data, uint32_t len);
uint32_t FLASH_If_Read(uint32_t addr, uint32_t *data, uint32_t len);

#endif /* __FLASH_IF_H */

