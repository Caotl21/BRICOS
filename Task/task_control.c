#include "sys_data_pool.h"
#include "sys_pid_algo.h"
#include "driver_thruster.h"
#include "driver_hydrocore.h"
#include "FreeRTOS.h"
#include "task.h"
#include "task_control.h"
#include "sys_log.h"
#include <string.h>

#define TASK_CONTROL_PERIOD_S    (0.01f)

// 控制器实例
Cascade_PID_t pid_roll, pid_pitch, pid_yaw;
PID_Controller_t pid_depth;
TaskHandle_t Control_Task_Handler = NULL;

static float Clamp_Symmetric(float value, float limit)
{
    if (limit <= 0.0f)
    {
        return value;
    }

    if (value > limit)
    {
        return limit;
    }
    if (value < -limit)
    {
        return -limit;
    }

    return value;
}

static float Normalize_Angle_Error(float error_deg)
{
    while (error_deg > 180.0f)
    {
        error_deg -= 360.0f;
    }

    while (error_deg < -180.0f)
    {
        error_deg += 360.0f;
    }

    return error_deg;
}

static void Reset_PID(PID_Controller_t *pid)
{
    if (pid == NULL)
    {
        return;
    }

    pid->error_int = 0.0f;
    pid->error_last = 0.0f;
}

static void Reset_Cascade_PID(Cascade_PID_t *pid)
{
    if (pid == NULL)
    {
        return;
    }

    Reset_PID(&pid->outer);
    Reset_PID(&pid->inner);
}

static void Reset_All_Controllers(void)
{
    Reset_Cascade_PID(&pid_roll);
    Reset_Cascade_PID(&pid_pitch);
    Reset_Cascade_PID(&pid_yaw);
    Reset_PID(&pid_depth);
}

static void Sync_PID_Config(PID_Controller_t *runtime, const PID_Controller_t *config)
{
    if ((runtime == NULL) || (config == NULL))
    {
        return;
    }

    runtime->kp = config->kp;
    runtime->ki = config->ki;
    runtime->kd = config->kd;
    runtime->integral_max = config->integral_max;
    runtime->output_max = config->output_max;
}

static void Sync_Cascade_Config(Cascade_PID_t *runtime, const Cascade_PID_t *config)
{
    if ((runtime == NULL) || (config == NULL))
    {
        return;
    }

    Sync_PID_Config(&runtime->outer, &config->outer);
    Sync_PID_Config(&runtime->inner, &config->inner);
}

static float PID_Update(PID_Controller_t *pid, float target, float measurement, float dt_s, uint8_t wrap_angle)
{
    float error;
    float derivative;
    float output;

    if ((pid == NULL) || (dt_s <= 0.0f))
    {
        return 0.0f;
    }

    error = target - measurement;
    if (wrap_angle)
    {
        error = Normalize_Angle_Error(error);
    }

    pid->error_int += error * dt_s;
    pid->error_int = Clamp_Symmetric(pid->error_int, pid->integral_max);

    derivative = (error - pid->error_last) / dt_s;
    output = (pid->kp * error) + (pid->ki * pid->error_int) + (pid->kd * derivative);
    output = Clamp_Symmetric(output, pid->output_max);

    pid->error_last = error;
    return output;
}

static float Cascade_PID_Update(Cascade_PID_t *pid,
                                float angle_target,
                                float angle_measurement,
                                float rate_measurement,
                                float dt_s,
                                uint8_t wrap_angle)
{
    float target_rate;

    if (pid == NULL)
    {
        return 0.0f;
    }

    target_rate = PID_Update(&pid->outer, angle_target, angle_measurement, dt_s, wrap_angle);
    return PID_Update(&pid->inner, target_rate, rate_measurement, dt_s, 0u);
}

static void Normalize_Thruster_Outputs(float *thruster_pwm, uint8_t count, float max_output)
{
    float max_pwm = 0.0f;
    for (uint8_t i = 0; i < count; i++)
    {
        float abs_pwm = (thruster_pwm[i] >= 0.0f) ? thruster_pwm[i] : -thruster_pwm[i];
        if (abs_pwm > max_pwm) max_pwm = abs_pwm;
    }

    if (max_pwm > max_output)
    {
        float scale = max_output / max_pwm;
        for (uint8_t i = 0; i < count; i++)
        {
            thruster_pwm[i] *= scale;
        }
    }
}

static void TAM_Mixer(Bot_Wrench_t *wrench_out, float *thruster_pwm, bot_tam_t *tam_config)
{
    float wrench_array[TAM_MAX_DOF] = {
        wrench_out->force_x, wrench_out->force_y, wrench_out->force_z,
        wrench_out->torque_x, wrench_out->torque_y, wrench_out->torque_z
    };

    for (int t = 0; t < tam_config->active_thrusters; t++) 
    {
        float total_thrust = 0.0f;
        
        for (int dof = 0; dof < TAM_MAX_DOF; dof++) 
        {
            // matrix[t][dof] 代表第 t 个推进器在第 dof 个自由度上的分配系数
            total_thrust += tam_config->matrix[t][dof] * wrench_array[dof];
        }
        
        thruster_pwm[t] = total_thrust; 
    }
    
    Normalize_Thruster_Outputs(&thruster_pwm[0], 4, 100.0f);
    Normalize_Thruster_Outputs(&thruster_pwm[4], 2, 100.0f);
}

static uint16_t Serialize_Body_Report(uint8_t *buf, const bot_body_state_t *body_state)
{
    uint16_t offset = 0;

    if ((buf == NULL) || (body_state == NULL))
    {
        return 0u;
    }

    memcpy(&buf[offset], &body_state->roll, sizeof(float)); offset += sizeof(float);
    memcpy(&buf[offset], &body_state->pitch, sizeof(float)); offset += sizeof(float);
    memcpy(&buf[offset], &body_state->yaw, sizeof(float)); offset += sizeof(float);
    memcpy(&buf[offset], &body_state->gyro_x, sizeof(float)); offset += sizeof(float);
    memcpy(&buf[offset], &body_state->gyro_y, sizeof(float)); offset += sizeof(float);
    memcpy(&buf[offset], &body_state->gyro_z, sizeof(float)); offset += sizeof(float);
    memcpy(&buf[offset], &body_state->vel_x, sizeof(float)); offset += sizeof(float);
    memcpy(&buf[offset], &body_state->vel_y, sizeof(float)); offset += sizeof(float);
    memcpy(&buf[offset], &body_state->vel_z, sizeof(float)); offset += sizeof(float);
    memcpy(&buf[offset], &body_state->acc_x, sizeof(float)); offset += sizeof(float);
    memcpy(&buf[offset], &body_state->acc_y, sizeof(float)); offset += sizeof(float);
    memcpy(&buf[offset], &body_state->acc_z, sizeof(float)); offset += sizeof(float);
    memcpy(&buf[offset], &body_state->depth_m, sizeof(float)); offset += sizeof(float);

    return offset;
}

static void Report_Body_State_To_OrangePi(const bot_body_state_t *body_state)
{
    uint8_t report_buf[13u * sizeof(float)];
    uint16_t report_len;

    if (body_state == NULL)
    {
        return;
    }

    report_len = Serialize_Body_Report(report_buf, body_state);
    if (report_len == 0u)
    {
        return;
    }

    Driver_Protocol_SendFrame(BSP_UART_OPI_RT,
                              DATA_TYPE_STATE_BODY,
                              report_buf,
                              (uint8_t)report_len,
                              USE_CPU);
}

static void vTask_Control(void *pvParameters) 
{
    bot_params_t *local_params = (bot_params_t *)pvParameters;

    // 严谨的架构师防爆检查
    if (local_params == NULL) {
        vTaskDelete(NULL); // 如果传参失败，直接销毁任务防止死机
    }
    bot_sys_mode_e    last_sys_mode = (bot_sys_mode_e)0xFF;
    bot_run_mode_e    last_motion_mode = (bot_run_mode_e)0xFF;
    bot_body_state_t  local_state;
    bot_target_t      local_target;


    float thruster_pwm[THRUSTER_COUNT] = {0}; // 最终输出到电调的 PWM 波数组

    (void)pvParameters;
    TickType_t xLastWakeTime = xTaskGetTickCount();

    while(1)
    {
        Bot_Wrench_t wrench_out;

        memset(&wrench_out, 0, sizeof(wrench_out));
        memset(thruster_pwm, 0, sizeof(thruster_pwm));

        // 获取最新全局快照
        Bot_State_Pull(&local_state);
        Bot_Target_Pull(&local_target);
        
        if ((local_params->sys_mode != last_sys_mode) || (local_params->motion_mode != last_motion_mode))
        {
            Reset_All_Controllers();
            last_sys_mode = local_params->sys_mode;
            last_motion_mode = local_params->motion_mode;
        }

        Sync_Cascade_Config(&pid_roll, &local_params->pid_roll);
        Sync_Cascade_Config(&pid_pitch, &local_params->pid_pitch);
        Sync_Cascade_Config(&pid_yaw, &local_params->pid_yaw);
        Sync_PID_Config(&pid_depth, &local_params->pid_depth);

        switch (local_params->sys_mode) 
        {
            case SYS_MODE_STANDBY:
                // 待机模式：低功耗模式
                Reset_All_Controllers();
                break;

            case SYS_MODE_ACTIVE_DISARMED:
                // 加锁模式：解算 PID，但输出被强制拦截为 0，推进器不转动
                Reset_All_Controllers();
                Driver_Thruster_Set_Idle(); // 发送 1500us 中位信号
                break;

            case SYS_MODE_MOTION_ARMED:

                if (local_target.target_mode != local_params->motion_mode)
                {
                    // 目标模式和当前运动模式不匹配，安全起见先停机
                    Reset_All_Controllers();
                    Driver_Thruster_Set_Idle();
                    LOG_ERROR("Motion mode mismatch!");
                    break;
                }
                else
                {
                    switch (local_params->motion_mode) 
                    {
                        case MOTION_STATE_MANUAL:
                            // [纯手动模式]：直接把摇杆量扔给推力分配矩阵
                            wrench_out.force_x = local_target.cmd.manual_cmd.surge;
                            wrench_out.force_y = local_target.cmd.manual_cmd.sway;
                            wrench_out.force_z = local_target.cmd.manual_cmd.heave;
                            wrench_out.torque_z = local_target.cmd.manual_cmd.yaw_cmd;
                            break;

                        case MOTION_STATE_STABILIZE:
                            // [自稳定深模式]：串级 PID 控制姿态 + 开环控制平移

                            // Roll 和 Pitch 的控制逻辑 -- 维持稳定 target不需要下发直接为0
                            wrench_out.torque_x = Cascade_PID_Update(&pid_roll,
                                                                     0.0f,
                                                                     local_state.roll,
                                                                     local_state.gyro_x,
                                                                     TASK_CONTROL_PERIOD_S,
                                                                     0u);
                            wrench_out.torque_y = Cascade_PID_Update(&pid_pitch,
                                                                     0.0f,
                                                                     local_state.pitch,
                                                                     local_state.gyro_y,
                                                                     TASK_CONTROL_PERIOD_S,
                                                                     0u);

                            // Yaw 的控制逻辑 -- 定向控制 跟踪target_yaw
                            wrench_out.torque_z = Cascade_PID_Update(&pid_yaw,
                                                                     local_target.cmd.stab_cmd.target_yaw,
                                                                     local_state.yaw,
                                                                     local_state.gyro_z,
                                                                     TASK_CONTROL_PERIOD_S,
                                                                     1u);

                            // Depth 的控制逻辑 -- 定深控制 单环PID控制深度
                            {
                                float target_depth = local_target.cmd.stab_cmd.target_depth;

                                if (target_depth < 0.0f)
                                {
                                    target_depth = 0.0f;
                                }
                                if (target_depth > local_params->failsafe_max_depth)
                                {
                                    target_depth = local_params->failsafe_max_depth;
                                }

                                wrench_out.force_z = PID_Update(&pid_depth,
                                                                target_depth,
                                                                local_state.depth_m,
                                                                TASK_CONTROL_PERIOD_S,
                                                                0u);
                            }

                            // X/Y 方向的平移控制逻辑 -- 目前简单映射为摇杆量开环
                            wrench_out.force_x = local_target.cmd.stab_cmd.surge;
                            wrench_out.force_y = local_target.cmd.stab_cmd.sway;
                            break;

                        case MOTION_STATE_AUTO:
                            // [自主导航模式]：由轨迹规划算法生成 target_roll/pitch/depth
                            // 逻辑类似于 STABILIZE，但 target 数据不是操作手给的，而是算法生成的
                            // Run_Trajectory_Planner(&local_target, &local_state);
                            // ... 调用 PID ...
                            break;
                    }
                }
                
                
                TAM_Mixer(&wrench_out, thruster_pwm, &local_params->tam_config);
                // 输出到电调
                for (int i = 0; i < THRUSTER_COUNT; i++) {
                    Driver_Thruster_SetSpeed((bsp_pwm_ch_t)(BSP_PWM_THRUSTER_1 + i), thruster_pwm[i]);
                }
                break;
        }

        Report_Body_State_To_OrangePi(&local_state);

        Bot_Task_CheckIn_Monitor(TASK_ID_CONTROL);

        // 绝对延时，保证 100Hz 的严格周期
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(10));
    }
}

void Task_Control_Init(void)
{
    //只更新一次
    static bot_params_t  local_params;
    Bot_Params_Pull(&local_params);

    xTaskCreate((TaskFunction_t)vTask_Control,
                (const char *)"Task_Control",
                (uint16_t)CONTROL_STK_SIZE,
                (void *)&local_params,
                (UBaseType_t)CONTROL_TASK_PRIO,
                (TaskHandle_t *)&Control_Task_Handler);
}
