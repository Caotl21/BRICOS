#include "stm32f10x.h"
#include "bootloader.h"
#include "ymodem.h"
#include "flash_if.h"
#include <string.h>

#define BOOTLOADER_TIMEOUT      3000  // Bootloader超时时间(ms)
#define DOWNLOAD_BUFFER_SIZE    (2 * 1024)  // 2KB缓冲区（Ymodem每次最大1KB数据包）

/**
 * @brief  简单延时函数
 */
static void Delay_Ms(uint32_t ms)
{
    uint32_t i, j;
    for (i = 0; i < ms; i++)
    {
        for (j = 0; j < 8000; j++)  // 假设72MHz系统时钟
        {
            __NOP();
        }
    }
}

/**
 * @brief  检查是否需要进入Bootloader模式
 * @retval 1: 进入Bootloader, 0: 跳转到APP
 */
static uint8_t Check_BootloaderMode(void)
{
    uint32_t timeout = 3000;  // 3s超时
    
    // 检查UART是否收到特定字符 'B' (0x42)
    while (timeout--)
    {
        if (USART_GetFlagStatus(USART1, USART_FLAG_RXNE) == SET)
        {
            uint8_t data = USART_ReceiveData(USART1);
            if (data == 'B')  // 收到'B'进入Bootloader模式
            {
                return 1;
            }
        }
        Delay_Ms(1);
    }
    
    return 0;
}

/**
 * @brief  UART初始化 - 简化版本（只使用USART1）
 */
static void UART_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;

    // 使能时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB1Periph_USART2, ENABLE);

    // 配置TX引脚 PA9
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // 配置RX引脚 PA10
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // 配置TX引脚 PA2
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // 配置RX引脚 PA3
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // 配置USART1
    USART_InitStructure.USART_BaudRate = 115200;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &USART_InitStructure);
    USART_Init(USART2, &USART_InitStructure);

    // 使能USART1
    USART_Cmd(USART1, ENABLE);
    USART_Cmd(USART2, ENABLE);
}

/**
 * @brief  SysTick初始化(1ms中断)
 */
static void SysTick_Init(void)
{
    // 配置SysTick为1ms中断
    SysTick_Config(SystemCoreClock / 1000);
}

/**
 * @brief  发送字符串
 */
void UART_SendString(const char *str)
{
    while (*str)
    {
        // 等待发送缓冲区空
        while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);
        // 发送数据
        USART_SendData(USART2, *str++);
        // 等待发送完成
        while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET);
    }
}

/**
 * @brief  发送单个字节
 */
void UART_SendByte(uint8_t byte)
{
    // 等待发送缓冲区空
    while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);
    // 发送数据
    USART_SendData(USART2, byte);
    // 等待发送完成
    while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET);
}

/**
 * @brief  发送整数到串口（支持负数）
 */
void UART_SendInt(int32_t value)
{
    char buffer[12];  // 足够存储32位整数
    int i = 0;
    
    if (value == 0) {
        UART_SendByte('0');
        return;
    }
    
    if (value < 0) {
        UART_SendByte('-');
        value = -value;
    }
    
    // 转换为字符串（逆序）
    while (value > 0) {
        buffer[i++] = '0' + (value % 10);
        value /= 10;
    }
    
    // 逆序输出
    while (i > 0) {
        UART_SendByte(buffer[--i]);
    }
}

///**
// * @brief  处理Ymodem接收并烧录 - 方案2: 边接收边写Flash
// * @param  app_num: 应用编号 (方案2中始终是2)
// * @retval 0:成功, <0:失败
// */
//static int32_t Process_YmodemDownload(uint8_t app_num)
//{
//    uint8_t packet_buffer[DOWNLOAD_BUFFER_SIZE];  // 2KB包缓冲区
//    uint8_t packet_data[PACKET_1K_SIZE + PACKET_OVERHEAD];
//    FileInfo_t file_info;
//    int32_t packet_length;
//    uint32_t app_addr;
//    uint32_t app_size;
//    uint32_t errors = 0;
//    uint32_t packets_received = 0;
//    uint32_t file_size = 0;
//    uint32_t bytes_written = 0;
//    uint32_t buffer_index = 0;
//    int32_t result;

//    // 方案2: 始终上传到APP2
//    if (app_num == 2)
//    {
//        app_addr = APP2_ADDR;
//        app_size = APP2_SIZE;
//    }
//    else
//    {
//        UART_SendString("Error: Only APP2 is supported in Solution 2!\r\n");
//        return -1;
//    }

//    // 显示OTA信息
//    UART_SendString("\r\n========== OTA Update (Solution 2) ==========\r\n");
//    UART_SendString("Target: APP2 (Cache)\r\n");
//    UART_SendString("After reboot: APP2 -> APP1 -> Run\r\n");
//    UART_SendString("=============================================\r\n");

//    // 标记OTA开始
//    Bootloader_StartOTA();

//    memset(&file_info, 0, sizeof(file_info));
//    memset(packet_buffer, 0, sizeof(packet_buffer));

//    UART_SendString("\r\nWaiting for file (Ymodem)...\r\n");

//    // 发送'C'表示准备接收(使用CRC16)
//    for (uint32_t i = 0; i < 3; i++)
//    {
//        Ymodem_SendByte(CRC16);
//        Delay_Ms(100);
//    }

//    // 擦除APP区域（先擦除整个区域）
//    UART_SendString("Erasing...\r\n");
//    if (FLASH_If_Erase(app_addr, app_size) != FLASH_COMPLETE)
//    {
//        UART_SendString("Erase failed!\r\n");
//        Bootloader_FinishOTA(0);
//        return -1;
//    }

//    // 接收循环
//    while (1)
//    {
//        result = Ymodem_ReceivePacket(packet_data, &packet_length, YMODEM_PACKET_TIMEOUT);

//        if (result == YMODEM_OK)
//        {
//            errors = 0;

//            if (packet_length == 0)
//            {
//                // 收到EOT
//                Ymodem_SendByte(NAK);
//                result = Ymodem_ReceivePacket(packet_data, &packet_length, YMODEM_PACKET_TIMEOUT);
//                if (result == YMODEM_OK && packet_length == 0)
//                {
//                    Ymodem_SendByte(ACK);

//                    // 写入剩余数据
//                    if (buffer_index > 0)
//                    {
//                        if (FLASH_If_Write(app_addr + bytes_written, (uint32_t *)packet_buffer, buffer_index) != FLASH_COMPLETE)
//                        {
//                            UART_SendString("Write failed!\r\n");
//                            Bootloader_FinishOTA(0);
//                            return -1;
//                        }
//                        bytes_written += buffer_index;
//                    }

//                    break;  // 传输完成
//                }
//            }
//            else if (packet_length > 0)
//            {
//                if (packets_received == 0)
//                {
//                    // 第一个包，包含文件信息
//                    if (Ymodem_ParseFileInfo(packet_data, &file_info) == YMODEM_OK)
//                    {
//                        file_size = file_info.file_size;

//                        if (file_size > app_size)
//                        {
//                            UART_SendString("File too large!\r\n");
//                            Ymodem_SendByte(CA);
//                            Ymodem_SendByte(CA);
//                            Bootloader_FinishOTA(0);
//                            return -1;
//                        }

//                        UART_SendString("File: ");
//                        UART_SendString((char *)file_info.file_name);
//                        UART_SendString("\r\nWriting...\r\n");

//                        Ymodem_SendByte(ACK);
//                        Ymodem_SendByte(CRC16);
//                    }
//                    else
//                    {
//                        // 空包，会话结束
//                        Ymodem_SendByte(ACK);
//                        break;
//                    }
//                }
//                else
//                {
//                    // 数据包 - 边接收边写Flash
//                    uint32_t bytes_to_copy = packet_length;

//                    if (file_size > 0 && bytes_written + buffer_index + bytes_to_copy > file_size)
//                    {
//                        bytes_to_copy = file_size - bytes_written - buffer_index;
//                    }

//                    // 复制到缓冲区
//                    for (uint32_t i = 0; i < bytes_to_copy; i++)
//                    {
//                        packet_buffer[buffer_index++] = packet_data[i];

//                        // 缓冲区满了，写入Flash
//                        if (buffer_index >= DOWNLOAD_BUFFER_SIZE)
//                        {
//                            if (FLASH_If_Write(app_addr + bytes_written, (uint32_t *)packet_buffer, buffer_index) != 0)
//                            {
//                                UART_SendString("Write failed!\r\n");
//                                Bootloader_FinishOTA(0);
//                                return -1;
//                            }
//                            bytes_written += buffer_index;
//                            buffer_index = 0;
//                        }
//                    }

//                    Ymodem_SendByte(ACK);
//                }

//                packets_received++;
//            }
//        }
//        else if (result == YMODEM_ABORT)
//        {
//            Ymodem_SendByte(ACK);
//            UART_SendString("Transfer aborted!\r\n");
//            Bootloader_FinishOTA(0);
//            return -1;
//        }
//        else
//        {
//            errors++;
//            if (errors > MAX_ERRORS)
//            {
//                UART_SendString("Too many errors!\r\n");
//                Ymodem_SendByte(CA);
//                Ymodem_SendByte(CA);
//                Bootloader_FinishOTA(0);
//                return -1;
//            }
//        }
//    }

//    // OTA成功
//    UART_SendString("OTA Success!\r\n");
//    UART_SendString("Bytes written: ");
//    // 简单显示字节数（这里省略详细显示）
//    UART_SendString("\r\n");

//    Bootloader_FinishOTA(1);

//    UART_SendString("\r\n========== OTA Complete ==========\r\n");
//    UART_SendString("Reboot to apply update\r\n");
//    UART_SendString("==================================\r\n");

//    return 0;
//}

/**
 * @brief  处理Ymodem接收并烧录 - 优化版本
 * @param  app_num: 应用编号 (方案2中始终是2)
 * @retval 0:成功, <0:失败
 */
static int32_t Process_YmodemDownload(uint8_t app_num)
{
	//UART_SendString("\r\n[DEBUG] Process_YmodemDownLoad Started\r\n");
	
    uint8_t packet_data[PACKET_1K_SIZE + PACKET_OVERHEAD];
    FileInfo_t file_info;
    int32_t packet_length;
    uint32_t app_addr;
    uint32_t app_size;
    uint32_t errors = 0;
    uint32_t packets_received = 0;
    uint32_t file_size = 0;
    uint32_t bytes_written = 0;
    int32_t result;

    // 方案2: 始终上传到APP2
    if (app_num == 2)
    {
        app_addr = APP2_ADDR;
        app_size = APP2_SIZE;
		//UART_SendString("[DEBUG] APP2 Address: 0x08009C00\r\n");
        //UART_SendString("[DEBUG] APP2 Size: 23KB\r\n");
    }
    else
    {
        UART_SendString("Error: Only APP2 is supported!\r\n");
        return -1;
    }

    // 显示OTA信息
//    UART_SendString("\r\n========== OTA Update ==========\r\n");
//    UART_SendString("Target: APP2 (Cache)\r\n");
//    UART_SendString("After reboot: APP2 -> APP1 -> Run\r\n");
//    UART_SendString("==================================\r\n");

    // 标记OTA开始
    Bootloader_StartOTA();

    memset(&file_info, 0, sizeof(file_info));

    //UART_SendString("\r\nWaiting for file (Ymodem)...\r\n");

    // 发送'C'表示准备接收(使用CRC16)
    for (uint32_t i = 0; i < 3; i++)
    {
        Ymodem_SendByte(CRC16);
        Delay_Ms(100);
    }

    // 擦除APP区域
    UART_SendString("Erasing APP2...\r\n");
    uint32_t pages_to_erase = (app_size + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE;
    
    for (uint32_t i = 0; i < pages_to_erase; i++)
    {
        uint32_t page_addr = app_addr + (i * FLASH_PAGE_SIZE);
        if (FLASH_If_ErasePage(page_addr) != FLASH_COMPLETE)
        {
            UART_SendString("Erase failed at page: 0x");
            UART_SendString("\r\n");
            Bootloader_FinishOTA(0);
            return -1;
        }
        
        // 进度显示
        if ((i % 4) == 0) UART_SendString(".");
    }
    UART_SendString(" OK\r\n");

    // 接收循环
    while (1)
    {
        result = Ymodem_ReceivePacket(packet_data, &packet_length, YMODEM_PACKET_TIMEOUT);

         // 改进的调试输出
        UART_SendString("[DEBUG] Receive Packet: ");
        UART_SendInt(result);  // 使用新函数显示负数
        UART_SendString(", Length: ");
        UART_SendInt(packet_length);
        UART_SendString("\r\n");


        if (result == YMODEM_OK)
        {
            errors = 0;

            if (packet_length == 0)
            {
                // 收到EOT
                Ymodem_SendByte(NAK);
                result = Ymodem_ReceivePacket(packet_data, &packet_length, YMODEM_PACKET_TIMEOUT);
                if (result == YMODEM_OK && packet_length == 0)
                {
                    Ymodem_SendByte(ACK);
                    break;  // 传输完成
                }
            }
            else if (packet_length > 0)
            {
                if (packets_received == 0)
                {
                    // 第一个包，包含文件信息
                    if (Ymodem_ParseFileInfo(packet_data, &file_info) == YMODEM_OK)
                    {
                        file_size = file_info.file_size;

                        if (file_size > app_size)
                        {
                            UART_SendString("File too large! ");
							UART_SendByte('0' + (file_size / 1024));
                            UART_SendString("KB > ");
							UART_SendByte('0' + (app_size / 1024));
                            UART_SendString("KB\r\n");
                            Ymodem_SendByte(CA);
                            Ymodem_SendByte(CA);
                            Bootloader_FinishOTA(0);
                            return -1;
                        }

                        //UART_SendString("File: ");
                        //UART_SendString((char *)file_info.file_name);
                        //UART_SendString(" (");
						//UART_SendByte('0' + (file_size / 1024));
                        //UART_SendString("KB)\r\nWriting...\r\n");

                        Ymodem_SendByte(ACK);
                        Ymodem_SendByte(CRC16);
                    }
                    else
                    {
                        // 空包，会话结束
                        Ymodem_SendByte(ACK);
                        break;
                    }
                }
                else
                {
                    // 数据包 - 直接写入Flash
                    uint32_t bytes_to_write = packet_length;
                    
                    // 检查文件大小限制
                    if (file_size > 0 && bytes_written + bytes_to_write > file_size)
                    {
                        bytes_to_write = file_size - bytes_written;
                    }
                    
                    // 检查Flash空间
                    if (bytes_written + bytes_to_write > app_size)
                    {
                        UART_SendString("Flash space exceeded!\r\n");
                        Ymodem_SendByte(CA);
                        Ymodem_SendByte(CA);
                        Bootloader_FinishOTA(0);
                        return -1;
                    }

                    // 写入Flash - 修复：按字对齐处理
                    uint32_t words_to_write = (bytes_to_write + 3) / 4; // 字节转字数(向上取整)
                    uint32_t write_addr = app_addr + bytes_written;
                    
                    // 确保地址字对齐
                    if ((write_addr & 0x3) != 0) {
                        UART_SendString("Address not aligned: 0x");
						UART_SendByte('0' + write_addr);
                        UART_SendString("\r\n");
                        Bootloader_FinishOTA(0);
                        return -1;
                    }
                    
                    if (FLASH_If_Write(write_addr, (uint32_t *)packet_data, words_to_write) != FLASH_COMPLETE)
                    {
                        UART_SendString("Write failed at: 0x");
                        UART_SendByte('0' + write_addr);
                        UART_SendString("\r\n");
                        Bootloader_FinishOTA(0);
                        return -1;
                    }
                    
                    bytes_written += bytes_to_write;
                    
                    // 进度显示
                    if ((packets_received % 16) == 0) {
                        UART_SendString(".");
                    }
                    
                    Ymodem_SendByte(ACK);
                }

                packets_received++;
            }
        }
        else if (result == YMODEM_ABORT)
        {
            Ymodem_SendByte(ACK);
            UART_SendString("Transfer aborted!\r\n");
            Bootloader_FinishOTA(0);
            return -1;
        }
        else
        {
            errors++;
            UART_SendString("Packet error: ");
			UART_SendByte('0' + errors);
            UART_SendString("\r\n");
            
            if (errors > MAX_ERRORS)
            {
                UART_SendString("Too many errors!\r\n");
                Ymodem_SendByte(CA);
                Ymodem_SendByte(CA);
                Bootloader_FinishOTA(0);
                return -1;
            }
        }
    }

    // OTA成功
    UART_SendString("\r\nOTA Success!\r\n");
    UART_SendString("Bytes written: ");
	UART_SendByte('0' + bytes_written);
    UART_SendString(" (");
	UART_SendByte('0' + (bytes_written / 1024));
    UART_SendString("KB)\r\n");

    Bootloader_FinishOTA(1);

    UART_SendString("\r\n========== OTA Complete ==========\r\n");
    UART_SendString("Reboot to apply update\r\n");
    UART_SendString("==================================\r\n");

    return 0;
}

/**
 * @brief  主函数
 */
int main(void)
{
    uint8_t bootloader_mode = 0;
    uint8_t cmd;

    // 系统初始化
    SystemInit();

    // 初始化SysTick
    SysTick_Init();

    // 初始化UART
    UART_Init();

    // 初始化Flash接口
    FLASH_If_Init();
	UART_SendString("hello world!");

    // 检查是否需要进入Bootloader模式
    bootloader_mode = Check_BootloaderMode();

    if (!bootloader_mode)
    {
        // 尝试跳转到应用程序
        Bootloader_Run();

        // 如果跳转失败，进入Bootloader模式
        bootloader_mode = 1;
    }

    // Bootloader模式
    if (bootloader_mode)
    {
        uint32_t app1_ver = Bootloader_GetAppVersion(1);
        uint32_t app2_ver = Bootloader_GetAppVersion(2);

        // 发送Bootloader就绪信号
        UART_SendString("\r\n");
        UART_SendString("========================================\r\n");
        UART_SendString("STM32 Bootloader v2.0 (Solution 2)\r\n");
        UART_SendString("========================================\r\n");
        UART_SendString("Flash Layout:\r\n");
        UART_SendString("  Bootloader: 0x08000000 (16KB)\r\n");
        UART_SendString("  APP1:       0x08004000 (23KB) [Run]\r\n");
        UART_SendString("  APP2:       0x08009C00 (23KB) [Cache]\r\n");
        UART_SendString("----------------------------------------\r\n");
        UART_SendString("APP1 Version: ");
        if (app1_ver > 0) {
            // 简单显示版本号
            UART_SendByte('0' + (app1_ver / 100) % 10);
            UART_SendByte('.');
            UART_SendByte('0' + (app1_ver / 10) % 10);
            UART_SendByte('.');
            UART_SendByte('0' + app1_ver % 10);
        } else {
            UART_SendString("N/A");
        }
        UART_SendString("\r\nAPP2 Version: ");
        if (app2_ver > 0) {
            UART_SendByte('0' + (app2_ver / 100) % 10);
            UART_SendByte('.');
            UART_SendByte('0' + (app2_ver / 10) % 10);
            UART_SendByte('.');
            UART_SendByte('0' + app2_ver % 10);
        } else {
            UART_SendString("N/A");
        }
        UART_SendString("\r\n");
        UART_SendString("========================================\r\n");
        UART_SendString("Commands:\r\n");
        UART_SendString("  2 - Download to APP2 (OTA Cache)\r\n");
		UART_SendString("  C - Download to APP2 (Mannual Test)\r\n");
        UART_SendString("  J - Jump to APP1\r\n");
		UART_SendString("  O - OTA Simulation Test\r\n");
        UART_SendString("  I - Show Info\r\n");
        UART_SendString("========================================\r\n");
        UART_SendString("READY\r\n");

        // 主循环
        while (1)
        {
            // 等待命令
            if (USART_GetFlagStatus(USART1, USART_FLAG_RXNE) == SET)
            {
                cmd = USART_ReceiveData(USART1);

                switch (cmd)
                {
                    case '2':
                        Process_YmodemDownload(2);
                        UART_SendString("READY\r\n");
                        break;
					
					case 'C':  // 手动复制APP2到APP1
					case 'c':
						UART_SendString("\r\n>>> Manual Copy: APP2 -> APP1 <<<\r\n");
						Bootloader_RecoverFromAPP2();
						UART_SendString("READY\r\n");
						break;

                    case 'J':
                    case 'j':
                        UART_SendString("\r\n>>> Jumping to APP1 <<<\r\n");
                        Delay_Ms(100);
                        Bootloader_Run();
                        UART_SendString("Jump failed!\r\n");
                        break;

                    case 'I':
                    case 'i':
                        {
                            uint8_t attempts = Bootloader_GetBootAttempts();
                            UART_SendString("\r\n========== System Info ==========\r\n");
                            UART_SendString("Running: APP1 (Fixed)\r\n");
                            UART_SendString("Boot Attempts: ");
                            UART_SendByte('0' + attempts);
                            UART_SendString("\r\nAPP1 Valid: ");
                            UART_SendString(Bootloader_CheckApp(APP1_ADDR) ? "Yes" : "No");
                            UART_SendString("\r\nAPP2 Valid: ");
                            UART_SendString(Bootloader_CheckApp(APP2_ADDR) ? "Yes" : "No");
                            UART_SendString("\r\n================================\r\n");
                        }
                        break;
						
					case 'O':  // 模拟OTA完成，设置need_copy标志
					case 'o':
						{
							UART_SendString("\r\n>>> Simulating OTA Completion <<<\r\n");
							Bootloader_FinishOTA(1);  // 设置need_copy=1
							UART_SendString("OTA flags set. Reboot to apply update.\r\n");
							UART_SendString("READY\r\n");
						}
						break;

                    default:
                        break;
                }
            }
        }
    }

    return 0;
}

/**
 * @brief  SysTick中断处理函数
 */
void SysTick_Handler(void)
{
    Ymodem_IncTick();
}


