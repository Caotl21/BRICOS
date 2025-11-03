#ifndef __BOOTLOADER_H
#define __BOOTLOADER_H

#include "stm32f10x.h"

/* Flash分区定义 - STM32F103C8T6 64KB Flash */
#define FLASH_BASE_ADDR         0x08000000
#define FLASH_SIZE              0x00010000  // 64KB

/* Bootloader区域: 16KB */
#define BOOTLOADER_ADDR         0x08000000
#define BOOTLOADER_SIZE         0x00004000  // 16KB

/* APP1区域: 24KB */
#define APP1_ADDR               0x08004000
#define APP1_SIZE               0x00005C00  // 24KB

/* APP2区域: 24KB */
#define APP2_ADDR               0x08009C00
#define APP2_SIZE               0x00005C00  // 24KB

/* Flash页大小 */
#define FLASH_PAGE_SIZE         0x400       // 1KB

/* 应用标志定义 */
#define APP_FLAG_ADDR           (FLASH_BASE_ADDR + FLASH_SIZE - FLASH_PAGE_SIZE)  // 最后一页用于存储标志
#define APP_VALID_FLAG          0x55AA55AA

/* 方案2配置: APP1固定运行，APP2作为缓存/备份 */
#define MAX_BOOT_ATTEMPTS       3   // 最大启动尝试次数
#define BOOT_SUCCESS_FLAG       0xA5A5A5A5  // 启动成功标志

/* 应用信息结构体 */
typedef struct {
    uint32_t valid_flag;        // 有效标志
    uint32_t app_size;          // 应用大小
    uint32_t app_crc;           // CRC校验值
    uint32_t version;           // 版本号
} AppInfo_t;

/* 启动标志结构体 - 方案2: 固定APP1运行 */
typedef struct {
    uint32_t valid_flag;        // 有效标志 0x55AA55AA
    uint8_t  boot_attempts;     // APP1启动尝试次数
    uint8_t  need_copy;         // 需要从APP2复制到APP1 (1=需要)
    uint8_t  ota_complete;      // OTA完成标志 (1=APP2接收完成)
    uint8_t  reserved1;         // 保留字段
    uint32_t app1_version;      // APP1版本号
    uint32_t app2_version;      // APP2版本号（缓存区）
    uint32_t app1_crc;          // APP1 CRC校验值
    uint32_t app2_crc;          // APP2 CRC校验值
    uint32_t boot_count;        // 总启动次数
    uint32_t reserved2;         // 保留字段
} BootFlag_t;



/* 函数声明 */
void Bootloader_Init(void);
void Bootloader_JumpToApp(uint32_t app_addr);
uint8_t Bootloader_CheckApp(uint32_t app_addr);
void Bootloader_Run(void);

/* 方案2功能: 固定APP1运行 + APP2缓存 */
void Bootloader_IncrementBootAttempts(void);
void Bootloader_MarkBootSuccess(void);
void Bootloader_RecoverFromAPP2(void);          // 从APP2恢复APP1
uint8_t Bootloader_GetBootAttempts(void);
void Bootloader_SetAppVersion(uint8_t app_num, uint32_t version);
uint32_t Bootloader_GetAppVersion(uint8_t app_num);
void Bootloader_StartOTA(void);                 // 开始OTA（始终上传到APP2）
void Bootloader_FinishOTA(uint8_t success);     // 完成OTA（复制APP2到APP1）
uint8_t Bootloader_CopyAPP2ToAPP1(void);        // 复制APP2到APP1

#endif /* __BOOTLOADER_H */

