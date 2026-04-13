#ifndef __TASK_CONTROL_H
#define __TASK_CONTROL_H

#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

/* 控制任务优先级与栈大小（100Hz 控制环） */
#define CONTROL_TASK_PRIO       5
#define CONTROL_STK_SIZE        512

/* 控制任务句柄 */
extern TaskHandle_t Control_Task_Handler;

/* 控制任务初始化（在启动调度器前调用） */
void Task_Control_Init(void);

#endif /* __TASK_CONTROL_H */

