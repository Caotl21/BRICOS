#include "sys_data_pool.h"
#include "sys_port.h"
#include "sys_log.h"

#include <string.h>

#include "driver_param.h"

static bot_body_state_t s_bricsbot_state;
static bot_sys_state_t s_bricsbot_sys_state;
static uint32_t s_task_last_tick[MAX_MONITOR_TASKS];
static bot_actuator_state_t s_bricsbot_actuator_target;
static bot_target_t s_bricsbot_target;
static bot_params_t s_bricsbot_params;

void Bot_Data_Pool_Init(void)
{
    memset(&s_bricsbot_state, 0, sizeof(s_bricsbot_state));
    memset(&s_bricsbot_sys_state, 0, sizeof(s_bricsbot_sys_state));
    memset(&s_task_last_tick, 0, sizeof(s_task_last_tick));
    memset(&s_bricsbot_actuator_target, 0, sizeof(s_bricsbot_actuator_target));
    memset(&s_bricsbot_target, 0, sizeof(s_bricsbot_target));
    memset(&s_bricsbot_params, 0, sizeof(s_bricsbot_params));

    s_bricsbot_actuator_target.servo_angle = 90u;

    Driver_PidParam_FillDefault(&s_bricsbot_params);
    if (!Driver_PidParam_Load(&s_bricsbot_params)) {
        (void)Driver_PidParam_Save(&s_bricsbot_params);
    }

    s_bricsbot_params.failsafe_max_depth = 10.0f;
    s_bricsbot_params.failsafe_low_voltage = 10.0f;
}

void Bot_State_Pull(bot_body_state_t *out_state)
{
    if (out_state == NULL) return;

    SYS_ENTER_CRITICAL();
    memcpy(out_state, &s_bricsbot_state, sizeof(bot_body_state_t));
    SYS_EXIT_CRITICAL();
}

void Bot_Sys_State_Pull(bot_sys_state_t *out_state)
{
    if (out_state == NULL) return;

    SYS_ENTER_CRITICAL();
    memcpy(out_state, &s_bricsbot_sys_state, sizeof(bot_sys_state_t));
    SYS_EXIT_CRITICAL();
}

void Bot_Target_Pull(bot_target_t *out_target)
{
    if (out_target == NULL) return;

    SYS_ENTER_CRITICAL();
    memcpy(out_target, &s_bricsbot_target, sizeof(bot_target_t));
    SYS_EXIT_CRITICAL();
}

void Bot_Params_Pull(bot_params_t *out_params)
{
    if (out_params == NULL) return;

    SYS_ENTER_CRITICAL();
    memcpy(out_params, &s_bricsbot_params, sizeof(bot_params_t));
    SYS_EXIT_CRITICAL();
}

void Bot_State_LeakStatus_Pull(bool *out_is_leaking)
{
    if (out_is_leaking == NULL) return;

    SYS_ENTER_CRITICAL();
    *out_is_leaking = s_bricsbot_sys_state.is_leak_detected;
    SYS_EXIT_CRITICAL();
}

void Bot_State_Push_IMU(bot_body_state_t *imu_data)
{
    if (imu_data == NULL) return;

    SYS_ENTER_CRITICAL();
    memcpy(s_bricsbot_state.Quater, imu_data->Quater, sizeof(float) * 4u);
    s_bricsbot_state.gyro_x = imu_data->gyro_x;
    s_bricsbot_state.gyro_y = imu_data->gyro_y;
    s_bricsbot_state.gyro_z = imu_data->gyro_z;
    s_bricsbot_state.acc_x = imu_data->acc_x;
    s_bricsbot_state.acc_y = imu_data->acc_y;
    s_bricsbot_state.acc_z = imu_data->acc_z;
    SYS_EXIT_CRITICAL();
}

void Bot_State_Push_DepthTemp(float depth, float water_temp)
{
    SYS_ENTER_CRITICAL();
    s_bricsbot_state.depth_m = depth;
    s_bricsbot_sys_state.water_temp_c = water_temp;
    SYS_EXIT_CRITICAL();
}

void Bot_State_Push_CabinEnv(float temp, float humi, bool leak)
{
    SYS_ENTER_CRITICAL();
    s_bricsbot_sys_state.cabin_temp_c = temp;
    s_bricsbot_sys_state.cabin_humi = humi;
    s_bricsbot_sys_state.is_leak_detected = leak;
    SYS_EXIT_CRITICAL();
}

void Bot_State_Push_Power(float vol, float cur)
{
    SYS_ENTER_CRITICAL();
    s_bricsbot_sys_state.bat_voltage_v = vol;
    s_bricsbot_sys_state.bat_current_a = cur;
    SYS_EXIT_CRITICAL();
}

void Bot_Params_Push_Servo(uint8_t angle)
{
    // 待需要时实现，目前仓内未使用
}

void Bot_Params_Push_Light(uint8_t light1_pwm, uint8_t light2_pwm)
{
    // 待需要时实现，目前仓内未使用
}

void Bot_State_Push_SysStatus(float cpu_usage, float chip_temp)
{
    SYS_ENTER_CRITICAL();
    s_bricsbot_sys_state.cpu_usage = cpu_usage;
    s_bricsbot_sys_state.chip_temp = chip_temp;
    SYS_EXIT_CRITICAL();
}

void Bot_Actuator_Pull(bot_actuator_state_t *out_state)
{
    if (out_state == NULL) return;

    SYS_ENTER_CRITICAL();
    memcpy(out_state, &s_bricsbot_actuator_target, sizeof(bot_actuator_state_t));
    SYS_EXIT_CRITICAL();
}

void Bot_Task_CheckIn_Monitor(monitor_task_id_t task_id)
{
    if (task_id >= MAX_MONITOR_TASKS) {
        LOG_ERROR("Invalid task ID %u in Bot_Task_CheckIn_Monitor", task_id);
        return;
    }

    SYS_ENTER_CRITICAL();
    s_task_last_tick[task_id] = xTaskGetTickCount();
    SYS_EXIT_CRITICAL();
}

void Bot_Task_LastTick_Pull(uint32_t *out_ticks, uint8_t len)
{
    if ((out_ticks == NULL) || (len < MAX_MONITOR_TASKS)) return;

    SYS_ENTER_CRITICAL();
    memcpy(out_ticks, s_task_last_tick, sizeof(uint32_t) * MAX_MONITOR_TASKS);
    SYS_EXIT_CRITICAL();
}

void Bot_Target_Push(const bot_target_t *new_target)
{
    if (new_target == NULL) return;

    SYS_ENTER_CRITICAL();
    memcpy(&s_bricsbot_target, new_target, sizeof(bot_target_t));
    SYS_EXIT_CRITICAL();
}
