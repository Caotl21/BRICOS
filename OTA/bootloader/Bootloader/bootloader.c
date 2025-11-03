#include "bootloader.h"
#include "flash_if.h"
#include "Delay.h"

typedef void (*pFunction)(void);

// 外部函数声明
extern void UART_SendString(const char *str);
extern void UART_SendByte(uint8_t byte);

/**
 * @brief  Bootloader初始化
 */
void Bootloader_Init(void)
{
    // 系统时钟已在SystemInit中配置
    // 这里可以添加其他初始化代码
}

/**
 * @brief  检查应用程序是否有效
 * @param  app_addr: 应用程序起始地址
 * @retval 0: 无效, 1: 有效
 */
uint8_t Bootloader_CheckApp(uint32_t app_addr)
{
    uint32_t sp = *(__IO uint32_t*)app_addr;
    
    // 检查栈指针是否在SRAM范围内
    // STM32F103C8T6的SRAM: 0x20000000 - 0x20005000 (20KB)
    if ((sp & 0xFFFE0000) == 0x20000000)
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
	
    // 关闭所有外设时钟（可选）
    RCC->APB1ENR = 0;
    RCC->APB2ENR = 0;
    
    // 获取应用程序的栈顶地址
    __set_MSP(*(__IO uint32_t*)app_addr);
    
    // 获取应用程序的复位向量地址
    JumpAddress = *(__IO uint32_t*)(app_addr + 4);
    Jump_To_Application = (pFunction)JumpAddress;
    
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

    // 检查APP2是否有效
    if (Bootloader_CheckApp(APP2_ADDR) == 0)
    {
        UART_SendString("APP2 invalid!\r\n");
        return 0;
    }

    // 解锁Flash
    FLASH_If_Init();

    UART_SendString("Erasing APP1...\r\n");
    
    // 擦除APP1区域
    for (i = 0; i < (APP1_SIZE / FLASH_PAGE_SIZE); i++)
    {
        if (FLASH_If_ErasePage(APP1_ADDR + (i * FLASH_PAGE_SIZE)) != FLASH_COMPLETE)
        {
            UART_SendString("Erase failed!\r\n");
            return 0;
        }
    }

    UART_SendString("Copying APP2 to APP1...\r\n");
    
    // 复制APP2到APP1 - 修复：先读取到变量
    for (i = 0; i < word_count; i++)
    {
        // 先读取到变量，避免Flash同时读写
        data = *(uint32_t*)(APP2_ADDR + (i * 4));
        
        if (FLASH_If_Write(APP1_ADDR + (i * 4), &data, 1) != FLASH_COMPLETE)
        {
            UART_SendString("Write failed at: ");
            // 可以添加地址信息
            return 0;
        }
        
        // 每写入256个字延时一下，避免Flash过热
        if ((i % 256) == 0) {
            Delay_ms(1);
            UART_SendString(".");  // 进度指示
        }
    }
    
    UART_SendString("\r\nVerifying...\r\n");

    // 验证复制结果
    for (i = 0; i < word_count; i++)
    {
        if (*(uint32_t*)(APP1_ADDR + (i * 4)) != *(uint32_t*)(APP2_ADDR + (i * 4)))
        {
			UART_SendString("Error Location: ");
            UART_SendByte('0' + i);
            UART_SendString("\r\nVerify failed!\r\n");
            return 0;
        }
    }

    UART_SendString("Copy successful!\r\n");
    return 1;
}

/**
 * @brief  从APP2恢复APP1
 */
void Bootloader_RecoverFromAPP2(void)
{
    BootFlag_t boot_flag;
    BootFlag_t *old_flag = (BootFlag_t*)APP_FLAG_ADDR;

    UART_SendString("\r\n========== Copying APP2 to APP1 ==========\r\n");

    // 复制APP2到APP1
    if (Bootloader_CopyAPP2ToAPP1())
    {
        UART_SendString("Copy successful!\r\n");

        // 复制成功，更新标志
        if (old_flag->valid_flag == APP_VALID_FLAG)
        {
            boot_flag = *old_flag;
			UART_SendString("Update Flag successful!\r\n");
        }
        else
        {
            // 初始化
            boot_flag.valid_flag = APP_VALID_FLAG;
            boot_flag.boot_attempts = 0;
            boot_flag.need_copy = 0;
            boot_flag.ota_complete = 0;
            boot_flag.reserved1 = 0;
            boot_flag.app1_version = 0;
            boot_flag.app2_version = 0;
            boot_flag.app1_crc = 0;
            boot_flag.app2_crc = 0;
            boot_flag.boot_count = 0;
            boot_flag.reserved2 = 0;
        }

        boot_flag.boot_attempts = 0;
        boot_flag.need_copy = 0;
        boot_flag.app1_version = boot_flag.app2_version;  // 更新APP1版本

        // 擦除并写入
        FLASH_If_ErasePage(APP_FLAG_ADDR);
        FLASH_If_Write(APP_FLAG_ADDR, (uint32_t*)&boot_flag, sizeof(BootFlag_t));

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
 * @brief  Bootloader主运行函数 - 方案2: 固定APP1运行
 */
//void Bootloader_Run(void)
//{
//    BootFlag_t *boot_flag = (BootFlag_t*)APP_FLAG_ADDR;
//    uint8_t boot_attempts;

//    // 检查是否需要从APP2复制到APP1
//    if (boot_flag->valid_flag == APP_VALID_FLAG && boot_flag->need_copy == 1)
//    {
//        UART_SendString("\r\n>>> OTA Update Detected <<<\r\n");
//        UART_SendString("need_copy flag is set.\r\n");

//        // OTA完成，需要复制APP2到APP1
//        Bootloader_RecoverFromAPP2();
//    }

//    // 获取启动尝试次数
//    boot_attempts = Bootloader_GetBootAttempts();

//    // 检查启动尝试次数
//    if (boot_attempts >= MAX_BOOT_ATTEMPTS)
//    {
//        UART_SendString("\r\n>>> Boot Attempts Exceeded <<<\r\n");

//        // 超过最大尝试次数，从APP2恢复APP1
//        Bootloader_RecoverFromAPP2();
//    }

//    // 增加启动尝试计数
//    Bootloader_IncrementBootAttempts();

//    // 始终尝试运行APP1
//    if (Bootloader_CheckApp(APP1_ADDR))
//    {
//        UART_SendString("\r\n>>> Jumping to APP1 <<<\r\n");
//        Delay_ms(100);  // 等待UART发送完成
//        Bootloader_JumpToApp(APP1_ADDR);
//        // 如果跳转成功，不会返回到这里
//    }

//    // 如果APP1无效，尝试从APP2恢复
//    UART_SendString("\r\n>>> APP1 Invalid, trying to recover <<<\r\n");

//    if (Bootloader_CheckApp(APP2_ADDR))
//    {
//        Bootloader_RecoverFromAPP2();

//        // 再次尝试运行APP1
//        if (Bootloader_CheckApp(APP1_ADDR))
//        {
//            UART_SendString("\r\n>>> Jumping to APP1 (after recovery) <<<\r\n");
//            Delay_ms(100);
//            Bootloader_JumpToApp(APP1_ADDR);
//        }
//    }

//    // 如果都失败了，返回到main函数，进入Bootloader模式
//    UART_SendString("\r\n>>> Cannot start APP1, staying in Bootloader <<<\r\n");
//    // 不要在这里死循环，让main函数处理
//}

void Bootloader_Run(void)
{
    BootFlag_t *boot_flag = (BootFlag_t*)APP_FLAG_ADDR;
    uint8_t boot_attempts;

    UART_SendString("\r\n>>> Bootloader_Run Started <<<\r\n");
    Delay_ms(100);

    // 显示Boot Flag状态
    UART_SendString("Boot Flag Status:\r\n");
    UART_SendString("  valid_flag: ");
    UART_SendString(boot_flag->valid_flag == APP_VALID_FLAG ? "Valid" : "Invalid");
    UART_SendString("\r\n");
    UART_SendString("  need_copy: ");
    UART_SendString(boot_flag->need_copy ? "Yes" : "No");
    UART_SendString("\r\n");
    UART_SendString("  boot_attempts: ");
    // 添加显示boot_attempts的代码
    UART_SendByte('0' + boot_flag->boot_attempts);
    UART_SendString("\r\n");

    // 检查是否需要从APP2复制到APP1
    if (boot_flag->valid_flag == APP_VALID_FLAG && boot_flag->need_copy == 1)
    {
        UART_SendString("\r\n>>> OTA Update Detected - need_copy=1 <<<\r\n");
        Bootloader_RecoverFromAPP2();
    }

    // 获取启动尝试次数
    boot_attempts = Bootloader_GetBootAttempts();
    UART_SendString("Current boot attempts: ");
    UART_SendByte('0' + boot_attempts);
    UART_SendString("\r\n");

    // 检查启动尝试次数
    if (boot_attempts >= MAX_BOOT_ATTEMPTS)
    {
        UART_SendString("\r\n>>> Boot Attempts Exceeded <<<\r\n");
        Bootloader_RecoverFromAPP2();
        boot_attempts = 0; // 重置
    }

    // 增加启动尝试计数
    Bootloader_IncrementBootAttempts();
    UART_SendString("Boot attempts incremented\r\n");

    // 检查APP1有效性
    UART_SendString("Checking APP1 at 0x08004000...\r\n");
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
    UART_SendString("Checking APP2 at 0x08009C00...\r\n");

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
    BootFlag_t *old_flag = (BootFlag_t*)APP_FLAG_ADDR;

    if (old_flag->valid_flag == APP_VALID_FLAG)
    {
        boot_flag = *old_flag;
    }
    else
    {
        // 首次启动，初始化Boot Flag
        boot_flag.valid_flag = APP_VALID_FLAG;
        boot_flag.boot_attempts = 0;
        boot_flag.need_copy = 0;
        boot_flag.ota_complete = 0;
        boot_flag.reserved1 = 0;
        boot_flag.app1_version = 0;
        boot_flag.app2_version = 0;
        boot_flag.app1_crc = 0;
        boot_flag.app2_crc = 0;
        boot_flag.boot_count = 0;
        boot_flag.reserved2 = 0;
    }

    boot_flag.boot_attempts++;
    boot_flag.boot_count++;

    // 擦除并写入
    FLASH_If_ErasePage(APP_FLAG_ADDR);
    FLASH_If_Write(APP_FLAG_ADDR, (uint32_t*)&boot_flag, sizeof(BootFlag_t));
}

/**
 * @brief  标记启动成功（由APP调用）
 */
void Bootloader_MarkBootSuccess(void)
{
    BootFlag_t boot_flag;
    BootFlag_t *old_flag = (BootFlag_t*)APP_FLAG_ADDR;

    if (old_flag->valid_flag == APP_VALID_FLAG)
    {
        boot_flag = *old_flag;
        boot_flag.boot_attempts = 0;  // 重置尝试次数

        // 擦除并写入
        FLASH_If_ErasePage(APP_FLAG_ADDR);
        FLASH_If_Write(APP_FLAG_ADDR, (uint32_t*)&boot_flag, sizeof(BootFlag_t));
    }
}

/**
 * @brief  获取启动尝试次数
 */
uint8_t Bootloader_GetBootAttempts(void)
{
    BootFlag_t *boot_flag = (BootFlag_t*)APP_FLAG_ADDR;

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
    BootFlag_t *old_flag = (BootFlag_t*)APP_FLAG_ADDR;

    if (old_flag->valid_flag == APP_VALID_FLAG)
    {
        boot_flag = *old_flag;
    }
    else
    {
        // 初始化
        boot_flag.valid_flag = APP_VALID_FLAG;
        boot_flag.boot_attempts = 0;
        boot_flag.need_copy = 0;
        boot_flag.ota_complete = 0;
        boot_flag.reserved1 = 0;
        boot_flag.app1_version = 0;
        boot_flag.app2_version = 0;
        boot_flag.app1_crc = 0;
        boot_flag.app2_crc = 0;
        boot_flag.boot_count = 0;
        boot_flag.reserved2 = 0;
    }

    if (app_num == 1)
    {
        boot_flag.app1_version = version;
    }
    else if (app_num == 2)
    {
        boot_flag.app2_version = version;
    }

    // 擦除并写入
    FLASH_If_ErasePage(APP_FLAG_ADDR);
    FLASH_If_Write(APP_FLAG_ADDR, (uint32_t*)&boot_flag, sizeof(BootFlag_t));
}

/**
 * @brief  获取APP版本号
 */
uint32_t Bootloader_GetAppVersion(uint8_t app_num)
{
    BootFlag_t *boot_flag = (BootFlag_t*)APP_FLAG_ADDR;

    if (boot_flag->valid_flag == APP_VALID_FLAG)
    {
        if (app_num == 1)
        {
            return boot_flag->app1_version;
        }
        else if (app_num == 2)
        {
            return boot_flag->app2_version;
        }
    }

    return 0;
}

/**
 * @brief  开始OTA更新（方案2: 始终上传到APP2）
 */
void Bootloader_StartOTA(void)
{
    BootFlag_t boot_flag;
    BootFlag_t *old_flag = (BootFlag_t*)APP_FLAG_ADDR;

    if (old_flag->valid_flag == APP_VALID_FLAG)
    {
        boot_flag = *old_flag;
    }
    else
    {
        // 初始化
        boot_flag.valid_flag = APP_VALID_FLAG;
        boot_flag.boot_attempts = 0;
        boot_flag.need_copy = 0;
        boot_flag.ota_complete = 0;
        boot_flag.reserved1 = 0;
        boot_flag.app1_version = 0;
        boot_flag.app2_version = 0;
        boot_flag.app1_crc = 0;
        boot_flag.app2_crc = 0;
        boot_flag.boot_count = 0;
        boot_flag.reserved2 = 0;
    }

    boot_flag.ota_complete = 0;  // 标记OTA开始

    // 擦除并写入
    FLASH_If_ErasePage(APP_FLAG_ADDR);
    FLASH_If_Write(APP_FLAG_ADDR, (uint32_t*)&boot_flag, sizeof(BootFlag_t));
}

/**
 * @brief  完成OTA更新（方案2: 设置need_copy标志）
 */
void Bootloader_FinishOTA(uint8_t success)
{
    BootFlag_t boot_flag;
    BootFlag_t *old_flag = (BootFlag_t*)APP_FLAG_ADDR;

    if (old_flag->valid_flag == APP_VALID_FLAG)
    {
        boot_flag = *old_flag;

        if (success)
        {
            UART_SendString("\r\n>>> Setting need_copy flag <<<\r\n");

            // OTA成功，设置need_copy标志
            // 下次启动时会自动从APP2复制到APP1
            boot_flag.ota_complete = 1;
            boot_flag.need_copy = 1;
            boot_flag.boot_attempts = 0;

            UART_SendString("ota_complete = 1\r\n");
            UART_SendString("need_copy = 1\r\n");
            UART_SendString("boot_attempts = 0\r\n");
        }
        else
        {
            UART_SendString("\r\n>>> OTA Failed, clearing flags <<<\r\n");

            // OTA失败，清除标志
            boot_flag.ota_complete = 0;
            boot_flag.need_copy = 0;
        }

        // 擦除并写入
        FLASH_If_ErasePage(APP_FLAG_ADDR);
        FLASH_If_Write(APP_FLAG_ADDR, (uint32_t*)&boot_flag, sizeof(BootFlag_t));

        UART_SendString("Boot flags saved to Flash.\r\n");
    }
    else
    {
        UART_SendString("\r\n>>> Warning: Boot Flag invalid! <<<\r\n");
    }
}

