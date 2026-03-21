#include "task_monitor.h"

#include "sys_monitor.h"
#include "sys_log.h"
#include "sys_data_pool.h"

TaskHandle_t Monitor_Task_Handler = NULL;

static void vTask_Monitor_Core(void *pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = pdMS_TO_TICKS(MONITOR_PERIOD_MS);

    (void)pvParameters;

    while (1) {
        uint32_t cpu  = System_Runtime_GetCpuUsagePercent();
        uint32_t temp = System_Runtime_GetChipTemperature();

        Bot_State_Push_SysStatus((float)cpu, (float)temp);
        LOG_INFO("CPU=%lu%%, TEMP=%lu", cpu, temp);

        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}

void Task_Monitor_Init(void)
{
    xTaskCreate((TaskFunction_t)vTask_Monitor_Core,
                (const char *)"Task_Monitor",
                (uint16_t)MONITOR_STK_SIZE,
                (void *)0,
                (UBaseType_t)MONITOR_TASK_PRIO,
                (TaskHandle_t *)&Monitor_Task_Handler);
}

