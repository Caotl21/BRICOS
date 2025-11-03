#include "stm32f10x.h"
#include "Delay.h"
#include "LED.h"

// 应用版本号 (修改这里来标识不同版本)
#define APP_VERSION_MAJOR   1
#define APP_VERSION_MINOR   0
#define APP_VERSION_PATCH   0
#define APP_VERSION_CODE    ((APP_VERSION_MAJOR * 100) + (APP_VERSION_MINOR * 10) + APP_VERSION_PATCH)

/* Flash分区定义 - STM32F103C8T6 64KB Flash */
#define FLASH_BASE_ADDR         0x08000000
#define FLASH_SIZE              0x00010000  // 64KB

/* Flash页大小 */
#define FLASH_PAGE_SIZE         0x400       // 1KB

/* 应用标志定义 */
#define APP_FLAG_ADDR           (FLASH_BASE_ADDR + FLASH_SIZE - FLASH_PAGE_SIZE)  // 最后一页用于存储标志
#define APP_VALID_FLAG          0x55AA55AA

// 启动标志结构体 - 方案2: 固定APP1运行
typedef struct {
    uint32_t valid_flag;        // 有效标志 0x55AA55AA
    uint8_t  boot_attempts;     // APP1启动cut尝试次数
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

// Flash操作函数声明
void FLASH_Unlock_Custom(void);
void FLASH_Lock_Custom(void);
void FLASH_ErasePage_Custom(uint32_t addr);
void FLASH_WriteWord_Custom(uint32_t addr, uint32_t data);

/**
 * @brief  UART发送字符串
 */
void UART_SendString(const char *str)
{
    while (*str)
    {
        while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
        USART_SendData(USART1, *str++);
    }
}

/**
 * @brief  UART发送字节
 */
void UART_SendByte(uint8_t byte)
{
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    USART_SendData(USART1, byte);
}

/**
 * @brief  UART初始化
 */
void Debug_UART_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);
    
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    
    USART_InitStructure.USART_BaudRate = 115200;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;  // 启用接收
    USART_Init(USART1, &USART_InitStructure);

    USART_Cmd(USART1, ENABLE);
}

/**
 * @brief  检查是否收到进入Bootloader命令
 * @retval 1: 收到'B'命令, 0: 未收到
 */
uint8_t Check_BootloaderCommand(void)
{
    if (USART_GetFlagStatus(USART1, USART_FLAG_RXNE) == SET)
    {
        uint8_t data = USART_ReceiveData(USART1);
        if (data == 'B')
        {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief  标记启动成功（通知Bootloader）
 * 这是A/B分区OTA的关键函数！
 */
void MarkBootSuccess(void)
{
    BootFlag_t boot_flag;
    BootFlag_t *old_flag = (BootFlag_t*)APP_FLAG_ADDR;
    
    // 读取当前标志
    if (old_flag->valid_flag == APP_VALID_FLAG)
    {
        boot_flag = *old_flag;
        
        // 如果已经标记成功，不需要重复写入
        if (boot_flag.boot_attempts == 0)
        {
            return;
        }
        
        // 重置启动尝试次数（方案2: 始终运行APP1）
        boot_flag.boot_attempts = 0;

        // 更新APP1的版本号（方案2: 始终是APP1）
        boot_flag.app1_version = APP_VERSION_CODE;
        
        // 解锁Flash
        FLASH_Unlock_Custom();
        
        // 擦除标志页
        FLASH_ErasePage_Custom(APP_FLAG_ADDR);
        
        // 写入新标志
        uint32_t *src = (uint32_t*)&boot_flag;
        uint32_t addr = APP_FLAG_ADDR;
        for (uint32_t i = 0; i < sizeof(BootFlag_t) / 4; i++)
        {
            FLASH_WriteWord_Custom(addr, *src);
            addr += 4;
            src++;
        }
        
        // 锁定Flash
        FLASH_Lock_Custom();
    }
}

/**
 * @brief  Flash解锁
 */
void FLASH_Unlock_Custom(void)
{
    FLASH->KEYR = 0x45670123;
    FLASH->KEYR = 0xCDEF89AB;
}

/**
 * @brief  Flash锁定
 */
void FLASH_Lock_Custom(void)
{
    FLASH->CR |= FLASH_CR_LOCK;
}

/**
 * @brief  Flash页擦除
 */
void FLASH_ErasePage_Custom(uint32_t addr)
{
    while (FLASH->SR & FLASH_SR_BSY);
    FLASH->CR |= FLASH_CR_PER;
    FLASH->AR = addr;
    FLASH->CR |= FLASH_CR_STRT;
    while (FLASH->SR & FLASH_SR_BSY);
    FLASH->CR &= ~FLASH_CR_PER;
}

/**
 * @brief  Flash字写入
 */
void FLASH_WriteWord_Custom(uint32_t addr, uint32_t data)
{
    while (FLASH->SR & FLASH_SR_BSY);
    FLASH->CR |= FLASH_CR_PG;
    
    *(__IO uint16_t*)addr = (uint16_t)data;
    while (FLASH->SR & FLASH_SR_BSY);
    
    *(__IO uint16_t*)(addr + 2) = (uint16_t)(data >> 16);
    while (FLASH->SR & FLASH_SR_BSY);
    
    FLASH->CR &= ~FLASH_CR_PG;
}

/**
 * @brief  显示启动信息
 */
void ShowBootInfo(void)
{
    BootFlag_t *boot_flag = (BootFlag_t*)APP_FLAG_ADDR;
    uint32_t vtor = SCB->VTOR;
    
    UART_SendString("\r\n");
    UART_SendString("========================================\r\n");
    UART_SendString("STM32 OTA Application (方案2)\r\n");
    UART_SendString("========================================\r\n");

    // 显示版本
    UART_SendString("Version: ");
    UART_SendByte('0' + APP_VERSION_MAJOR);
    UART_SendByte('.');
    UART_SendByte('0' + APP_VERSION_MINOR);
    UART_SendByte('.');
    UART_SendByte('0' + APP_VERSION_PATCH);
    UART_SendString("\r\n");

    // 显示运行分区（方案2: 始终是APP1）
    UART_SendString("Running from: APP1 (固定)\r\n");

    if (boot_flag->valid_flag == APP_VALID_FLAG)
    {
        UART_SendString("Boot Count: ");
        // 简单显示启动次数（只显示个位数）
        UART_SendByte('0' + (boot_flag->boot_count % 10));
        UART_SendString("\r\n");
    }
    
    // 显示向量表地址
    UART_SendString("Vector Table: 0x");
    for (int i = 7; i >= 0; i--)
    {
        uint8_t nibble = (vtor >> (i * 4)) & 0xF;
        UART_SendByte((nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10));
    }
    UART_SendString("\r\n");
    
    UART_SendString("========================================\r\n");
    UART_SendString("System Running...\r\n");
}

/**
 * @brief  主函数
 */
int main(void)
{
    uint32_t startup_delay = 0;
    
    // 初始化外设
    LED_Init();
    Debug_UART_Init();
    
    // 启动延迟，确保系统稳定
    for (startup_delay = 0; startup_delay < 1000000; startup_delay++);
    
    // ⭐ 关键：标记启动成功
    // 这会告诉Bootloader当前APP运行正常，防止回滚
    MarkBootSuccess();
    
    // 显示启动信息
    ShowBootInfo();
    
    // 主循环
    while (1)
    {
        // 检查是否收到进入Bootloader命令
        if (Check_BootloaderCommand())
        {
            UART_SendString("\r\n>>> Entering Bootloader... <<<\r\n");
            Delay_ms(100);  // 等待串口发送完成
            NVIC_SystemReset();  // 系统复位，进入Bootloader
        }

        // LED闪烁表示程序运行
        LED1_Turn();
        Delay_ms(2000);

        // 应用代码...
    }
}

