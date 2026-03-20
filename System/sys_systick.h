#ifndef __SYS_SYSTICK_H
#define __SYS_SYSTICK_H

#include "stm32f4xx.h"

// 暴露给外部（通常是 main 函数）的初始化接口
void System_SysTick_Init(uint8_t sysclk);

#endif /* __SYS_SYSTICK_H */
