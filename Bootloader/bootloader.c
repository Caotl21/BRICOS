#include "bootloader.h"
#include "flash_if.h"
#include "Delay.h"
#include "flash_layout.h"
#include "ymodem.h"
#include "Serial.h"
#include "boot_flag.h"
#include <string.h>

typedef void (*pFunction)(void);

static uint8_t g_ymodem_packet_data[PACKET_1K_SIZE + PACKET_OVERHEAD];

/**
 * @brief  Bootloader初始化
 */
void Bootloader_Init(void)
{
    /* 系统时钟已在SystemInit中配置 */
}

/**
 * @brief  检查应用程序是否有效
 * @param  app_addr: 应用程序起始地址
 * @retval 0: 无效, 1: 有效
 */
uint8_t Bootloader_CheckApp(uint32_t app_addr)
{
    uint32_t sp = *(__IO uint32_t *)app_addr;

    /* STM32F407 RAM: 0x20000000 - 0x2002FFFF (192KB) */
    if (IS_VALID_SP(sp))
    {
        return 1;
    }

    return 0;
}

/**
 * @brief  跳转到应用程序
 * @param  app_addr: 应用程序起始地址
 */
void Bootloader_JumpToApp(uint32_t app_addr)
{
    pFunction Jump_To_Application;
    uint32_t JumpAddress;
    
    // 检查应用程序是否有效
    if (Bootloader_CheckApp(app_addr) == 0)
    {
        return;
    }

    
    
    // 关闭所有中断
    __disable_irq();
    
    // 关闭SysTick
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;
	
    // 设置向量表偏移
    SCB->VTOR = app_addr;
	
    /* 关闭所有外设时钟 (F407 比 F103 多了 AHB1/AHB2/AHB3) */
    RCC->AHB1ENR = 0;
    RCC->AHB2ENR = 0;
    RCC->AHB3ENR = 0;
    RCC->APB1ENR = 0;
    RCC->APB2ENR = 0;

    /* 清除所有中断挂起标志 */
    {
        uint32_t i;
        for (i = 0; i < 8; i++)
        {
            NVIC->ICER[i] = 0xFFFFFFFF;  /* 关闭所有中断 */
            NVIC->ICPR[i] = 0xFFFFFFFF;  /* 清除所有挂起 */
        }
    }
    

#if (__FPU_PRESENT == 1) && (__FPU_USED == 1)
    /* 关闭 FPU (F407 特有) */
    SCB->CPACR &= ~((3UL << 20) | (3UL << 22));
#endif
    
    /* 设置主栈指针 */
    __set_MSP(*(__IO uint32_t*)app_addr);
    
    // 获取应用程序的复位向量地址
    JumpAddress = *(__IO uint32_t*)(app_addr + 4);
    Jump_To_Application = (pFunction)JumpAddress;

    /* 重新使能中断（APP会在自己的初始化中配置） */
    __enable_irq();
    
    // 跳转到应用程序
    Jump_To_Application();
}

/**
 * @brief  复制APP2到APP1
 * @retval 0: 失败, 1: 成功
 */
uint8_t Bootloader_CopyAPP2ToAPP1(void)
{
    uint32_t i;
    uint32_t data;
    uint32_t word_count = APP1_SIZE / 4;
    uint32_t sector;

    // 检查APP2是否有效
    if (Bootloader_CheckApp(APP2_ADDR) == 0)
    {
        UART_SendString("APP2 invalid!\r\n");
        return 0;
    }

    // 解锁Flash
    FLASH_If_Init();

    UART_SendString("Erasing APP1...\r\n");

    /* 擦除APP1区域: Sector 2, 3, 4 */
    for (sector = APP1_SECTOR_START; sector <= APP1_SECTOR_END; sector = Flash_GetNextSector(sector))
    {
        if (FLASH_EraseSector(sector, VoltageRange_3) != FLASH_COMPLETE)
        {
            UART_SendString("Erase sector failed!\r\n");
            FLASH_Lock();
            return 0;
        }
        UART_SendByte('.');
    }
    UART_SendString(" Done\r\n");

    UART_SendString("Copying APP2 to APP1...\r\n");
    
    /* 逐字复制 */
    for (i = 0; i < word_count; i++)
    {
        data = *(uint32_t *)(APP2_ADDR + (i * 4));

        if (FLASH_ProgramWord(APP1_ADDR + (i * 4), data) != FLASH_COMPLETE)
        {
            UART_SendString("Write failed at offset: ");
            UART_SendInt(i * 4);
            UART_SendString("\r\n");
            FLASH_Lock();
            return 0;
        }

        /* 每4KB显示进度 */
        if ((i % 1024) == 0)
        {
            UART_SendByte('.');
        }
    }
    UART_SendString(" Done\r\n");

    FLASH_Lock();

    UART_SendString("Verifying...\r\n");

    /* 验证 */
    for (i = 0; i < word_count; i++)
    {
        if (*(uint32_t *)(APP1_ADDR + (i * 4)) != *(uint32_t *)(APP2_ADDR + (i * 4)))
        {
            UART_SendString("Verify failed at offset: ");
            UART_SendInt(i * 4);
            UART_SendString("\r\n");
            return 0;
        }
    }

    UART_SendString("Copy & Verify successful!\r\n");
    return 1;
}

/**
 * @brief  从APP2恢复APP1
 */
void Bootloader_RecoverFromAPP2(void)
{
    BootFlag_t boot_flag;

    UART_SendString("\r\n========== Copying APP2 to APP1 ==========\r\n");

    if (Bootloader_CopyAPP2ToAPP1())
    {
        UART_SendString("Copy successful!\r\n");

        /* 更新标志 */
        BootFlag_Read(&boot_flag);
        boot_flag.boot_attempts = 0;
        boot_flag.need_copy     = 0;
        boot_flag.app1_version  = boot_flag.app2_version;

        BootFlag_Save(&boot_flag);

        UART_SendString("Boot flags updated.\r\n");
        UART_SendString("===========================================\r\n");
    }
    else
    {
        UART_SendString("Copy failed! APP2 may be invalid.\r\n");
        UART_SendString("===========================================\r\n");
    }
}

/**
 * @brief  Bootloader主运行函数
 */
void Bootloader_Run(void)
{
    BootFlag_t *boot_flag = (BootFlag_t*)APP_FLAG_ADDR;
    uint8_t boot_attempts;

    UART_SendString("\r\n>>> Bootloader_Run Started <<<\r\n");
    Delay_ms(100);

    /* 显示Boot Flag状态 */
    UART_SendString("Boot Flag Status:\r\n");
    UART_SendString("  valid_flag: ");
    UART_SendString(boot_flag->valid_flag == APP_VALID_FLAG ? "Valid" : "Invalid");
    UART_SendString("\r\n");
    UART_SendString("  need_copy: ");
    UART_SendString(boot_flag->need_copy ? "Yes" : "No");
    UART_SendString("\r\n");
    UART_SendString("  boot_attempts: ");
    UART_SendByte('0' + boot_flag->boot_attempts);
    UART_SendString("\r\n");

    /* 检查是否需要从APP2复制到APP1 */
    if (boot_flag->valid_flag == APP_VALID_FLAG && boot_flag->need_copy == 1)
    {
        UART_SendString("\r\n>>> OTA Update Detected - need_copy=1 <<<\r\n");
        Bootloader_RecoverFromAPP2();
    }

    /* 获取启动尝试次数 */
    boot_attempts = Bootloader_GetBootAttempts();
    UART_SendString("Current boot attempts: ");
    UART_SendByte('0' + boot_attempts);
    UART_SendString("\r\n");

    /* 超过最大尝试次数 */
    if (boot_attempts >= MAX_BOOT_ATTEMPTS)
    {
        UART_SendString("\r\n>>> Boot Attempts Exceeded <<<\r\n");
        Bootloader_RecoverFromAPP2();
        boot_attempts = 0;
    }

    /* 增加启动尝试计数 */
    Bootloader_IncrementBootAttempts();
    UART_SendString("Boot attempts incremented\r\n");

    // 检查APP1有效性
    UART_SendString("Checking APP1 at 0x08008000...\r\n");
    if (Bootloader_CheckApp(APP1_ADDR))
    {
        UART_SendString("APP1 is VALID - Jumping...\r\n");
        Delay_ms(100);
        Bootloader_JumpToApp(APP1_ADDR);
        // 如果跳转成功，不会返回到这里
    }
    else
    {
        UART_SendString("APP1 is INVALID\r\n");
    }

    // 如果APP1无效，尝试从APP2恢复
    UART_SendString("\r\n>>> APP1 Invalid, trying recovery <<<\r\n");
    UART_SendString("Checking APP2 at 0x08020000...\r\n");

    if (Bootloader_CheckApp(APP2_ADDR))
    {
        UART_SendString("APP2 is VALID - Starting recovery...\r\n");
        Bootloader_RecoverFromAPP2();

        UART_SendString("Checking APP1 after recovery...\r\n");
        if (Bootloader_CheckApp(APP1_ADDR))
        {
            UART_SendString("APP1 recovered - Jumping...\r\n");
            Delay_ms(100);
            Bootloader_JumpToApp(APP1_ADDR);
        }
        else
        {
            UART_SendString("Recovery FAILED - APP1 still invalid\r\n");
        }
    }
    else
    {
        UART_SendString("APP2 is INVALID - No recovery possible\r\n");
    }

    UART_SendString("\r\n>>> All attempts failed - Staying in Bootloader <<<\r\n");
}

/**
 * @brief  增加启动尝试次数
 */
void Bootloader_IncrementBootAttempts(void)
{
    BootFlag_t boot_flag;
    BootFlag_Read(&boot_flag);

    boot_flag.boot_attempts++;
    boot_flag.boot_count++;

    BootFlag_Save(&boot_flag);
}


/**
 * @brief  获取启动尝试次数
 */
uint8_t Bootloader_GetBootAttempts(void)
{
    BootFlag_t *boot_flag = (BootFlag_t *)APP_FLAG_ADDR;

    if (boot_flag->valid_flag == APP_VALID_FLAG)
        return boot_flag->boot_attempts;

    return 0;
}

/**
 * @brief  设置APP版本号
 */
void Bootloader_SetAppVersion(uint8_t app_num, uint32_t version)
{
    BootFlag_t boot_flag;
    BootFlag_Read(&boot_flag);

    if (app_num == 1)
        boot_flag.app1_version = version;
    else if (app_num == 2)
        boot_flag.app2_version = version;

    BootFlag_Save(&boot_flag);
}

/**
 * @brief  获取APP版本号
 */
uint32_t Bootloader_GetAppVersion(uint8_t app_num)
{
    BootFlag_t *boot_flag = (BootFlag_t *)APP_FLAG_ADDR;

    if (boot_flag->valid_flag == APP_VALID_FLAG)
    {
        if (app_num == 1)
            return boot_flag->app1_version;
        else if (app_num == 2)
            return boot_flag->app2_version;
    }

    return 0;
}

/**
 * @brief  开始OTA更新搬运 只设置标志位
 */
void Bootloader_StartOTA(void)
{
    BootFlag_t boot_flag;
    BootFlag_Read(&boot_flag);

    boot_flag.ota_complete = 0;

    BootFlag_Save(&boot_flag);
}

/**
 * @brief  完成OTA更新搬运（设置need_copy标志）
 * @param  success: 1:成功, 0:失败
 */
void Bootloader_FinishOTA(uint8_t success)
{
    BootFlag_t boot_flag;
    BootFlag_Read(&boot_flag);

    if (success)
    {
        boot_flag.ota_complete = 1;
        boot_flag.need_copy    = 1;  /* 下次启动时复制APP2→APP1 */
    }
    else
    {
        boot_flag.ota_complete = 0;
        boot_flag.need_copy    = 0;
    }

    BootFlag_Save(&boot_flag);
}

/* ============ 显示版本号 ============ */

/**
 * @brief  显示版本号 (格式: X.Y.Z)
 */
static void Show_Version(uint32_t ver)
{
    if (ver > 0)
    {
        UART_SendByte('0' + (ver / 100) % 10);
        UART_SendByte('.');
        UART_SendByte('0' + (ver / 10) % 10);
        UART_SendByte('.');
        UART_SendByte('0' + ver % 10);
    }
    else
    {
        UART_SendString("N/A");
    }
}

/* ============ 显示菜单 ============ */

/**
 * @brief  显示命令菜单
 */
void Bootloader_ShowMenu(void)
{
    uint32_t app1_ver = Bootloader_GetAppVersion(1);
    uint32_t app2_ver = Bootloader_GetAppVersion(2);

    UART_SendString("\r\n");
    UART_SendString("========================================\r\n");
    UART_SendString("  STM32F407 OTA Bootloader v2.0\r\n");
    UART_SendString("========================================\r\n");
    UART_SendString("Flash Layout:\r\n");
    UART_SendString("  Bootloader: 0x08000000 (16KB)\r\n");
    UART_SendString("  Boot Flag:  0x08004000 (16KB)\r\n");
    UART_SendString("  APP1 (Run): 0x08008000 (96KB)\r\n");
    UART_SendString("  APP2 (OTA): 0x08020000 (128KB)\r\n");
    UART_SendString("----------------------------------------\r\n");
    UART_SendString("APP1 Version: ");
    Show_Version(app1_ver);
    UART_SendString("\r\nAPP2 Version: ");
    Show_Version(app2_ver);
    UART_SendString("\r\n");
    UART_SendString("========================================\r\n");
    UART_SendString("Commands:\r\n");
    UART_SendString("  2 - Download firmware to APP2 (Ymodem)\r\n");
    UART_SendString("  C - Copy APP2 -> APP1 (Manual)\r\n");
    UART_SendString("  J - Jump to APP1\r\n");
    UART_SendString("  O - OTA Simulation (set need_copy)\r\n");
    UART_SendString("  I - Show System Info\r\n");
    UART_SendString("  E - Erase APP1\r\n");
    UART_SendString("  F - Erase APP2\r\n");
    UART_SendString("  R - Reset (reboot)\r\n");
    UART_SendString("  M - Show this menu\r\n");
    UART_SendString("========================================\r\n");
    UART_SendString("READY\r\n");
}

/* ============ 显示系统信息 ============ */

/**
 * @brief  显示系统详细信息
 */
static void Bootloader_ShowSystemInfo(void)
{
    uint8_t attempts = Bootloader_GetBootAttempts();
    BootFlag_t *flag = (BootFlag_t *)APP_FLAG_ADDR;

    UART_SendString("\r\n========== System Info ==========\r\n");

    /* Boot Flag 状态 */
    UART_SendString("Boot Flag:\r\n");
    UART_SendString("  valid_flag:    ");
    UART_SendString(flag->valid_flag == APP_VALID_FLAG ? "Valid" : "Invalid");
    UART_SendString("\r\n  need_copy:     ");
    UART_SendByte('0' + (flag->valid_flag == APP_VALID_FLAG ? flag->need_copy : 0));
    UART_SendString("\r\n  ota_complete:  ");
    UART_SendByte('0' + (flag->valid_flag == APP_VALID_FLAG ? flag->ota_complete : 0));
    UART_SendString("\r\n  boot_attempts: ");
    UART_SendByte('0' + attempts);
    UART_SendString("\r\n  boot_count:    ");
    UART_SendInt(flag->valid_flag == APP_VALID_FLAG ? flag->boot_count : 0);
    UART_SendString("\r\n");

    /* APP 状态 */
    UART_SendString("APP1 (0x08008000): ");
    UART_SendString(Bootloader_CheckApp(APP1_ADDR) ? "VALID" : "INVALID");
    UART_SendString("  Ver: ");
    Show_Version(Bootloader_GetAppVersion(1));
    UART_SendString("\r\n");

    UART_SendString("APP2 (0x08020000): ");
    UART_SendString(Bootloader_CheckApp(APP2_ADDR) ? "VALID" : "INVALID");
    UART_SendString("  Ver: ");
    Show_Version(Bootloader_GetAppVersion(2));
    UART_SendString("\r\n");

    /* APP1 向量表前几个字 */
    UART_SendString("APP1 SP:  ");
    UART_SendHex(*(__IO uint32_t *)APP1_ADDR);
    UART_SendString("\r\nAPP1 PC:  ");
    UART_SendHex(*(__IO uint32_t *)(APP1_ADDR + 4));
    UART_SendString("\r\n");

    UART_SendString("APP2 SP:  ");
    UART_SendHex(*(__IO uint32_t *)APP2_ADDR);
    UART_SendString("\r\nAPP2 PC:  ");
    UART_SendHex(*(__IO uint32_t *)(APP2_ADDR + 4));
    UART_SendString("\r\n");

    UART_SendString("================================\r\n");
}

/* ============ Ymodem 下载处理 ============ */

/**
 * @brief  处理Ymodem接收并烧录
 * @param  app_num: 应用编号 (方案2中始终是2, 写入APP2区域)
 * @retval 0:成功, <0:失败
 */
static int32_t Process_YmodemDownload(uint8_t app_num)
{
    FileInfo_t file_info;
    int32_t packet_length;
    uint32_t app_addr;
    uint32_t app_max_size;
    uint32_t errors = 0;
    uint32_t packets_received = 0;
    uint32_t file_size = 0;
    uint32_t bytes_written = 0;
    int32_t result;

    /* 方案2: 始终上传到APP2 */
    if (app_num == 2)
    {
        app_addr     = APP2_ADDR;
        app_max_size = APP2_SIZE;
    }
    else
    {
        UART_SendString("Invalid app_num\r\n");
        return -1;
    }

    /* 标记OTA开始 */
    Bootloader_StartOTA();

    memset(&file_info, 0, sizeof(file_info));

    UART_SendString("\r\nWaiting for file (Ymodem)...\r\n");

    /* 发送'C'表示准备接收(使用CRC16) */
    for (uint32_t i = 0; i < 3; i++)
    {
        Ymodem_SendByte('C');
        Delay_ms(500);
    }

    /* 擦除APP2区域 */
    UART_SendString("Erasing APP2...\r\n");
    if (FLASH_If_Erase(APP2_ADDR, APP2_SIZE) != FLASH_COMPLETE)
    {
        UART_SendString("Erase APP2 failed!\r\n");
        Bootloader_FinishOTA(0);
        return -2;
    }
    UART_SendString("Erase APP2 done.\r\n");

    /* 接收文件数据包循环 */
    while (1)
    {
        result = Ymodem_ReceivePacket(g_ymodem_packet_data, &packet_length, 1000);

        if (result == 0)
        {
            errors = 0;

            if (packet_length == 0)
            {
                Ymodem_SendByte(ACK);
                break;
            }

            if (packets_received == 0)
            {
                if (g_ymodem_packet_data[0] != 0)
                {
                    Ymodem_ParseFileInfo(g_ymodem_packet_data, &file_info);
                    file_size = file_info.file_size;

                    UART_SendString("File: ");
                    UART_SendString((const char *)file_info.file_name);
                    UART_SendString("\r\nSize: ");
                    UART_SendInt(file_size);
                    UART_SendString(" bytes\r\n");

                    if (file_size > app_max_size)
                    {
                        UART_SendString("File too large!\r\n");
                        Ymodem_Abort();
                        Bootloader_FinishOTA(0);
                        return -3;
                    }
                }

                Ymodem_SendByte(ACK);
                Ymodem_SendByte('C');
                packets_received++;
            }
            else
            {
                uint32_t write_len = packet_length;

                if (bytes_written + write_len > file_size)
                {
                    write_len = file_size - bytes_written;
                }

                if (FLASH_If_Write(app_addr + bytes_written,
                                   (uint32_t *)g_ymodem_packet_data,
                                   write_len) != FLASH_COMPLETE)
                {
                    UART_SendString("Flash write error!\r\n");
                    Ymodem_Abort();
                    Bootloader_FinishOTA(0);
                    return -4;
                }

                bytes_written += write_len;
                packets_received++;

                Ymodem_SendByte(ACK);

                if ((packets_received % 10) == 0)
                {
                    Ymodem_SendByte('.');
                }
            }
        }
        else if (result == -1)
        {
            UART_SendString("\r\nAborted by sender.\r\n");
            Bootloader_FinishOTA(0);
            return -5;
        }
        else
        {
            errors++;
            if (errors > 5)
            {
                UART_SendString("\r\nToo many errors!\r\n");
                Ymodem_Abort();
                Bootloader_FinishOTA(0);
                return -6;
            }
            Ymodem_SendByte(NAK);
        }
    }

    /* 下载完成 */
    UART_SendString("\r\n\r\nDownload complete!\r\n");
    UART_SendString("Total bytes: ");
    UART_SendInt(bytes_written);
    UART_SendString("\r\n");

    if (Bootloader_CheckApp(APP2_ADDR) == 0)
    {
        UART_SendString("APP2 verification failed!\r\n");
        Bootloader_FinishOTA(0);
        return -7;
    }

    Bootloader_FinishOTA(1);

    UART_SendString("OTA success! Rebooting...\r\n");
    Delay_ms(500);

    NVIC_SystemReset();

    return 0;
}

/* ============ 命令处理 ============ */

/**
 * @brief  处理来自USART1的调试命令
 * @param  cmd: 收到的命令字符
 */
static void Bootloader_ProcessCommand(uint8_t cmd)
{
    switch (cmd)
    {
        case '2':
            /* 通过Ymodem下载固件到APP2 */
            UART_SendString("\r\n>>> Download to APP2 (Ymodem) <<<\r\n");
            Process_YmodemDownload(2);
            UART_SendString("READY\r\n");
            break;

        case 'C':
        case 'c':
            /* 手动复制APP2到APP1 */
            UART_SendString("\r\n>>> Manual Copy: APP2 -> APP1 <<<\r\n");
            if (Bootloader_CheckApp(APP2_ADDR))
            {
                Bootloader_RecoverFromAPP2();
            }
            else
            {
                UART_SendString("APP2 is INVALID, cannot copy.\r\n");
            }
            UART_SendString("READY\r\n");
            break;

        case 'J':
        case 'j':
            /* 跳转到APP1 */
            UART_SendString("\r\n>>> Jumping to APP1 <<<\r\n");
            if (Bootloader_CheckApp(APP1_ADDR))
            {
                UART_SendString("APP1 valid, jumping...\r\n");
                Delay_ms(100);
                Bootloader_JumpToApp(APP1_ADDR);
            }
            UART_SendString("Jump failed! APP1 invalid.\r\n");
            UART_SendString("READY\r\n");
            break;

        case 'O':
        case 'o':
            /* OTA模拟测试：设置need_copy标志 */
            UART_SendString("\r\n>>> Simulating OTA Completion <<<\r\n");
            if (Bootloader_CheckApp(APP2_ADDR))
            {
                Bootloader_FinishOTA(1);
                UART_SendString("OTA flags set (need_copy=1).\r\n");
                UART_SendString("Reboot to apply update.\r\n");
            }
            else
            {
                UART_SendString("APP2 is INVALID, cannot simulate OTA.\r\n");
            }
            UART_SendString("READY\r\n");
            break;

        case 'I':
        case 'i':
            /* 显示系统信息 */
            Bootloader_ShowSystemInfo();
            break;

        case 'E':
        case 'e':
            /* 擦除APP1 */
            UART_SendString("\r\n>>> Erasing APP1 <<<\r\n");
            if (FLASH_If_Erase(APP1_ADDR, APP1_SIZE) == FLASH_COMPLETE)
            {
                UART_SendString("APP1 erased.\r\n");
            }
            else
            {
                UART_SendString("APP1 erase failed!\r\n");
            }
            UART_SendString("READY\r\n");
            break;

        case 'F':
        case 'f':
            /* 擦除APP2 */
            UART_SendString("\r\n>>> Erasing APP2 <<<\r\n");
            if (FLASH_If_Erase(APP2_ADDR, APP2_SIZE) == FLASH_COMPLETE)
            {
                UART_SendString("APP2 erased.\r\n");
            }
            else
            {
                UART_SendString("APP2 erase failed!\r\n");
            }
            UART_SendString("READY\r\n");
            break;

        case 'R':
        case 'r':
            /* 软件复位 */
            UART_SendString("\r\n>>> Rebooting... <<<\r\n");
            Delay_ms(200);
            NVIC_SystemReset();
            break;

        case 'M':
        case 'm':
            /* 重新显示菜单 */
            Bootloader_ShowMenu();
            break;

        default:
            break;
    }
}

void Bootloader_CommandLoop(void)
{
    uint8_t cmd;

    Bootloader_ShowMenu();

    while (1)
    {
        if (USART_GetFlagStatus(USART1, USART_FLAG_RXNE) != RESET)
        {
            cmd = USART_ReceiveData(USART1);
            Bootloader_ProcessCommand(cmd);
        }
    }
}

