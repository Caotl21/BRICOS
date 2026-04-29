#include "task_monitor.h"

#include <string.h>

#include "bsp_watchdog.h"

#include "driver_hydrocore.h"
#include "driver_param.h"

#include "sys_data_pool.h"
#include "sys_log.h"
#include "sys_monitor.h"
#include "sys_mode_manager.h"

#include "task_comm.h"
#include "task_control.h"
#include "task_sensor.h"

TaskHandle_t Monitor_Task_Handler = NULL;

#define MONITOR_LOW_VOLTAGE_WARN_V  (22.0f)
/* 模式切换后保留一个短暂保护窗，避免切换瞬间误报任务超时。 */
#define MODE_SWITCH_GRACE_MS        (800u)
/* 保护窗最大时长兜底，防止 control 心跳长期不同步导致一直屏蔽点名。 */
#define MODE_SWITCH_GUARD_MAX_MS    (3000u)
#define STACK_WATERMARK_LOG_PERIOD_MS (5000u)

static const uint32_t TASK_TIMEOUT_THERSHOLDS[MAX_MONITOR_TASKS] = {
    [TASK_ID_CONTROL] = pdMS_TO_TICKS(100),
    [TASK_ID_IMU] = pdMS_TO_TICKS(200),
    [TASK_ID_MS5837] = pdMS_TO_TICKS(1000),
    [TASK_ID_POWER] = pdMS_TO_TICKS(10000),
    [TASK_ID_DHT11] = pdMS_TO_TICKS(2000),
    [TASK_ID_MONITOR] = pdMS_TO_TICKS(2000)
};

static uint8_t prv_is_task_watch_enabled(bot_sys_mode_e mode, uint8_t task_id)
{
    if (task_id >= MAX_MONITOR_TASKS) {
        return 0u;
    }

    if (mode == SYS_MODE_STANDBY) {
        return (task_id == TASK_ID_MONITOR);
    }

    if (mode == SYS_MODE_FAILSAFE) {
        return (task_id == TASK_ID_MONITOR);
    }

    return 1u;
}

static uint8_t prv_tick_elapsed_ge(uint32_t now_tick, uint32_t start_tick, uint32_t duration_tick)
{
    /* 使用无符号减法比较 tick，能安全处理 tick 回绕。 */
    return ((uint32_t)(now_tick - start_tick) >= duration_tick) ? 1u : 0u;
}

static uint32_t prv_collect_safety_faults(const bot_sys_state_t *sys_state, const bot_params_t *params)
{
    uint32_t faults = SYS_FAULT_NONE;

    if ((sys_state == NULL) || (params == NULL)) {
        return faults;
    }

    if (sys_state->is_leak_detected) {
        faults |= SYS_FAULT_LEAK;
    }

    if (sys_state->is_imu_error) {
        faults |= SYS_FAULT_IMU;
    }

    if ((sys_state->bat_voltage_v > 1.0f) && (sys_state->bat_voltage_v < params->failsafe_low_voltage)) {
        faults |= SYS_FAULT_LOW_VOLTAGE;
    }

    return faults;
}

static uint16_t Serialize_Sys_Report(uint8_t *buf,
                                     const bot_sys_state_t *sys_state,
                                     const bot_params_t *params,
                                     bot_sys_mode_e sys_mode,
                                     bot_run_mode_e motion_mode)
{
    uint16_t offset = 0;

    if ((buf == NULL) || (sys_state == NULL) || (params == NULL))
    {
        return 0u;
    }

    buf[offset++] = (uint8_t)sys_mode;
    buf[offset++] = (uint8_t)motion_mode;

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
    uint8_t low_voltage_alarm_reported = 0u;

    (void)pvParameters;

    static bot_sys_state_t sys_state;
    static bot_params_t params;
    static bot_actuator_state_t actuator_state;
    bot_sys_mode_e last_sys_mode = SYS_MODE_STANDBY;
    bot_sys_mode_e current_sys_mode = SYS_MODE_STANDBY;
    bot_run_mode_e current_motion_mode;

    uint32_t last_ticks[MAX_MONITOR_TASKS];
    uint8_t sys_report_buf[2u + (7u * sizeof(float)) + 3u];
    uint8_t actuator_report_buf[3u];
    /* 记录 STANDBY->DISARMED 过渡被 monitor 首次观测到的 tick。 */
    uint32_t mode_switch_tick = 0u;
    /* 保护窗激活标志：仅在 STANDBY->DISARMED 时置 1。 */
    uint8_t mode_switch_guard_active = 0u;
    /* 首轮循环只初始化基线模式，避免把上电初值误判成“模式切换边沿”。 */
    uint8_t mode_guard_initialized = 0u;
    uint32_t last_stack_log_tick = 0u;

    bsp_wdg_init(5000);

    while (1) {
        uint32_t current_tick = xTaskGetTickCount();
        UBaseType_t wm_monitor;
        UBaseType_t wm_control;
        UBaseType_t wm_rt_comm;
        UBaseType_t wm_nrt_comm;
        UBaseType_t wm_imu;
        UBaseType_t wm_ms5837;
        UBaseType_t wm_power;
        UBaseType_t wm_dht11;

        uint16_t sys_report_len;
        uint16_t actuator_report_len;
        uint32_t failsafe_faults;

        uint32_t cpu = System_Runtime_GetCpuUsagePercent();
        uint32_t temp = System_Runtime_GetChipTemperature();
        Bot_State_Push_SysStatus((float)cpu, (float)temp);

        Bot_Sys_State_Pull(&sys_state);
        Bot_Params_Pull(&params);
        Bot_Actuator_Pull(&actuator_state);
        Bot_Task_LastTick_Pull(last_ticks, MAX_MONITOR_TASKS);
        System_ModeManager_Pull(&current_sys_mode, &current_motion_mode, NULL);

        if (mode_guard_initialized == 0u) {
            last_sys_mode = current_sys_mode;
            mode_guard_initialized = 1u;
        } else if (current_sys_mode != last_sys_mode) {
            /* 仅在 STANDBY -> ACTIVE_DISARMED 进入保护窗，其它模式切换不启用保护窗。 */
            if ((last_sys_mode == SYS_MODE_STANDBY) &&
                (current_sys_mode == SYS_MODE_ACTIVE_DISARMED)) {
                mode_switch_tick = current_tick;
                mode_switch_guard_active = 1u;
                LOG_WARNING("Mode switch guard active: %u->%u",
                            (uint32_t)last_sys_mode,
                            (uint32_t)current_sys_mode);
            } else {
                mode_switch_guard_active = 0u;
            }
            last_sys_mode = current_sys_mode;
        }

        if (sys_state.bat_voltage_v < MONITOR_LOW_VOLTAGE_WARN_V) {
            if (low_voltage_alarm_reported == 0u) {
                LOG_ERROR("Voltage alarm: bat_voltage=%.2fV < %.2fV",
                          sys_state.bat_voltage_v,
                          (double)MONITOR_LOW_VOLTAGE_WARN_V);
                low_voltage_alarm_reported = 1u;
            }
        } else {
            low_voltage_alarm_reported = 0u;
        }

        bool all_tasks_healthy = true;
        uint8_t monitor_only_guard = 0u;
        if (mode_switch_guard_active) {
            /* 保护窗释放条件：最小保护时间已过，且 control 在切换后至少打卡一次。 */
            uint8_t grace_elapsed = prv_tick_elapsed_ge(current_tick,
                                                        mode_switch_tick,
                                                        pdMS_TO_TICKS(MODE_SWITCH_GRACE_MS));
            uint8_t guard_timeout = prv_tick_elapsed_ge(current_tick,
                                                        mode_switch_tick,
                                                        pdMS_TO_TICKS(MODE_SWITCH_GUARD_MAX_MS));
            uint8_t control_synced = prv_tick_elapsed_ge(last_ticks[TASK_ID_CONTROL],
                                                         mode_switch_tick,
                                                         1u);

            if (guard_timeout) {
                mode_switch_guard_active = 0u;
                LOG_WARNING("Mode switch guard timeout: mode=%u", (uint32_t)current_sys_mode);
            } else if (grace_elapsed && control_synced) {
                mode_switch_guard_active = 0u;
                LOG_INFO("Mode switch guard released: mode=%u", (uint32_t)current_sys_mode);
            } else {
                /* 保护窗期间仅依据 monitor 自身健康度喂狗，避免切换窗口误判。 */
                monitor_only_guard = 1u;
            }
        }

        for (uint8_t i = 0; i < MAX_MONITOR_TASKS; i++) {
            if (monitor_only_guard && (i != TASK_ID_MONITOR)) {
                continue;
            }
            if (!prv_is_task_watch_enabled(current_sys_mode, i)) {
                continue;
            }

            uint32_t last_tick = last_ticks[i];
            uint32_t diff_tick = current_tick - last_tick;
            if (diff_tick > TASK_TIMEOUT_THERSHOLDS[i]){
                all_tasks_healthy = false;
                LOG_ERROR("Task %u timeout! Last tick: %lu, diff: %lu ms", i, last_tick, diff_tick);
                break;
            }
        }

        failsafe_faults = prv_collect_safety_faults(&sys_state, &params);
        if (!all_tasks_healthy) {
            failsafe_faults |= SYS_FAULT_TASK_TIMEOUT;
        }

        if (failsafe_faults != SYS_FAULT_NONE) {
            (void)System_ModeManager_EnterFailsafe(failsafe_faults);
        }

        if (all_tasks_healthy) {
            LOG_INFO("All tasks healthy!! CPU=%lu%%, TEMP=%lu", cpu, temp);
            bsp_wdg_feed();
        }

        sys_report_len = Serialize_Sys_Report(sys_report_buf,
                                              &sys_state,
                                              &params,
                                              current_sys_mode,
                                              current_motion_mode);
        actuator_report_len = Serialize_Actuator_Report(actuator_report_buf, &actuator_state);

        if (sys_report_len != 0u)
        {
            Driver_Protocol_SendFrame(BSP_UART_OPI_NRT,
                                      DATA_TYPE_STATE_SYS,
                                      sys_report_buf,
                                      (uint8_t)sys_report_len,
                                      USE_CPU);
        }

        if (actuator_report_len != 0u)
        {
            Driver_Protocol_SendFrame(BSP_UART_OPI_NRT,
                                      DATA_TYPE_STATE_ACTUATOR,
                                      actuator_report_buf,
                                      (uint8_t)actuator_report_len,
                                      USE_CPU);
        }

        if ((current_sys_mode == SYS_MODE_MOTION_ARMED) &&
            prv_tick_elapsed_ge(current_tick,
                                last_stack_log_tick,
                                pdMS_TO_TICKS(STACK_WATERMARK_LOG_PERIOD_MS))) {
            wm_monitor = uxTaskGetStackHighWaterMark(NULL);
            wm_control = (Control_Task_Handler != NULL) ? uxTaskGetStackHighWaterMark(Control_Task_Handler) : 0u;
            wm_rt_comm = (RT_Comm_Task_Handler != NULL) ? uxTaskGetStackHighWaterMark(RT_Comm_Task_Handler) : 0u;
            wm_nrt_comm = (NRT_Comm_Task_Handler != NULL) ? uxTaskGetStackHighWaterMark(NRT_Comm_Task_Handler) : 0u;
            wm_imu = (IMU_Task_Handler != NULL) ? uxTaskGetStackHighWaterMark(IMU_Task_Handler) : 0u;
            wm_ms5837 = (MS5837_Task_Handler != NULL) ? uxTaskGetStackHighWaterMark(MS5837_Task_Handler) : 0u;
            wm_power = (Power_Task_Handler != NULL) ? uxTaskGetStackHighWaterMark(Power_Task_Handler) : 0u;
            wm_dht11 = (DHT11_Task_Handler != NULL) ? uxTaskGetStackHighWaterMark(DHT11_Task_Handler) : 0u;

            LOG_INFO("StackHWM(words) MON=%lu CTRL=%lu RT=%lu NRT=%lu",
                     (unsigned long)wm_monitor,
                     (unsigned long)wm_control,
                     (unsigned long)wm_rt_comm,
                     (unsigned long)wm_nrt_comm);
            LOG_INFO("StackHWM(words) IMU=%lu MS=%lu PWR=%lu DHT=%lu",
                     (unsigned long)wm_imu,
                     (unsigned long)wm_ms5837,
                     (unsigned long)wm_power,
                     (unsigned long)wm_dht11);

            last_stack_log_tick = current_tick;
        }

        Bot_Task_CheckIn_Monitor(TASK_ID_MONITOR);

        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}

void Task_Monitor_Init(void)
{
    System_Watchdog_Init();
    xTaskCreate((TaskFunction_t)vTask_Monitor_Core,
                (const char *)"Task_Monitor",
                (uint16_t)MONITOR_STK_SIZE,
                (void *)0,
                (UBaseType_t)MONITOR_TASK_PRIO,
                (TaskHandle_t *)&Monitor_Task_Handler);
}
