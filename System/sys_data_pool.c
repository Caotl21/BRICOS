#include "sys_data_pool.h"
#include "sys_port.h"
#include "sys_log.h"
#include <string.h>
#include "driver_param.h"

static bot_body_state_t       s_bricsbot_state;
static bot_sys_state_t        s_bricsbot_sys_state;
static bot_actuator_state_t   s_bricsbot_actuator_target;
static bot_target_t           s_bricsbot_target;
static bot_params_t           s_bricsbot_params;

// 数据池初始化 在RTOS调度前启动
void Bot_Data_Pool_Init(void)
{
    memset(&s_bricsbot_state, 0, sizeof(s_bricsbot_state));
    memset(&s_bricsbot_sys_state, 0, sizeof(s_bricsbot_sys_state));
    memset(&s_bricsbot_actuator_target, 0, sizeof(s_bricsbot_actuator_target));
    memset(&s_bricsbot_target, 0, sizeof(s_bricsbot_target));
    memset(&s_bricsbot_params, 0, sizeof(s_bricsbot_params));

    s_bricsbot_actuator_target.servo_angle = 90; // 默认舵机角度为中位

    s_bricsbot_params.motion_mode = MOTION_STATE_MANUAL;
    s_bricsbot_params.sys_mode = SYS_MODE_ACTIVE_DISARMED;

    // 从flash读取PID参数
    Driver_PidParam_FillDefault(&s_bricsbot_params);
    if (!Driver_PidParam_Load(&s_bricsbot_params))
    {
        (void)Driver_PidParam_Save(&s_bricsbot_params);
    }
    s_bricsbot_params.failsafe_max_depth = 10.0f; // 默认最大下潜深度10米
    s_bricsbot_params.failsafe_low_voltage = 10.0f; // 默认低压报警线10V
}


// --- Getter API 实现 ---
void Bot_State_Pull(bot_body_state_t *out_state) {
    if (out_state == NULL) return;

    SYS_ENTER_CRITICAL();
    memcpy(out_state, &s_bricsbot_state, sizeof(bot_body_state_t));
    SYS_EXIT_CRITICAL();
}

void Bot_Sys_State_Pull(bot_sys_state_t *out_state) {
    if (out_state == NULL) return;

    SYS_ENTER_CRITICAL();
    memcpy(out_state, &s_bricsbot_sys_state, sizeof(bot_sys_state_t));
    SYS_EXIT_CRITICAL();
}

void Bot_Target_Pull(bot_target_t *out_target) {
    if (out_target == NULL) return;

    SYS_ENTER_CRITICAL();
    memcpy(out_target, &s_bricsbot_target, sizeof(bot_target_t));
    SYS_EXIT_CRITICAL();
}

void Bot_Params_Pull(bot_params_t *out_params) {
    if (out_params == NULL) return;

    SYS_ENTER_CRITICAL();
    memcpy(out_params, &s_bricsbot_params, sizeof(bot_params_t));
    SYS_EXIT_CRITICAL();
}

void Bot_MODE_Pull(bot_params_t *out_params) {
    if (out_params == NULL) return;

    SYS_ENTER_CRITICAL();
    out_params->motion_mode = s_bricsbot_params.motion_mode;
    out_params->sys_mode = s_bricsbot_params.sys_mode;
    SYS_EXIT_CRITICAL();
}

void Bot_State_LeakStatus_Pull(bool *out_is_leaking)
{
    if (out_is_leaking == NULL) {
        return; 
    }
    
    taskENTER_CRITICAL(); 
    memcpy(out_is_leaking, &s_bricsbot_sys_state.is_leak_detected, sizeof(bool));
    taskEXIT_CRITICAL();  
}

// --- Setter API 实现 ---
void Bot_State_Push_IMU(bot_body_state_t *imu_data) {
    SYS_ENTER_CRITICAL();
    memcpy(s_bricsbot_state.Quater, imu_data->Quater, sizeof(float) * 4);
    s_bricsbot_state.gyro_x = imu_data->gyro_x;
    s_bricsbot_state.gyro_y = imu_data->gyro_y;
    s_bricsbot_state.gyro_z = imu_data->gyro_z;
    s_bricsbot_state.acc_x = imu_data->acc_x;
    s_bricsbot_state.acc_y = imu_data->acc_y;
    s_bricsbot_state.acc_z = imu_data->acc_z;
    SYS_EXIT_CRITICAL();
}

void Bot_State_Push_DepthTemp(float depth, float water_temp) {
    SYS_ENTER_CRITICAL();
    s_bricsbot_state.depth_m = depth;
    s_bricsbot_sys_state.water_temp_c = water_temp;
    SYS_EXIT_CRITICAL();
}

void Bot_State_Push_CabinEnv(float temp, float humi, bool leak) {
    SYS_ENTER_CRITICAL();
    s_bricsbot_sys_state.cabin_temp_c = temp;
    s_bricsbot_sys_state.cabin_humi = humi;
    s_bricsbot_sys_state.is_leak_detected = leak;
    SYS_EXIT_CRITICAL();
}

void Bot_State_Push_Power(float vol, float cur) {
    SYS_ENTER_CRITICAL();
    s_bricsbot_sys_state.bat_voltage_v = vol;
    s_bricsbot_sys_state.bat_current_a = cur;
    SYS_EXIT_CRITICAL();
}

void Bot_State_Push_Servo(uint8_t angle) {
    if (angle > 180) angle = 180; // 限幅保护

    SYS_ENTER_CRITICAL();
    s_bricsbot_actuator_target.servo_angle = angle;
    SYS_EXIT_CRITICAL();
}

void Bot_State_Push_Light(uint8_t light1_pwm, uint8_t light2_pwm) {
    SYS_ENTER_CRITICAL();
    s_bricsbot_actuator_target.light1_pwm = light1_pwm;
    s_bricsbot_actuator_target.light2_pwm = light2_pwm;
    SYS_EXIT_CRITICAL();
}

void Bot_State_Push_SysStatus(float cpu_usage, float chip_temp) {
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

// --- 看门狗打卡 ---
void Bot_Task_CheckIn_Monitor(monitor_task_id_t task_id)
{
    if (task_id >= MAX_MONITOR_TASKS) {
        LOG_ERROR("Invalid task ID %u in Bot_Task_CheckIn_Monitor", task_id);
        return;
    }
    SYS_ENTER_CRITICAL();
    s_bricsbot_sys_state.task_last_tick[task_id] = xTaskGetTickCount(); // 监控任务心跳
    SYS_EXIT_CRITICAL();
}

// --- 控制与配置更新系列 ---
void Bot_Target_Push(const bot_target_t *new_target) {
    if (new_target == NULL) return;

    SYS_ENTER_CRITICAL();
    memcpy(&s_bricsbot_target, new_target, sizeof(bot_target_t));
    SYS_EXIT_CRITICAL();
}


// --- 模式切换 API 实现 ---

// 尝试切换系统模式 (加锁/解锁)
bool Bot_Params_Request_SysMode(bot_sys_mode_e requested_mode) 
{
    SYS_ENTER_CRITICAL();
    
    // 请求解锁 (ARMED)，进行安全检查
    if (requested_mode == SYS_MODE_MOTION_ARMED) {
        // 检查是否漏水
        if (s_bricsbot_sys_state.is_leak_detected) {
            SYS_EXIT_CRITICAL();
            return false;
        }
        // 检查 IMU 是否健康
        if (s_bricsbot_sys_state.is_imu_error) {
            SYS_EXIT_CRITICAL();
            return false;
        }
        // 检查电压是否过低
        if (s_bricsbot_sys_state.bat_voltage_v < s_bricsbot_params.failsafe_low_voltage) {
            SYS_EXIT_CRITICAL();
            return false;
        }

        // 可以添加更多安全检查，如深度是否过深等
    }
    
    // 检查通过，或者请求的是加锁/待机(这些总是允许的)
    s_bricsbot_params.sys_mode = requested_mode;
    
    SYS_EXIT_CRITICAL();
    return true;
}

// API: 尝试切换运动状态 (手动/自稳/自动)
bool Bot_Params_Request_MotionState(bot_run_mode_e requested_state)
{
    SYS_ENTER_CRITICAL();
    
    // 如果请求切入“定深定向自稳”，必须确保深度计数据是新的
    if (requested_state == MOTION_STATE_STABILIZE || requested_state == MOTION_STATE_AUTO) {
        if(s_bricsbot_sys_state.is_imu_error) { 
            SYS_EXIT_CRITICAL();
            return false;
        }
    }
    
    s_bricsbot_params.motion_mode = requested_state;
    SYS_EXIT_CRITICAL();
    return true;
}
