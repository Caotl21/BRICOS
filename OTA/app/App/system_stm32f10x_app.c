/**
  * APP版本的system_stm32f10x.c
  * 添加了向量表重定位功能
  * 使用时需要定义 VECT_TAB_OFFSET 为应用程序的偏移地址
  * APP1: #define VECT_TAB_OFFSET  0x4000
  * APP2: #define VECT_TAB_OFFSET  0xA000
  */

#include "stm32f10x.h"

/* 向量表偏移地址 - 在编译选项中定义 */
#ifndef VECT_TAB_OFFSET
#define VECT_TAB_OFFSET  0x4000  /* 默认APP1地址 */
#endif

/* 系统时钟频率 */
uint32_t SystemCoreClock = 72000000;

/* 私有函数声明 */
static void SetSysClock(void);
static void SetSysClockTo72(void);

/**
  * @brief  系统初始化函数
  *         配置系统时钟和向量表
  */
void SystemInit (void)
{
    /* 复位RCC时钟配置到默认状态 */
    RCC->CR |= (uint32_t)0x00000001;
    RCC->CFGR &= (uint32_t)0xF8FF0000;
    RCC->CR &= (uint32_t)0xFEF6FFFF;
    RCC->CR &= (uint32_t)0xFFFBFFFF;
    RCC->CFGR &= (uint32_t)0xFF80FFFF;
    RCC->CIR = 0x009F0000;
    
    /* 配置系统时钟 */
    SetSysClock();
    
    /* 重定位向量表 - 关键步骤! */
    SCB->VTOR = FLASH_BASE | VECT_TAB_OFFSET;
}

/**
  * @brief  配置系统时钟到72MHz
  */
static void SetSysClock(void)
{
    SetSysClockTo72();
}

/**
  * @brief  设置系统时钟为72MHz
  *         使用HSI (8MHz内部RC振荡器)
  */
static void SetSysClockTo72(void)
{
    __IO uint32_t StartUpCounter = 0, HSEStatus = 0;
    
    /* 使能HSE */
    RCC->CR |= ((uint32_t)RCC_CR_HSEON);
    
    /* 等待HSE就绪或超时 */
    do
    {
        HSEStatus = RCC->CR & RCC_CR_HSERDY;
        StartUpCounter++;  
    } while((HSEStatus == 0) && (StartUpCounter != 0x0500));
    
    if ((RCC->CR & RCC_CR_HSERDY) != RESET)
    {
        HSEStatus = (uint32_t)0x01;
    }
    else
    {
        HSEStatus = (uint32_t)0x00;
    }  
    
    if (HSEStatus == (uint32_t)0x01)
    {
        /* 使能预取缓冲 */
        FLASH->ACR |= FLASH_ACR_PRFTBE;
        
        /* Flash 2个等待周期 */
        FLASH->ACR &= (uint32_t)((uint32_t)~FLASH_ACR_LATENCY);
        FLASH->ACR |= (uint32_t)FLASH_ACR_LATENCY_2;    
        
        /* HCLK = SYSCLK */
        RCC->CFGR |= (uint32_t)RCC_CFGR_HPRE_DIV1;
        
        /* PCLK2 = HCLK */
        RCC->CFGR |= (uint32_t)RCC_CFGR_PPRE2_DIV1;
        
        /* PCLK1 = HCLK/2 */
        RCC->CFGR |= (uint32_t)RCC_CFGR_PPRE1_DIV2;
        
        /* PLL = HSE * 9 = 72 MHz (假设HSE=8MHz) */
        RCC->CFGR &= (uint32_t)((uint32_t)~(RCC_CFGR_PLLSRC | RCC_CFGR_PLLXTPRE | RCC_CFGR_PLLMULL));
        RCC->CFGR |= (uint32_t)(RCC_CFGR_PLLSRC_HSE | RCC_CFGR_PLLMULL9);
        
        /* 使能PLL */
        RCC->CR |= RCC_CR_PLLON;
        
        /* 等待PLL就绪 */
        while((RCC->CR & RCC_CR_PLLRDY) == 0)
        {
        }
        
        /* 选择PLL作为系统时钟源 */
        RCC->CFGR &= (uint32_t)((uint32_t)~(RCC_CFGR_SW));
        RCC->CFGR |= (uint32_t)RCC_CFGR_SW_PLL;    
        
        /* 等待PLL被选为系统时钟源 */
        while ((RCC->CFGR & (uint32_t)RCC_CFGR_SWS) != (uint32_t)0x08)
        {
        }
    }
    else
    {
        /* 如果HSE启动失败，使用HSI */
        /* HSI = 8MHz, 需要PLL倍频到72MHz */
        /* PLL = HSI/2 * 18 = 72 MHz */
        
        /* 使能预取缓冲 */
        FLASH->ACR |= FLASH_ACR_PRFTBE;
        FLASH->ACR &= (uint32_t)((uint32_t)~FLASH_ACR_LATENCY);
        FLASH->ACR |= (uint32_t)FLASH_ACR_LATENCY_2;
        
        /* HCLK = SYSCLK */
        RCC->CFGR |= (uint32_t)RCC_CFGR_HPRE_DIV1;
        
        /* PCLK2 = HCLK */
        RCC->CFGR |= (uint32_t)RCC_CFGR_PPRE2_DIV1;
        
        /* PCLK1 = HCLK/2 */
        RCC->CFGR |= (uint32_t)RCC_CFGR_PPRE1_DIV2;

        /* PLL = HSI/2 * 16 = 64 MHz (最大支持16倍频) */
        RCC->CFGR &= (uint32_t)((uint32_t)~(RCC_CFGR_PLLSRC | RCC_CFGR_PLLXTPRE | RCC_CFGR_PLLMULL));
        RCC->CFGR |= (uint32_t)(RCC_CFGR_PLLSRC_HSI_Div2 | RCC_CFGR_PLLMULL16);
        
        /* 使能PLL */
        RCC->CR |= RCC_CR_PLLON;
        
        /* 等待PLL就绪 */
        while((RCC->CR & RCC_CR_PLLRDY) == 0)
        {
        }
        
        /* 选择PLL作为系统时钟源 */
        RCC->CFGR &= (uint32_t)((uint32_t)~(RCC_CFGR_SW));
        RCC->CFGR |= (uint32_t)RCC_CFGR_SW_PLL;
        
        /* 等待PLL被选为系统时钟源 */
        while ((RCC->CFGR & (uint32_t)RCC_CFGR_SWS) != (uint32_t)0x08)
        {
        }
    }
}

/**
  * @brief  更新SystemCoreClock变量
  */
void SystemCoreClockUpdate (void)
{
    SystemCoreClock = 72000000;
}

