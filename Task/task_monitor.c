#include "task_monitor.h"

#include <string.h>

#include "driver_hydrocore.h"
#include "driver_param.h"
#include "sys_data_pool.h"
#include "sys_log.h"
#include "sys_monitor.h"

TaskHandle_t Monitor_Task_Handler = NULL;

static uint16_t Serialize_Sys_Report(uint8_t *buf,
                                     const bot_sys_state_t *sys_state,
                                     const bot_params_t *params)
{
    uint16_t offset = 0;

    if ((buf == NULL) || (sys_state == NULL) || (params == NULL))
    {
        return 0u;
    }

    buf[offset++] = (uint8_t)params->sys_mode;
    buf[offset++] = (uint8_t)params->motion_mode;

    memcpy(&buf[offset], &sys_state->water_temp_c, sizeof(float)); offset += sizeof(float);
    memcpy(&buf[offset], &sys_state->cabin_temp_c, sizeof(float)); offset += sizeof(float);
    memcpy(&buf[offset], &sys_state->cabin_humi, sizeof(float)); offset += sizeof(float);
    memcpy(&buf[offset], &sys_state->bat_voltage_v, sizeof(float)); offset += sizeof(float);
    memcpy(&buf[offset], &sys_state->bat_current_a, sizeof(float)); offset += sizeof(float);
    memcpy(&buf[offset], &sys_state->chip_temp, sizeof(float)); offset += sizeof(float);
    memcpy(&buf[offset], &sys_state->cpu_usage, sizeof(float)); offset += sizeof(float);

    buf[offset++] = sys_state->is_leak_detected ? 1u : 0u;
    buf[offset++] = sys_state->is_imu_error ? 1u : 0u;
    buf[offset++] = (sys_state->bat_voltage_v < params->failsafe_low_voltage) ? 1u : 0u;

    return offset;
}

static uint16_t Serialize_Actuator_Report(uint8_t *buf, const bot_actuator_state_t *actuator_state)
{
    if ((buf == NULL) || (actuator_state == NULL))
    {
        return 0u;
    }

    buf[0] = actuator_state->servo_angle;
    buf[1] = actuator_state->light1_pwm;
    buf[2] = actuator_state->light2_pwm;

    return 3u;
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
        uint8_t sys_report_buf[2u + (7u * sizeof(float)) + 3u];
        uint8_t actuator_report_buf[3u];
        uint16_t sys_report_len;
        uint16_t actuator_report_len;
        uint32_t cpu = System_Runtime_GetCpuUsagePercent();
        uint32_t temp = System_Runtime_GetChipTemperature();

        Bot_State_Push_SysStatus((float)cpu, (float)temp);
        Bot_Sys_State_Pull(&sys_state);
        Bot_Params_Pull(&params);
        Bot_Actuator_Pull(&actuator_state);

        sys_report_len = Serialize_Sys_Report(sys_report_buf, &sys_state, &params);
        actuator_report_len = Serialize_Actuator_Report(actuator_report_buf, &actuator_state);

        if (sys_report_len != 0u)
        {
            Driver_Protocol_SendFrame(BSP_UART_OPI_NRT,
                                      DATA_TYPE_STATE_SYS,
                                      sys_report_buf,
                                      (uint8_t)sys_report_len);
        }

        if (actuator_report_len != 0u)
        {
            Driver_Protocol_SendFrame(BSP_UART_OPI_NRT,
                                      DATA_TYPE_STATE_ACTUATOR,
                                      actuator_report_buf,
                                      (uint8_t)actuator_report_len);
        }

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
