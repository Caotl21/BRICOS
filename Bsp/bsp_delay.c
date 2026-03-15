#include "bsp_delay.h"
#include "stm32f4xx.h"

// 声明外部变量，这通常在 system_stm32f4xx.c 中定义，代表当前系统主频 (如 168000000)
extern uint32_t SystemCoreClock; 

bool bsp_delay_init(void) 
{
    // 先使能 DWT 外设的跟踪功能 (开启 DWT 时钟)
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    
    // 将 DWT 的周期计数器清零
    DWT->CYCCNT = 0;
    
    // 启动 DWT 周期计数器
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    
    return true;
}

void bsp_delay_us(uint32_t us) 
{
    // 计算需要的 CPU 时钟周期数
    // 假设主频 168MHz，那 1us 就是 168 个周期
    uint32_t ticks = us * (SystemCoreClock / 1000000);
    
    // 记录进入延时函数那一刻的计数器值
    uint32_t start_tick = DWT->CYCCNT;
    
    // 死等，直到经过了目标周期数
    // 注意：这里的减法利用了无符号整数溢出特性，哪怕 DWT->CYCCNT 中途溢出归零，结果也是绝对正确的！
    while ((DWT->CYCCNT - start_tick) < ticks) {
        // 空循环死等
    }
}

void bsp_delay_ms(uint32_t ms) 
{
    // 毫秒延时直接复用微秒逻辑
    while (ms--) {
        bsp_delay_us(1000);
    }
}