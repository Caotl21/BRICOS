#include "FreeRTOS.h"
#include "task.h"

#include "stm32f4xx_rcc.h"

#include "bsp_delay.h"
#include "bsp_gpio.h"
#include "bsp_pwm.h"
#include "bsp_uart.h"

#include "driver_dht11.h"
#include "driver_imu.h"
#include "driver_ms5837.h"
#include "driver_power.h"
#include "driver_thruster.h"

#include "shell/app_shell.h"
#include "sys_boot_flag.h"
#include "sys_data_pool.h"
#include "sys_log.h"
#include "sys_mode_manager.h"
#include "sys_systick.h"

#include "fault_snapshot.h"

#include "task_comm.h"
#include "task_control.h"
#include "task_monitor.h"
#include "task_nrt_cmd.h"
#include "task_rt_cmd.h"
#include "task_sensor.h"

static void Main_Log_ResetReason(void)
{
    uint32_t csr = RCC->CSR;
    uint8_t iwdg = (RCC_GetFlagStatus(RCC_FLAG_IWDGRST) == SET) ? 1u : 0u;
    uint8_t wwdg = (RCC_GetFlagStatus(RCC_FLAG_WWDGRST) == SET) ? 1u : 0u;
    uint8_t borrst = (RCC_GetFlagStatus(RCC_FLAG_BORRST) == SET) ? 1u : 0u;
    uint8_t porrst = (RCC_GetFlagStatus(RCC_FLAG_PORRST) == SET) ? 1u : 0u;
    uint8_t pinrst = (RCC_GetFlagStatus(RCC_FLAG_PINRST) == SET) ? 1u : 0u;
    uint8_t sftrst = (RCC_GetFlagStatus(RCC_FLAG_SFTRST) == SET) ? 1u : 0u;
    uint8_t lpwrrst = (RCC_GetFlagStatus(RCC_FLAG_LPWRRST) == SET) ? 1u : 0u;

    LOG_INFO("Reset flags: CSR=0x%08lX IWDG=%u WWDG=%u BOR=%u POR=%u PIN=%u SFT=%u LPWR=%u",
             (unsigned long)csr,
             (unsigned)iwdg,
             (unsigned)wwdg,
             (unsigned)borrst,
             (unsigned)porrst,
             (unsigned)pinrst,
             (unsigned)sftrst,
             (unsigned)lpwrrst);

    RCC_ClearFlag();
}

int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);

    bsp_delay_init();
    bsp_uart_init_default();
    bsp_gpio_init();
    bsp_pwm_init(0);

    Driver_IMU_Init();
    Driver_Ms5837_Init();
    Driver_DHT11_Init();
    Driver_Power_Init();
    Driver_Thruster_Init();

    System_SysTick_Init(SYSCLK);
    Sys_BootFlag_MarkBootSuccess();
    System_Log_Init();
    Main_Log_ResetReason();
    Bot_Data_Pool_Init();
    System_ModeManager_Init();

    {
        char overflow_task_name[32];

        if (System_FaultSnapshot_LoadLastStackOverflowTask(overflow_task_name, sizeof(overflow_task_name))) {
            LOG_ERROR("Last stack overflow task: %s", overflow_task_name);
        }
    }

    Task_Comm_Init();
    Task_Sensor_Init();
    Task_Control_Init();
    Task_NRT_Cmd_Init();
    Task_RT_Cmd_Init();
    Task_Monitor_Init();

    App_Shell_Init();

    LOG_INFO("======================================");
    LOG_INFO("   BRICOS System Booting...       ");
    LOG_INFO("   Tasks Initialization...     ");
    LOG_INFO("======================================");

    vTaskStartScheduler();

    while (1) {
    }
}
