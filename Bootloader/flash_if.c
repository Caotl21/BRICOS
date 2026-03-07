#include "flash_if.h"

/* 外部函数（调试输出） */
extern void UART_SendString(const char *str);
extern void UART_SendByte(uint8_t byte);

/**
 * @brief  初始化Flash接口（解锁）
 */
void FLASH_If_Init(void)
{
    FLASH_Unlock();

    /* 清除所有Flash错误标志 */
    FLASH_ClearFlag(FLASH_FLAG_EOP    | FLASH_FLAG_OPERR  | FLASH_FLAG_WRPERR |
                    FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
}

/**
 * @brief  反初始化Flash接口（上锁）
 */
void FLASH_If_DeInit(void)
{
    FLASH_Lock();
}

/**
 * @brief  擦除指定地址范围覆盖的所有扇区
 * @param  start_addr: 起始地址
 * @param  size:       需要擦除的大小 (字节)
 * @retval FLASH_COMPLETE: 成功, 其他: 失败
 *
 * @note   F407 Flash 扇区大小不均匀 (16K/64K/128K)
 *         此函数会擦除地址范围覆盖到的 所有完整扇区
 */
uint32_t FLASH_If_Erase(uint32_t start_addr, uint32_t size)
{
    FLASH_Status status = FLASH_COMPLETE;
    uint32_t end_addr;
    uint32_t sector_id;
    uint32_t sector_size;
    uint32_t current_addr;

    if (size == 0)
        return FLASH_COMPLETE;

    end_addr = start_addr + size - 1;

    /* 确保地址在Flash范围内 */
    if (start_addr < 0x08000000 || end_addr > 0x080FFFFF)
        return FLASH_ERROR_PROGRAM;

    FLASH_If_Init();

    /*
     * 遍历扇区：从 start_addr 所在扇区开始，
     * 逐扇区擦除，直到覆盖 end_addr
     */
    current_addr = start_addr;

    while (current_addr <= end_addr)
    {
        sector_id = Flash_GetSectorFromAddr(current_addr);
        sector_size = Flash_GetSectorSize(current_addr);

        if (sector_id == 0xFFFFFFFF || sector_size == 0)
        {
            FLASH_If_DeInit();
            return FLASH_ERROR_PROGRAM;
        }

        /* 擦除该扇区 */
        status = FLASH_EraseSector(sector_id, VoltageRange_3);
        if (status != FLASH_COMPLETE)
        {
            FLASH_If_DeInit();
            return status;
        }

        /* 移动到下个扇区的起始地址 */
        /* 先找到当前扇区的起始地址，再加扇区大小 */
        {
            uint32_t i;
            for (i = 0; i < FLASH_SECTOR_COUNT; i++)
            {
                if (flash_sectors[i].sector_id == sector_id)
                {
                    current_addr = flash_sectors[i].start_addr + flash_sectors[i].size;
                    break;
                }
            }
        }
    }

    FLASH_If_DeInit();
    return FLASH_COMPLETE;
}

/**
 * @brief  擦除单个扇区（通过地址定位）
 * @param  addr: 该扇区内的任意地址
 * @retval FLASH_COMPLETE: 成功, 其他: 失败
 */
uint32_t FLASH_If_EraseSector(uint32_t addr)
{
    FLASH_Status status;
    uint32_t sector_id;

    sector_id = Flash_GetSectorFromAddr(addr);
    if (sector_id == 0xFFFFFFFF)
        return FLASH_ERROR_PROGRAM;

    FLASH_If_Init();

    status = FLASH_EraseSector(sector_id, VoltageRange_3);

    FLASH_If_DeInit();
    return status;
}

/**
 * @brief  写入数据到Flash
 * @param  addr: 目标地址（必须4字节对齐）
 * @param  data: 源数据指针
 * @param  len:  数据长度（字节）
 * @retval FLASH_COMPLETE: 成功, 其他: 失败
 *
 * @note   F103 使用半字编程(16bit)，F407 使用字编程(32bit)
 *         写入前必须先擦除目标区域
 */
uint32_t FLASH_If_Write(uint32_t addr, uint32_t *data, uint32_t len)
{
    FLASH_Status status = FLASH_COMPLETE;
    uint32_t i;
    uint32_t word_count;
    uint32_t remaining;

    /* 地址必须4字节对齐 */
    if ((addr % 4) != 0)
        return FLASH_ERROR_PROGRAM;

    if (len == 0)
        return FLASH_COMPLETE;

    /* 计算完整的字数 */
    word_count = len / 4;
    remaining  = len % 4;

    FLASH_If_Init();

    /* 按字(32bit)写入 */
    for (i = 0; i < word_count; i++)
    {
        status = FLASH_ProgramWord(addr + (i * 4), data[i]);
        if (status != FLASH_COMPLETE)
        {
            FLASH_If_DeInit();
            return status;
        }

        /* 写入验证 */
        if (*(uint32_t *)(addr + (i * 4)) != data[i])
        {
            FLASH_If_DeInit();
            return FLASH_ERROR_PROGRAM;
        }
    }

    /* 处理不足4字节的尾部数据 */
    if (remaining > 0)
    {
        /*
         * 将剩余字节补0xFF组成一个完整的字
         * Flash擦除后默认0xFF，补0xFF不影响未使用位
         */
        uint32_t last_word = 0xFFFFFFFF;
        uint8_t *p = (uint8_t *)&last_word;
        uint8_t *src = (uint8_t *)&data[word_count];

        for (i = 0; i < remaining; i++)
        {
            p[i] = src[i];
        }

        status = FLASH_ProgramWord(addr + (word_count * 4), last_word);
        if (status != FLASH_COMPLETE)
        {
            FLASH_If_DeInit();
            return status;
        }
    }

    FLASH_If_DeInit();
    return FLASH_COMPLETE;
}

/**
 * @brief  从Flash读取数据
 * @param  addr: 源地址
 * @param  data: 目标缓冲区
 * @param  len:  读取长度（字节）
 * @retval 实际读取的字节数
 *
 * @note   Flash可以直接内存映射读取，此函数封装统一接口
 */
uint32_t FLASH_If_Read(uint32_t addr, uint32_t *data, uint32_t len)
{
    uint32_t i;
    uint32_t word_count;
    uint32_t remaining;

    if (len == 0)
        return 0;

    /* 按字读取 */
    word_count = len / 4;
    remaining  = len % 4;

    for (i = 0; i < word_count; i++)
    {
        data[i] = *(uint32_t *)(addr + (i * 4));
    }

    /* 处理尾部不足4字节 */
    if (remaining > 0)
    {
        uint32_t last_word = *(uint32_t *)(addr + (word_count * 4));
        uint8_t *dst = (uint8_t *)&data[word_count];
        uint8_t *src = (uint8_t *)&last_word;

        for (i = 0; i < remaining; i++)
        {
            dst[i] = src[i];
        }
    }

    return len;
}

