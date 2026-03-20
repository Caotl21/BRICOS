#include "bsp_timer.h"
#include "stm32f4xx.h"
#include "stm32f4xx_tim.h"

/**
 * @brief  初始化 TIM2 为 FreeRTOS 提供运行时间统计基准 (零中断方案)
 * @note   TIM2 挂载在 APB1 (84MHz)。
 * 将其配置为 50us 计数一次，32位计数器 59 小时才会溢出。
 */
void ConfigureTimeForRunTimeStats(void)
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
    
    // 1. 使能 TIM2 时钟
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE); 
    
    // 2. 配置定时器参数
    // APB1 定时器时钟为 84MHz
    // 预分频器设为 (84 * 50) - 1 = 4199，因此计数频率为 84MHz / 4200 = 20kHz (即 50us 计一次数)
    TIM_TimeBaseInitStructure.TIM_Prescaler = 4200 - 1; 
    
    // TIM2 是 32 位定时器，直接把重装载值拉满
    TIM_TimeBaseInitStructure.TIM_Period = 0xFFFFFFFF; 
    
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up; 
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1; 
    
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStructure);
    
    // 3. 极其清爽：不需要配置 NVIC 中断，直接启动定时器！
    TIM_Cmd(TIM2, ENABLE); 
}


uint32_t BSP_Timer_GetRunTimeTicks(void)
{
    // 直接返回硬件寄存器的值
    return TIM2->CNT;
}
