#ifndef __TASK_NRT_CMD_H
#define __TASK_NRT_CMD_H

#include <stdint.h>

/* ============================================================================
 * 对外开放的 API 接口
 * ============================================================================ */
/**
 * @brief  初始化并注册串口处理函数
 * @note   在 main 函数启动 FreeRTOS 调度器 (vTaskStartScheduler) 之前调用。
 */
void Task_NRT_Cmd_Init(void);

#endif /* __TASK_NRT_CMD_H */