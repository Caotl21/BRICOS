#ifndef __WATCHDOG_H
#define __WATCHDOG_H

#include "stm32f4xx.h"
#include "stm32f4xx_conf.h"

void Watchdog_Init(void);
void Watchdog_Feed(void);

#endif

