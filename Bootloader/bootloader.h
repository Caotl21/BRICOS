#ifndef __BOOTLOADER_H
#define __BOOTLOADER_H

#include "stm32f4xx.h"

/* ============ Flash 分区定义 (STM32F407ZGT6) ============ */

/*
 * Flash Layout:
 *   Sector 0  (16KB)  0x08000000 - 0x08003FFF  Bootloader
 *   Sector 1  (16KB)  0x08004000 - 0x08007FFF  Boot Flag (专用)
 *   Sector 2  (16KB)  0x08008000 - 0x0800BFFF  APP1 (Run)
 *   Sector 3  (16KB)  0x0800C000 - 0x0800FFFF  APP1
 *   Sector 4  (64KB)  0x08010000 - 0x0801FFFF  APP1
 *   Sector 5  (128KB) 0x08020000 - 0x0803FFFF  APP2 (Cache)
 *   Sector 6-11       0x08040000 - 0x080FFFFF  Reserved
 */

/* Bootloader 区域 - Sector 0 */
#define BOOTLOADER_ADDR         ((uint32_t)0x08000000)
#define BOOTLOADER_SIZE         ((uint32_t)0x4000)          /* 16KB */

/* APP1 运行区 - Sector 2~4 */
#define APP1_ADDR               ((uint32_t)0x08008000)
#define APP1_SIZE               ((uint32_t)0x18000)         /* 96KB = 16+16+64 */
#define APP1_END_ADDR           ((uint32_t)0x0801FFFF)
#define APP1_SECTOR_START       FLASH_Sector_2
#define APP1_SECTOR_END         FLASH_Sector_4

/* APP2 缓存区 - Sector 5 */
#define APP2_ADDR               ((uint32_t)0x08020000)
#define APP2_SIZE               ((uint32_t)0x20000)         /* 128KB */
#define APP2_END_ADDR           ((uint32_t)0x0803FFFF)
#define APP2_SECTOR_START       FLASH_Sector_5
#define APP2_SECTOR_END         FLASH_Sector_5


/* 最大启动尝试次数 */
#define MAX_BOOT_ATTEMPTS       3


/* ============ APP 有效性检查 ============ */

/*
 * STM32F407 RAM: 0x20000000 - 0x2002FFFF (192KB)
 * 有效的栈指针应该指向 RAM 范围内
 */
#define RAM_BASE                ((uint32_t)0x20000000)
#define RAM_SIZE                ((uint32_t)0x30000)         /* 192KB */
#define RAM_END                 (RAM_BASE + RAM_SIZE)
#define IS_VALID_SP(sp)         (((sp) >= RAM_BASE) && ((sp) <= RAM_END))

/* ============ 函数声明 ============ */

void     Bootloader_Init(void);
void     Bootloader_Run(void);
uint8_t  Bootloader_CheckApp(uint32_t app_addr);
void     Bootloader_JumpToApp(uint32_t app_addr);
uint8_t  Bootloader_CopyAPP2ToAPP1(void);
void     Bootloader_RecoverFromAPP2(void);
void     Bootloader_IncrementBootAttempts(void);
uint8_t  Bootloader_GetBootAttempts(void);
void     Bootloader_SetAppVersion(uint8_t app_num, uint32_t version);
uint32_t Bootloader_GetAppVersion(uint8_t app_num);
void     Bootloader_StartOTA(void);
void     Bootloader_FinishOTA(uint8_t success);

void     Bootloader_ShowMenu(void);
void     Bootloader_CommandLoop(void);

#endif /* __BOOTLOADER_H */

