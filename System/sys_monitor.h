#ifndef __SYS_MONITOR_H
#define __SYS_MONITOR_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * 任务监控 ID（用于心跳看门狗）
 * ============================================================================ */
typedef enum {
    TASK_ID_CONTROL = 0,  /* 控制任务 */
    TASK_ID_IMU,          /* IMU 采集任务 */
    TASK_ID_MS5837,       /* 深度传感器任务 */
    TASK_ID_POWER,        /* 电源采样任务 */
    TASK_ID_DHT11,        /* 舱内温湿度任务 */
    TASK_ID_MONITOR,      /* 监控任务 */
    MAX_MONITOR_TASKS
} monitor_task_id_t;

bool System_Watchdog_Init(void);
bool System_Runtime_Monitor_Init(void);
uint32_t System_Runtime_GetCounter(void);
uint32_t System_Runtime_GetCpuUsagePercent(void);
uint32_t System_Runtime_GetChipTemperature(void);

/* --------------------------- 任务心跳监控接口 ------------------------------- */
void Bot_Task_CheckIn_Monitor(monitor_task_id_t task_id);
void Bot_Task_LastTick_Pull(uint32_t *out_ticks, uint8_t len);
void Bot_Task_LastTick_Reset(monitor_task_id_t task_id);

#endif /* __SYS_MONITOR_H */
