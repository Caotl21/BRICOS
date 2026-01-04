#include "flash.h"
#include <stdio.h>
#include "stm32f4xx_flash.h"

/**
  * @brief  根据地址获取对应的 Flash 扇区编号
  * @param  Address: Flash 地址
  * @retval 扇区编号 (FLASH_Sector_x)
  */
static uint32_t GetSector(uint32_t Address)
{
    if      (Address < ADDR_FLASH_SECTOR_1)  return FLASH_Sector_0;
    else if (Address < ADDR_FLASH_SECTOR_2)  return FLASH_Sector_1;
    else if (Address < ADDR_FLASH_SECTOR_3)  return FLASH_Sector_2;
    else if (Address < ADDR_FLASH_SECTOR_4)  return FLASH_Sector_3;
    else if (Address < ADDR_FLASH_SECTOR_5)  return FLASH_Sector_4;
    else if (Address < ADDR_FLASH_SECTOR_6)  return FLASH_Sector_5;
    else if (Address < ADDR_FLASH_SECTOR_7)  return FLASH_Sector_6;
    else if (Address < ADDR_FLASH_SECTOR_8)  return FLASH_Sector_7;
    else if (Address < ADDR_FLASH_SECTOR_9)  return FLASH_Sector_8;
    else if (Address < ADDR_FLASH_SECTOR_10) return FLASH_Sector_9;
    else if (Address < ADDR_FLASH_SECTOR_11) return FLASH_Sector_10;
    else return FLASH_Sector_11;
}

/**
  * @brief  读取指定地址的一个字 (32位)
  * @param  faddr: 读取地址
  * @retval 读取到的数据
  */
uint32_t STMFLASH_ReadWord(uint32_t faddr)
{
    return *(volatile uint32_t*)faddr;
}

/**
  * @brief  向 Flash 擦除数据 (会自动擦除所在的扇区！)
  * @note   擦除当前数据所在的扇区的内容
  * @param  WriteAddr: 起始地址 (必须是4的倍数)
  */
void STMFLASH_EraseSector(uint32_t WriteAddr)
{
    FLASH_Status status = FLASH_COMPLETE;
   
    uint32_t addr = 0;
    uint32_t sector_num = 0;

    // 1. 解锁 Flash 控制寄存器
    FLASH_Unlock();
    
    // 2. 清除所有挂起的标志位 (防止之前操作遗留的错误)
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR | 
                    FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    // 3. 检查地址范围内是否需要擦除
    // Flash 只能把 1 写成 0，所以如果目标地址不是 0xFFFFFFFF，就必须擦除
    addr = WriteAddr;
    
	// 发现有非空数据，需要擦除！
	// 获取该地址属于哪个扇区
	sector_num = GetSector(addr);
	
	// 【危险操作】擦除扇区
	// VoltageRange_3 代表 2.7V~3.6V 供电，允许 32位并行操作
	status = FLASH_EraseSector(sector_num, VoltageRange_3);
	
	if(status != FLASH_COMPLETE)
	{
		// 擦除失败，比如电压不稳或写保护
		printf("Error: Flash Erase failed at Sector %d\r\n", sector_num);
		FLASH_Lock();
		return;
	}
	else
	{
		printf("Success: Flash Erase at Sector %d\r\n", sector_num);
	}

}

/**
  * @brief  向 Flash 擦写数据 (会自动擦除所在的扇区！)
  * @note   F4 的扇区很大，这个函数为了演示，如果检测到没擦除，会擦除整个扇区。
  * 实际使用时请务必小心数据覆盖。
  * @param  WriteAddr: 起始地址 (必须是4的倍数)
  * @param  pBuffer: 数据指针
  * @param  NumToWrite: 字(32位)的数量
  */
void STMFLASH_Write(uint32_t WriteAddr, uint32_t *pBuffer, uint32_t NumToWrite)
{
    FLASH_Status status = FLASH_COMPLETE;
    uint32_t endaddr = WriteAddr + (NumToWrite * 4); // 结束地址
    uint32_t addr = 0;
    uint32_t sector_num = 0;

    // 1. 解锁 Flash 控制寄存器
    FLASH_Unlock();
    
    // 2. 清除所有挂起的标志位 (防止之前操作遗留的错误)
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR | 
                    FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    // 3. 检查地址范围内是否需要擦除
    // Flash 只能把 1 写成 0，所以如果目标地址不是 0xFFFFFFFF，就必须擦除
    addr = WriteAddr;
    while(addr < endaddr)
    {
        if(STMFLASH_ReadWord(addr) != 0xFFFFFFFF)
        {
            // 发现有非空数据，需要擦除！
            // 获取该地址属于哪个扇区
            sector_num = GetSector(addr);
            
            // 【危险操作】擦除扇区
            // VoltageRange_3 代表 2.7V~3.6V 供电，允许 32位并行操作
            status = FLASH_EraseSector(sector_num, VoltageRange_3);
            
            if(status != FLASH_COMPLETE)
            {
                // 擦除失败，比如电压不稳或写保护
                printf("Error: Flash Erase failed at Sector %d\r\n", sector_num);
                FLASH_Lock();
                return;
            }
            
            // F4 的扇区很大，如果跨扇区写入，这里逻辑会复杂。
            // 简单起见，这里假设用户写入的数据不会跨越未擦除的边界太多。
            // 实际上 Flash_EraseSector 会擦除整个扇区，所以该扇区内所有数据都变 FF 了
        }
        addr += 4;
    }

    // 4. 开始写入
    addr = WriteAddr;
    while (addr < endaddr)
    {
        // 写入一个字 (32-bit)
        if (FLASH_ProgramWord(addr, *pBuffer) == FLASH_COMPLETE)
        {
            addr += 4;
            pBuffer++;
        }
        else
        {
            printf("Error: Write failed at addr 0x%X\r\n", addr);
            break;
        }
    }

    // 5. 上锁
    FLASH_Lock();
}

/**
  * @brief  采用追加写入法--读取最新的配置值
  * @retval 返回最新的数据。如果没找到（扇区是空的），返回默认值 0
  */
uint32_t Config_Read(void)
{
    uint32_t addr = SECTOR_ADDR_START;

    // 1. 如果第一个地址就是空的，说明还没存过数据
    if (STMFLASH_ReadWord(addr) == EMPTY_VAL)
    {
        return 0; // 返回默认值
    }

    // 2. 遍历扇区，寻找数据的“尾巴”
    // 为了防止死循环，必须判断 addr < SECTOR_ADDR_END
    while (addr < SECTOR_ADDR_END)
    {
		Flash_Unit_t *pUnit = (Flash_Unit_t*)addr;
		
		// 只检查 Status，不检查 Data
        if (pUnit->status == 0xFFFFFFFF) 
        {
            // 遇到状态为空，说明后面没数据了
            // 返回上一个有效数据
            if (addr > SECTOR_ADDR_START) {
                return ((Flash_Unit_t*)(addr - 8))->data;
            } else {
                return 0; // 还没存过任何数据
            }
        }
        
        
        addr += 8;
    }

    // 3. 特殊情况：整个扇区写满了，最后一个肯定是最新的
    return ((Flash_Unit_t*)(SECTOR_ADDR_END - 8))->data;
}

/**
  * @brief  追加写入新的配置值：重点是判断是否写满
  * @param  val: 要写入的新数值
  */
void Config_AppendWrite(uint32_t val)
{
    uint32_t addr = SECTOR_ADDR_START;
    FLASH_Status status = FLASH_COMPLETE;

    // 1. 寻找第一个空闲位置 (找 0xFFFFFFFF)
    while (addr < SECTOR_ADDR_END)
    {
        if (STMFLASH_ReadWord(addr) == EMPTY_VAL)
        {
            break; // 找到了空地，跳出循环，addr 此时指向空地
        }
        addr += 8;
    }

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR | 
                    FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    // 2. 判断是否写满 (垃圾回收机制)
    // 如果 addr 到了末尾，说明没空地了
    if (addr >= SECTOR_ADDR_END)
    {
        printf("Sector Full! Erasing...\r\n");
        
        // 扇区满了！必须擦除
        // 注意：这里我们简单粗暴地擦除了。
        // 在实际产品中，应该先用 RAM 暂存最新的 val，擦除后再写进去。
        
        status = FLASH_EraseSector(FLASH_Sector_11, VoltageRange_3);
        if(status != FLASH_COMPLETE)
        {
            printf("Erase Error!\r\n");
            FLASH_Lock();
            return;
        }
        
        // 擦除后，指针回到起点
        addr = SECTOR_ADDR_START;
    }

    // 3. 在找到的空地上写入新值
	// 写入数据(Data)
    status = FLASH_ProgramWord(addr, val);
    
    if (status == FLASH_COMPLETE)
    {
        printf("Append Data Success at 0x%X, Val: %X\r\n", addr, val);
		// 写入标记 (Status) -> 标记为“已占用”
		status = FLASH_ProgramWord(addr+4, VALID_STATUS);
		if (status == FLASH_COMPLETE)
		{
			printf("Append Status Success at 0x%X\r\n", addr);
		}
		else
		{
			printf("Write Status Error!\r\n");
		}
    }
    else
    {
        printf("Write Data Error!\r\n");
    }

    FLASH_Lock();
}
