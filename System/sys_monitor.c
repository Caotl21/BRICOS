#include "sys_monitor.h"
#include "bsp_timer.h"
#include "bsp_adc.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

static bsp_timer_cfg_t sys_monitor_timer_cfg = { .timer = BSP_TIM_SYSCOUNT, .tick_us = 0U };

bool System_Runtime_Monitor_Init(void)
{
    uint32_t res;
    // 初始化定时器硬件
    bsp_timer_cfg_t timer_cfg = {
        .timer = BSP_TIM_SYSCOUNT,
        .tick_us = 50 // 50us 计一次数，20kHz 计数频率
    };
    res = bsp_timer_init(&timer_cfg);
    if (!res)  return false;
    sys_monitor_timer_cfg = timer_cfg; // 保存配置以供后续读取计数时使用

    // 初始化ADC硬件
    bsp_adc_ch_t adc_ch_list[] = { BSP_ADC_CHIPTEMP };
    const bsp_adc_config_t adc_cfg = {
        .sample_time = BSP_ADC_SAMPLE_3CYC,
        .scan_enable = false,
        .continuous_enable = false,
        .dma_enable = false
    };
    res = bsp_adc_init(adc_ch_list, 1, NULL, &adc_cfg);
    if (!res)  return false;
    
    return res;
}

uint32_t System_Runtime_GetCounter(void)
{
    return bsp_timer_get_ticks(&sys_monitor_timer_cfg);
}

uint32_t System_Runtime_GetCpuUsagePercent(void)
{
    volatile UBaseType_t uxArraySize, x;
    uint32_t ulTotalRunTime=0, ulIDLERunTime=0;
    uint32_t cpu_usage = 0;

    // 获取当前任务数量
    uxArraySize = uxTaskGetNumberOfTasks();
    if (uxArraySize == 0U) {
        return 0U;
    }

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
            if (strncmp(pxTaskStatusArray[x].pcTaskName, "IDLE", 4) == 0) {
                ulIDLERunTime += pxTaskStatusArray[x].ulRunTimeCounter;
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
