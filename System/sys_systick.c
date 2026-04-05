#include "sys_systick.h"
#include "FreeRTOS.h"
#include "task.h"

/* 如果你后续要在本文件里实现 delay_us/delay_ms，这两个变量可以设为 static 私有。
 * 如果你的工程里别的地方（比如原有的 delay.c）还要用它们，就保持这样全局可见。*/
uint32_t fac_us = 0;
uint32_t fac_ms = 0;

/* 声明 FreeRTOS 底层汇编里的节拍处理函数 */
extern void xPortSysTickHandler(void);

/**
 * @brief  初始化 SysTick 定时器
 * @param  sysclk 系统主频 (针对 STM32F407，通常传入 168)
 * @note   将 SysTick 配置为兼容 FreeRTOS 的 1ms 中断周期
 */
void System_SysTick_Init(uint8_t sysclk)
{
    uint32_t reload;
    
    // 1. 选择 HCLK (AHB) 作为 SysTick 时钟源 (168MHz)
    SysTick_CLKSourceConfig(SysTick_CLKSource_HCLK); 
    
    // 2. 计算倍乘因子
    fac_us = sysclk;                                  // 1us 需要的 tick 计数次数
    reload = sysclk;                                  // 1us 的次数
    reload *= 1000000 / configTICK_RATE_HZ;           // 乘以 1个 OS Tick 包含的 us 数，算出重装载值
    fac_ms = 1000 / configTICK_RATE_HZ;               // 1个 OS Tick 对应的 ms 数 (通常是 1)
    
    // 3. 配置内部寄存器并启动
    SysTick->CTRL |= SysTick_CTRL_TICKINT_Msk;        // 开启 SYSTICK 中断
    SysTick->LOAD = reload;                           // 装载计算好的溢出时间值
    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;         // 使能 SYSTICK 定时器
}

/**
 * @brief  Cortex-M 系统滴答定时器硬件中断服务函数
 * @note   这是操作系统的“心脏起搏器”
 */
void SysTick_Handler(void)
{   
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
        xPortSysTickHandler();  // 调用 FreeRTOS 内核的节拍更新逻辑
    }
}
