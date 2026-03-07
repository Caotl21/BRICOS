#include "stm32f4xx.h"
#include "bootloader.h"
#include "ymodem.h"
#include "flash_if.h"
#include "flash_layout.h"
#include "boot_flag.h"
#include "Serial.h"
#include <string.h>
#include "stm32f4xx_it.h"
#include "Delay.h"

#define BOOTLOADER_TIMEOUT      3000    /* Bootloader超时时间(ms) */
#define DOWNLOAD_BUFFER_SIZE    (2 * 1024)

/**
 * @brief  SysTick初始化 (1ms中断)
 */
static void SysTick_Init(void)
{
    /* F407: SystemCoreClock = 168MHz */
    SysTick_Config(SystemCoreClock / 1000);
}



/* ============ Bootloader 模式检测 ============ */

/**
 * @brief  检查是否需要进入Bootloader模式
 * @retval 1: 进入Bootloader, 0: 跳转到APP
 */
static uint8_t Check_BootloaderMode(void)
{
    uint32_t start_tick;

    start_tick = Timebase_GetTickMs();

    /* 3秒内检查USART1是否收到 'B' */
    while ((Timebase_GetTickMs() - start_tick) < BOOTLOADER_TIMEOUT)
    {
        if (USART_GetFlagStatus(USART1, USART_FLAG_RXNE) != RESET)
        {
            if (USART_ReceiveData(USART1) == 'B')
            {
                return 1;
            }
        }
    }

    return 0;
}

/* ============ 主函数 ============ */

/**
 * @brief  主函数
 *
 * 启动流程：
 *   1. 初始化外设 (SysTick, UART)
 *   2. 等待3秒，检测 USART1/USART2 是否收到 'B'
 *   3. 收到 'B'  → 进入Bootloader命令模式（支持各种测试命令）
 *   4. 超时      → 执行正常 Bootloader 流程 (检查Flag、拷贝、跳转APP1)
 *   5. 若APP无效 → 自动进入命令模式等待OTA
 */
int main(void)
{
    /* 确保向量表指向Bootloader区域 */
    SCB->VTOR = BOOTLOADER_ADDR;

    /* 初始化 */
    SysTick_Init();
    Delay_Init();
    UART_Init();
    Bootloader_Init();

    Serial_SendString("\r\nhello world!\r\n");

    /* 显示当前 Boot Flag 状态 */
    {
        BootFlag_t *flag = (BootFlag_t *)APP_FLAG_ADDR;
        Serial_SendString("Boot Flag: ");
        Serial_SendString(flag->valid_flag == APP_VALID_FLAG ? "Valid" : "Invalid");
        Serial_SendString("  need_copy=");
        Serial_SendByte('0' + (flag->valid_flag == APP_VALID_FLAG ? flag->need_copy : 0));
        Serial_SendString("  attempts=");
        Serial_SendByte('0' + (flag->valid_flag == APP_VALID_FLAG ? flag->boot_attempts : 0));
        Serial_SendString("\r\n");
    }

    /* 等待3秒检测是否进入Bootloader模式 */
    Serial_SendString("Send 'B' within 3s to enter Bootloader mode...\r\n");

    if (Check_BootloaderMode())
    {
       Bootloader_CommandLoop();
    }
    else
    {
        /* ========== 正常启动模式 ========== */
        Serial_SendString("\r\n>>> Normal Boot Mode <<<\r\n");

        /*
         * Bootloader_Run 内部处理：
         * 1. 检查 need_copy → 复制APP2到APP1
         * 2. 检查 boot_attempts → 超限则恢复
         * 3. 检查 APP1 有效性 → 跳转
         * 4. APP1无效 → 尝试从APP2恢复
         * 5. 全部失败 → 返回
         */
        Bootloader_Run();

        /* 如果 Bootloader_Run 返回，说明无法跳转APP */
        /* 自动进入命令模式，等待OTA */
        Serial_SendString("\r\nNo valid APP found.\r\n");
        Serial_SendString("Entering Bootloader command mode...\r\n");

        Bootloader_CommandLoop();
    }
}


