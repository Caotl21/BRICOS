#ifndef __SYS_PORT_H
#define __SYS_PORT_H

#include "bsp_delay.h" // 包含你之前写的死等延时
#include "stm32f4xx.h"

#define USE_FREERTOS  1

#if (USE_FREERTOS == 1)
    #include "FreeRTOS.h"
    #include "task.h"

    // RTOS 环境下的延时：交出 CPU
    #define SYS_DELAY_MS(ms)           vTaskDelay(pdMS_TO_TICKS(ms))
    
    // RTOS 环境下的临界区：挂起系统调度
    #define SYS_ENTER_CRITICAL()       taskENTER_CRITICAL()
    #define SYS_EXIT_CRITICAL()        taskEXIT_CRITICAL()
    
    // RTOS 环境下的全局锁 (如 OTA 擦除 Flash 时使用)
    #define SYS_SUSPEND_ALL()          vTaskSuspendAll()
    #define SYS_RESUME_ALL()           xTaskResumeAll()

#else
    // 裸机环境下的延时：CPU 死等
    #define SYS_DELAY_MS(ms)           bsp_delay_ms(ms)
    
    // 裸机环境下的临界区：直接关闭/打开全局中断
    #define SYS_ENTER_CRITICAL()       __disable_irq()
    #define SYS_EXIT_CRITICAL()        __enable_irq()
    
    // 裸机环境下没有任务调度，宏置空即可
    #define SYS_SUSPEND_ALL()          do {} while(0)
    #define SYS_RESUME_ALL()           do {} while(0)

#endif

#endif // __SYS_PORT_H
