#include "FreeRTOS.h"
#include "task.h"

#include "bsp_cpu.h"
#include "sys_fault_snapshot.h"

/*
 * FreeRTOS application hook.
 * Called by the kernel when stack overflow checking is enabled.
 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;

    (void)System_FaultSnapshot_SaveStackOverflowTask(pcTaskName);

    taskDISABLE_INTERRUPTS();
    bsp_cpu_reset();

    for (;;)
    {
        /* Should not reach here, but keep looping if reset did not occur. */
    }
}
