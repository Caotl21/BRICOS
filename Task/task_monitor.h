#ifndef __TASK_MONITOR_H
#define __TASK_MONITOR_H

#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"

#define MONITOR_TASK_PRIO      1
#define MONITOR_STK_SIZE       512
#define MONITOR_PERIOD_MS      1000

extern TaskHandle_t Monitor_Task_Handler;

void Task_Monitor_Init(void);

#endif /* __TASK_MONITOR_H */

