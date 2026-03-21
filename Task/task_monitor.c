#include "task_monitor.h"

#include <stdio.h>

#include "bsp_uart.h"
#include "driver_param.h"
#include "sys_data_pool.h"
#include "sys_log.h"
#include "sys_monitor.h"

TaskHandle_t Monitor_Task_Handler = NULL;

static void Send_Sys_Report_To_OrangePi(const bot_sys_state_t *sys_state,
                                        const bot_params_t *params)
{
    char report_buf[192];
    int len;
    int water_temp_x100;
    int cabin_temp_x100;
    int cabin_humi_x100;
    int volt_x100;
    int curr_x100;
    int chip_temp_x100;
    int cpu_x100;
    int leak_flag;
    int imu_flag;
    int low_voltage_flag;

    if ((sys_state == NULL) || (params == NULL))
    {
        return;
    }

    leak_flag = sys_state->is_leak_detected ? 1 : 0;
    imu_flag = sys_state->is_imu_error ? 1 : 0;
    low_voltage_flag = (sys_state->bat_voltage_v < params->failsafe_low_voltage) ? 1 : 0;
    water_temp_x100 = (int)(sys_state->water_temp_c * 100.0f);
    cabin_temp_x100 = (int)(sys_state->cabin_temp_c * 100.0f);
    cabin_humi_x100 = (int)(sys_state->cabin_humi * 100.0f);
    volt_x100 = (int)(sys_state->bat_voltage_v * 100.0f);
    curr_x100 = (int)(sys_state->bat_current_a * 100.0f);
    chip_temp_x100 = (int)(sys_state->chip_temp * 100.0f);
    cpu_x100 = (int)(sys_state->cpu_usage * 100.0f);

    len = snprintf(report_buf, sizeof(report_buf),
                   "[SYS] mode=%u motion=%u water_x100=%d cabin_t_x100=%d cabin_h_x100=%d volt_x100=%d curr_x100=%d chip_x100=%d cpu_x100=%d leak=%d imu=%d lowv=%d\r\n",
                   (unsigned int)params->sys_mode,
                   (unsigned int)params->motion_mode,
                   water_temp_x100,
                   cabin_temp_x100,
                   cabin_humi_x100,
                   volt_x100,
                   curr_x100,
                   chip_temp_x100,
                   cpu_x100,
                   leak_flag,
                   imu_flag,
                   low_voltage_flag);

    if (len > 0)
    {
        if (len > (int)sizeof(report_buf))
        {
            len = (int)sizeof(report_buf);
        }
        bsp_uart_send_buffer(BSP_UART_OPI_NRT, (const uint8_t *)report_buf, (uint16_t)len);
    }
}

static void Send_Actuator_Report_To_OrangePi(const bot_actuator_state_t *actuator_state)
{
    char report_buf[96];
    int len;

    if (actuator_state == NULL)
    {
        return;
    }

    len = snprintf(report_buf, sizeof(report_buf),
                   "[ACT] servo=%u light1=%u light2=%u\r\n",
                   (unsigned int)actuator_state->servo_angle,
                   (unsigned int)actuator_state->light1_pwm,
                   (unsigned int)actuator_state->light2_pwm);

    if (len > 0)
    {
        if (len > (int)sizeof(report_buf))
        {
            len = (int)sizeof(report_buf);
        }
        bsp_uart_send_buffer(BSP_UART_OPI_NRT, (const uint8_t *)report_buf, (uint16_t)len);
    }
}

static void vTask_Monitor_Core(void *pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = pdMS_TO_TICKS(MONITOR_PERIOD_MS);
    uint8_t log_divider = 0;

    (void)pvParameters;

    while (1) {
        bot_sys_state_t sys_state;
        bot_params_t params;
        bot_actuator_state_t actuator_state;
        uint32_t cpu = System_Runtime_GetCpuUsagePercent();
        uint32_t temp = System_Runtime_GetChipTemperature();

        Bot_State_Push_SysStatus((float)cpu, (float)temp);
        Bot_Sys_State_Pull(&sys_state);
        Bot_Params_Pull(&params);
        Bot_Actuator_Pull(&actuator_state);

        Send_Sys_Report_To_OrangePi(&sys_state, &params);
        Send_Actuator_Report_To_OrangePi(&actuator_state);

        if (++log_divider >= 5u) {
            log_divider = 0u;
            LOG_INFO("CPU=%lu%%, TEMP=%lu", cpu, temp);
        }

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
