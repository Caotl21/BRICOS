#ifndef __TASK_COMM_H
#define __TASK_COMM_H

#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"

/* ============================================================================
 * 任务资源分配宏定义 (数字越大优先级越高)
 * ============================================================================ */

/* 实时通信任务 */
#define RT_COMM_TASK_PRIO       4
#define RT_COMM_STK_SIZE        512  

/* 非实时通信任务 */
#define NRT_COMM_TASK_PRIO      3
#define NRT_COMM_STK_SIZE       512

/* ============================================================================
 * 全局任务句柄声明 (外部可通过这些句柄操控任务运行状态)
 * ============================================================================ */
extern TaskHandle_t RT_Comm_Task_Handler;
extern TaskHandle_t NRT_Comm_Task_Handler;

/* ============================================================================
 * 对外开放的 API 接口
 * ============================================================================ */
/**
 * @brief  初始化并创建所有的上下位机通信任务
 * @note   在 main 函数启动 FreeRTOS 调度器 (vTaskStartScheduler) 之前调用。
 */
void Task_Comm_Init(void);

#endif /* __TASK_COMM_H */
