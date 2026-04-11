#ifndef __SYS_SYSTICK_H
#define __SYS_SYSTICK_H

#include "stm32f4xx.h"

#define SYSCLK 168

void System_SysTick_Init(uint8_t sysclk);

#endif /* __SYS_SYSTICK_H */
