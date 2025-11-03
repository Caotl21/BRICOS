//#include "flash_if.h"
//#include "bootloader.h"

///**
// * @brief  Flash接口初始化
// */
//void FLASH_If_Init(void)
//{
//    FLASH_Unlock();
//}

///**
// * @brief  擦除Flash页
// * @param  page_addr: 页起始地址
// * @retval FLASH_COMPLETE: 成功, 其他: 失败
// */
//uint32_t FLASH_If_ErasePage(uint32_t page_addr)
//{
//    FLASH_Status status = FLASH_COMPLETE;
//    
//    FLASH_Unlock();
//    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
//    
//    status = FLASH_ErasePage(page_addr);
//    
//    FLASH_Lock();
//    
//    return status;
//}

///**
// * @brief  擦除Flash区域
// * @param  start_addr: 起始地址
// * @param  end_addr: 结束地址
// * @retval FLASH_COMPLETE: 成功, 其他: 失败
// */
//uint32_t FLASH_If_Erase(uint32_t start_addr, uint32_t end_addr)
//{
//    uint32_t page_addr;
//    FLASH_Status status = FLASH_COMPLETE;
//    
//    FLASH_Unlock();
//    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
//    
//    // 按页擦除
//    for (page_addr = start_addr; page_addr < end_addr; page_addr += FLASH_PAGE_SIZE)
//    {
//        status = FLASH_ErasePage(page_addr);
//        if (status != FLASH_COMPLETE)
//        {
//            break;
//        }
//    }
//    
//    FLASH_Lock();
//    
//    return status;
//}

///**
// * @brief  写入Flash
// * @param  addr: 写入地址
// * @param  data: 数据指针
// * @param  len: 数据长度(字节)
// * @retval FLASH_COMPLETE: 成功, 其他: 失败
// */
//uint32_t FLASH_If_Write(uint32_t addr, uint32_t *data, uint32_t len)
//{
//    uint32_t i;
//    FLASH_Status status = FLASH_COMPLETE;
//    uint16_t *data16 = (uint16_t*)data;
//    uint32_t count = (len + 1) / 2;  // 转换为半字数量
//    
//    FLASH_Unlock();
//    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
//    
//    // STM32F1按半字(16位)写入
//    for (i = 0; i < count; i++)
//    {
//        status = FLASH_ProgramHalfWord(addr + i * 2, data16[i]);
//        if (status != FLASH_COMPLETE)
//        {
//            break;
//        }
//    }
//    
//    FLASH_Lock();
//    
//    return status;
//}

///**
// * @brief  读取Flash
// * @param  addr: 读取地址
// * @param  data: 数据指针
// * @param  len: 数据长度(字节)
// * @retval FLASH_COMPLETE: 成功
// */
//uint32_t FLASH_If_Read(uint32_t addr, uint32_t *data, uint32_t len)
//{
//    uint32_t i;
//    uint8_t *src = (uint8_t*)addr;
//    uint8_t *dst = (uint8_t*)data;
//    
//    for (i = 0; i < len; i++)
//    {
//        dst[i] = src[i];
//    }
//    
//    return FLASH_COMPLETE;
//}
#include "flash_if.h"
#include "bootloader.h"
#include "ymodem.h"

// 外部函数声明
extern void UART_SendString(const char *str);
extern void UART_SendByte(uint8_t byte);

/**
 * @brief  Flash接口初始化
 */
void FLASH_If_Init(void)
{
    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
}

/**
 * @brief  打印Flash状态
 */
void FLASH_If_PrintStatus(FLASH_Status status)
{
    switch(status)
    {
        case FLASH_BUSY: 
            UART_SendString("FLASH_BUSY");
            break;
        case FLASH_ERROR_PG: 
            UART_SendString("FLASH_ERROR_PG");
            break;
        case FLASH_ERROR_WRP: 
            UART_SendString("FLASH_ERROR_WRP");
            break;
        case FLASH_COMPLETE: 
            UART_SendString("FLASH_COMPLETE");
            break;
        case FLASH_TIMEOUT: 
            UART_SendString("FLASH_TIMEOUT");
            break;
        default:
            UART_SendString("UNKNOWN");
            break;
    }
}

/**
 * @brief  打印Flash错误标志
 */
void FLASH_If_PrintErrorFlags(void)
{
    UART_SendString("Flash Flags: ");
    if(FLASH_GetFlagStatus(FLASH_FLAG_BSY)) UART_SendString("BSY ");
    if(FLASH_GetFlagStatus(FLASH_FLAG_PGERR)) UART_SendString("PGERR ");
    if(FLASH_GetFlagStatus(FLASH_FLAG_WRPRTERR)) UART_SendString("WRPRTERR ");
    if(FLASH_GetFlagStatus(FLASH_FLAG_EOP)) UART_SendString("EOP ");
    if(FLASH_GetFlagStatus(FLASH_FLAG_OPTERR)) UART_SendString("OPTERR ");
    UART_SendString("\r\n");
}

/**
 * @brief  擦除Flash页
 * @param  page_addr: 页起始地址 (必须1KB对齐)
 * @retval FLASH_COMPLETE: 成功, 其他: 失败
 */
uint32_t FLASH_If_ErasePage(uint32_t page_addr)
{
    FLASH_Status status = FLASH_COMPLETE;
    
    UART_SendString("Erasing page at 0x");
    
    if ((page_addr & 0x3FF) != 0) {
        UART_SendString("ERROR: Address not 1KB aligned!\r\n");
        return FLASH_ERROR_PG;
    }
    
    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
    
    status = FLASH_ErasePage(page_addr);
    
    if (status != FLASH_COMPLETE) {
        UART_SendString(" - FAILED: ");
        FLASH_If_PrintStatus(status);
        UART_SendString(" ");
        FLASH_If_PrintErrorFlags();
    } else {
        UART_SendString(" - OK\r\n");
    }
    
    FLASH_Lock();
    
    return status;
}

/**
 * @brief  擦除Flash区域
 * @param  start_addr: 起始地址
 * @param  size: 区域大小
 * @retval FLASH_COMPLETE: 成功, 其他: 失败
 */
uint32_t FLASH_If_Erase(uint32_t start_addr, uint32_t size)
{
    uint32_t page_addr;
    uint32_t pages;
    FLASH_Status status = FLASH_COMPLETE;
    
    // 计算需要擦除的页数
    pages = (size + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE;
    
    UART_SendString("Erasing ");
    // 显示页数
    UART_SendString(" pages from 0x");
    // 显示起始地址
    UART_SendString("\r\n");
    
    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
    
    for (uint32_t i = 0; i < pages; i++)
    {
        page_addr = start_addr + (i * FLASH_PAGE_SIZE);
        
        // 确保地址对齐
        page_addr = page_addr & ~(FLASH_PAGE_SIZE - 1);
        
        status = FLASH_ErasePage(page_addr);
        if (status != FLASH_COMPLETE)
        {
            UART_SendString("Erase failed at page ");
            // 显示页码
            UART_SendString("\r\n");
            break;
        }
    }
    
    FLASH_Lock();
    
    return status;
}

/**
 * @brief  写入Flash
 * @param  addr: 写入地址 (必须半字对齐)
 * @param  data: 数据指针
 * @param  len: 数据长度(字节)
 * @retval FLASH_COMPLETE: 成功, 其他: 失败
 */
uint32_t FLASH_If_Write(uint32_t addr, uint32_t *data, uint32_t len)
{
    uint32_t i;
    FLASH_Status status = FLASH_COMPLETE;
    
    // 检查地址对齐（字对齐）
    if ((addr & 0x3) != 0) {
        UART_SendString("ERROR: Address not word aligned!\r\n");
        return FLASH_ERROR_PG;
    }
    
    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
    
    // 直接按字写入
    for (i = 0; i < len; i++)
    {
        status = FLASH_ProgramWord(addr + i * 4, data[i]);
        if (status != FLASH_COMPLETE) break;
    }
    
    FLASH_Lock();
    
    return status;
}
//uint32_t FLASH_If_Write(uint32_t addr, uint32_t *data, uint32_t len)
//{
//    uint32_t i;
//    FLASH_Status status = FLASH_COMPLETE;
//    uint16_t *data16 = (uint16_t*)data;
//    uint32_t count = (len + 1) / 2;  // 转换为半字数量
//    
//    // 检查地址对齐
//    if ((addr & 0x1) != 0) {
//        UART_SendString("ERROR: Address not half-word aligned!\r\n");
//        return FLASH_ERROR_PG;
//    }
//    
//    FLASH_Unlock();
//    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
//    
//    for (i = 0; i < count; i++)
//    {
//        status = FLASH_ProgramHalfWord(addr + i * 2, data16[i]);
//        if (status != FLASH_COMPLETE)
//        {
//            UART_SendString("Write failed at offset ");
//            // 显示偏移量
//            UART_SendString("\r\n");
//            break;
//        }
//    }
//    
//    FLASH_Lock();
//    
//    return status;
//}

/**
 * @brief  读取Flash
 * @param  addr: 读取地址
 * @param  data: 数据指针
 * @param  len: 数据长度(字节)
 * @retval FLASH_COMPLETE: 成功
 */
uint32_t FLASH_If_Read(uint32_t addr, uint32_t *data, uint32_t len)
{
    uint32_t i;
    uint8_t *src = (uint8_t*)addr;
    uint8_t *dst = (uint8_t*)data;
    
    for (i = 0; i < len; i++)
    {
        dst[i] = src[i];
    }
    
    return FLASH_COMPLETE;
}
