#ifndef __BSP_TIMER_H
#define __BSP_TIMER_H

#include <stdint.h>

/**
 * @brief  初始化 TIM2 为 FreeRTOS 提供运行时间统计基准 (零中断方案)
 * @note   TIM2 挂载在 APB1 (84MHz)。
 * 将其配置为 50us 计数一次，32位计数器 59 小时才会溢出。
 */
void ConfigureTimeForRunTimeStats(void);

/**
 * @brief  获取硬件定时器的当前计数值
 * @retval uint32_t 当前的 32 位时间戳 (单位: 50us)
 * @note   封装底层寄存器操作，对上层提供干净的 API
 */
extern uint32_t BSP_Timer_GetRunTimeTicks(void);

#endif /* __BSP_TIMER_H */
