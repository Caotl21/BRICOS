#include "sys_data_pool.h"
#include "sys_pid_algo.h"
#include "driver_thruster.h"
#include "FreeRTOS.h"
#include "sys_log.h"

//控制器实例
Cascade_PID_t pid_roll, pid_pitch, pid_yaw;
PID_Controller_t pid_depth;

static void Normalize_Thruster_Outputs(float *thruster_pwm, uint8_t count, float max_output)
{
    float max_pwm = 0.0f;
    for (uint8_t i = 0; i < count; i++)
    {
        if (thruster_pwm[i] > max_pwm) max_pwm = thruster_pwm[i];
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

static void TAM_Mixer(Bot_Wrench_t *wrench_out, float *thruster_pwm)
{
    // 简单的六轴全向推进器推力分配矩阵
    // 水平推进器分配
    thruster_pwm[0] = wrench_out->force_x - wrench_out->force_y + wrench_out->force_z - wrench_out->torque_x + wrench_out->torque_y - wrench_out->torque_z; // Thruster 1
    thruster_pwm[1] = wrench_out->force_x + wrench_out->force_y + wrench_out->force_z - wrench_out->torque_x - wrench_out->torque_y + wrench_out->torque_z; // Thruster 2
    thruster_pwm[2] = wrench_out->force_x - wrench_out->force_y + wrench_out->force_z + wrench_out->torque_x + wrench_out->torque_y + wrench_out->torque_z; // Thruster 3
    thruster_pwm[3] = wrench_out->force_x + wrench_out->force_y + wrench_out->force_z + wrench_out->torque_x - wrench_out->torque_y - wrench_out->torque_z; // Thruster 4
    // 垂直推进器分配
    thruster_pwm[4] = wrench_out->force_x - wrench_out->force_y - wrench_out->force_z + wrench_out->torque_x - wrench_out->torque_y - wrench_out->torque_z; // Thruster 5
    thruster_pwm[5] = wrench_out->force_x + wrench_out->force_y - wrench_out->force_z + wrench_out->torque_x + wrench_out->torque_y - wrench_out->torque_z; // Thruster 6

    Normalize_Thruster_Outputs(&thruster_pwm[0], 4, 100.0f);
    Normalize_Thruster_Outputs(&thruster_pwm[4], 2, 100.0f);
}

void Task_Control(void *pvParameters) 
{
    bot_state_t  local_state;
    bot_target_t local_target;
    bot_params_t local_params;

    Bot_Wrench_t wrench_out;
    
    float thruster_pwm[THRUSTER_COUNT] = {0}; // 最终输出到电调的 PWM 波数组
    
    TickType_t xLastWakeTime = xTaskGetTickCount();

    for(;;) 
    {
        // 获取最新全局快照
        Bot_State_Pull(&local_state);
        Bot_Target_Pull(&local_target);
        Bot_Params_Pull(&local_params);
        
        switch (local_params.sys_mode) 
        {
            case SYS_MODE_STANDBY:
                // 待机模式：低功耗模式
                //Thruster_Stop_All();
                break;
                
            case SYS_MODE_ACTIVE_DISARMED:
                // 加锁模式：解算 PID，但输出被强制拦截为 0，推进器不转动
                Thruster_Set_Idle(); // 发送 1500us 中位信号
                break;
                
            case SYS_MODE_MOTION_ARMED:

                if (local_target.target_mode != local_params.motion_mode)
                {
                    // 目标模式和当前运动模式不匹配，安全起见先停机
                    Thruster_Set_Idle();
                    LOG_ERROR("Motion mode mismatch!");
                    break;
                }
                else
                {
                    switch (local_params.motion_mode) 
                    {
                        case MOTION_STATE_MANUAL:
                            // [纯手动模式]：直接把摇杆量扔给推力分配矩阵
                            
                            break;
                            
                        case MOTION_STATE_STABILIZE:
                            // [自稳定深模式]：完整的串级 PID 控制

                            // Roll 和 Pitch 的控制逻辑 -- 维持稳定
                            float target_roll_rate  = PID_Calc(&pid_roll.outer,  local_target.target_roll,  local_state.roll);
                            wrench_out.torque_x     = PID_Calc(&pid_roll.inner,  target_roll_rate,    local_state.gyro_x);
            
                            float target_pitch_rate = PID_Calc(&pid_pitch.outer, local_target.target_pitch, local_state.pitch);
                            wrench_out.torque_y     = PID_Calc(&pid_pitch.inner, target_pitch_rate,   local_state.gyro_y);
                            
                            // Yaw 的控制逻辑 -- 定向控制
                            float target_yaw_rate   = PID_Calc(&pid_yaw.outer,   local_target.target_yaw,   local_state.yaw);
                            wrench_out.torque_z     = PID_Calc(&pid_yaw.inner,   target_yaw_rate,     local_state.gyro_z);
                            
                            // Depth 的控制逻辑 -- 定深控制
                            wrench_out.force_z      = PID_Calc(&pid_depth, local_target.target_depth, local_state.depth_m);

                            // X/Y 方向的平移控制逻辑 -- 目前简单映射为摇杆量开环
                            wrench_out.force_x      = local_target.manual_thrust_x;
                            wrench_out.force_y      = local_target.manual_thrust_y;
                            
                            break;
                            
                        case MOTION_STATE_AUTO:
                            // [自主导航模式]：由轨迹规划算法生成 target_roll/pitch/depth
                            // 逻辑类似于 STABILIZE，但 target 数据不是操作手给的，而是算法生成的
                            // Run_Trajectory_Planner(&local_target, &local_state);
                            // ... 调用 PID ...
                            break;
                    }
                }
                
                
                TAM_Mixer(&wrench_out, thruster_pwm);
                break;
        }

        // 输出到电调
        for (int i = 0; i < THRUSTER_COUNT; i++) {
            Driver_Thruster_SetSpeed((bsp_pwm_ch_t)(BSP_PWM_THRUSTER_1 + i), thruster_pwm[i]);
        }

        // 绝对延时，保证 100Hz 的严格周期
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(10));
    }
}