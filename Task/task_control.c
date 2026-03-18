#include "bot_data_pool.h"
#include "pid_algo.h"
#include "driver_thruster.h"

void Task_Control(void *pvParameters) 
{
    bot_state_t  local_state;
    bot_target_t local_target;
    bot_params_t local_params;
    
    // 6 个推进器的期望推力输出缓存 (-100% ~ 100%)
    float thrust_out[6] = {0}; 
    
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

                switch (local_params.motion_state) 
                {
                    case MOTION_STATE_MANUAL:
                        // [纯手动模式]：直接把摇杆量扔给推力分配矩阵
                        Thruster_Allocator(
                            local_target.manual_thrust_x,  // 前后
                            local_target.manual_thrust_y,  // 左右
                            local_target.manual_thrust_z,  // 上下
                            local_target.target_roll,      // 在手动模式下，这些其实是摇杆的原始 Roll 量
                            local_target.target_pitch, 
                            local_target.target_yaw,
                            thrust_out
                        );
                        break;
                        
                    case MOTION_STATE_STABILIZE:
                        // [自稳定深模式]：完整的串级 PID 控制
                        // 计算姿态 PID (期望角度 vs 实际角度 -> 输出纠正推力)
                        float comp_roll  = PID_Calculate(&local_params.pid_roll,  local_target.target_roll,  local_state.roll);
                        float comp_pitch = PID_Calculate(&local_params.pid_pitch, local_target.target_pitch, local_state.pitch);
                        float comp_yaw   = PID_Calculate(&local_params.pid_yaw,   local_target.target_yaw,   local_state.yaw);
                        
                        // 计算深度 PID
                        float comp_z     = PID_Calculate(&local_params.pid_depth, local_target.target_depth, local_state.depth_m);
                        
                        // 将手动平移摇杆量与 PID 补偿量融合，送入推力分配矩阵！
                        Thruster_Allocator(
                            local_target.manual_thrust_x,  // X、Y 轴可能还是手动的
                            local_target.manual_thrust_y, 
                            comp_z,                        // Z 轴被深度 PID 接管！
                            comp_roll,                     // 姿态被姿态 PID 接管！
                            comp_pitch,
                            comp_yaw,
                            thrust_out
                        );
                        break;
                        
                    case MOTION_STATE_AUTO:
                        // [自主导航模式]：由轨迹规划算法生成 target_roll/pitch/depth
                        // 逻辑类似于 STABILIZE，但 target 数据不是操作手给的，而是算法生成的
                        // Run_Trajectory_Planner(&local_target, &local_state);
                        // ... 调用 PID ...
                        break;
                }
                
                // 【核心执行】：将推力分配矩阵算出的 6 个浮点数，转换为 6 个真实的 PWM 波！
                Thruster_Apply_Outputs(thrust_out);
                break;
        }

        // 绝对延时，保证 100Hz 的严格周期
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(10));
    }
}