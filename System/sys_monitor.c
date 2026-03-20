#include "sys_monitor.h"
#include "bsp_timer.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

static bsp_timer_cfg_t sys_monitor_timer_cfg = {0};

bool System_Runtime_Monitor_Init(void)
{
    uint32_t res;
    // 初始化定时器硬件
    bsp_timer_cfg_t timer_cfg = {
        .timer = BSP_TIM_SYSCOUNT,
        .tick_us = 50 // 50us 计一次数，20kHz 计数频率
    };
    res = bsp_timer_init(&timer_cfg);
    
    sys_monitor_timer_cfg = timer_cfg; // 保存配置以供后续读取计数时使用

    return res;
}

uint32_t System_Runtime_GetCounter(void)
{
    return bsp_timer_get_ticks(&sys_monitor_timer_cfg);
}

uint32_t System_Runtime_GetCpuUsagePercent(void)
{
    volatile UBaseType_t uxArraySize, x;
    uint32_t ulTotalRunTime, ulIDLERunTime;
    uint32_t cpu_usage = 0;

    // 获取当前任务数量
    uxArraySize = uxTaskGetNumberOfTasks();

    // 为每个任务分配一个结构体来保存状态
    TaskStatus_t *pxTaskStatusArray = pvPortMalloc(uxArraySize * sizeof(TaskStatus_t));
    if (pxTaskStatusArray != NULL) {
        // 获取所有任务的状态
        uxArraySize = uxTaskGetSystemState(pxTaskStatusArray, uxArraySize, &ulTotalRunTime);

        if (ulTotalRunTime == 0) {
            vPortFree(pxTaskStatusArray);
            return 0; // 避免除以零
        }
        
        // 查找 IDLE 任务的运行时间
        for (x = 0; x < uxArraySize; x++) {
            if (strcmp(pxTaskStatusArray[x].pcTaskName, "IDLE") == 0) {
                ulIDLERunTime = pxTaskStatusArray[x].ulRunTimeCounter;
                break;
            }
        }

        // 计算 CPU 使用率
        if (ulTotalRunTime > 0) {
            cpu_usage = (100UL * (ulTotalRunTime - ulIDLERunTime)) / ulTotalRunTime;
        }

        vPortFree(pxTaskStatusArray);
    }

    return cpu_usage;
}