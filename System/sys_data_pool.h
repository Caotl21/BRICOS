#ifndef __BOT_DATA_POOL_H
#define __BOT_DATA_POOL_H

#include <stdint.h>
#include <stdbool.h>

#define PARAM_ID_ROLL       0
#define PARAM_ID_PITCH      1
#define PARAM_ID_YAW        2
#define PARAM_ID_DEPTH      3

// ============================================================================
//  状态池 (State) - 包含所有传感器的高频/低频融合数据
// ============================================================================
typedef struct {
    // --- 高频状态 (100Hz IMU) ---
    float roll;           // 横滚角 (度)
    float pitch;          // 俯仰角 (度)
    float yaw;            // 偏航角 (度)
    float gyro_x;         // X轴角速度 (度/秒) 
    float gyro_y;         // Y轴角速度 (度/秒) 
    float gyro_z;         // Z轴角速度 (度/秒) 
    float vel_x;          // X轴线速度 (米/秒) 
    float vel_y;          // Y轴线速度 (米/秒) 
    float vel_z;          // Z轴线速度 (米/秒)
    float acc_x;          // X轴加速度 (米/秒²)
    float acc_y;          // Y轴加速度 (米/秒²)
    float acc_z;          // Z轴加速度 (米/秒²)

    // --- 中频状态 (10Hz MS5837) ---
    float depth_m;        // 当前深度 (米)
    float water_temp_c;   // 外水温 (摄氏度)
    
    // --- 低频环境与系统状态 (1~2Hz) ---
    float cabin_temp_c;   // 舱内温度 (DHT11)
    float cabin_humi;     // 舱内湿度 (DHT11)
    float bat_voltage_v;  // 电池电压 (ADC)
    float bat_current_a;  // 电池总电流 (ADC)
    
    // --- 报警标志 ---
    bool is_leak_detected; // 是否漏水
    bool is_imu_error;     // IMU是否异常
} bot_state_t;

// ============================================================================
// 目标池 (Target) - 包含高速下发的期望姿态、手动摇杆量、执行机构指令
// ============================================================================
// MANUAL 模式专属包 (16字节)，直接映射为推进器推力 纯开环
typedef struct {
    float surge;
    float sway;
    float heave;
    float yaw_cmd;
} payload_manual_t;  // 16 字节

// STABILIZE 模式专属包 开环闭环混合
typedef struct {
    float surge;        // X轴开环推力
    float sway;         // Y轴开环推力
    float target_depth; // 闭环目标深度
    float target_yaw;   // 闭环目标航向
    // roll 和 pitch 默认闭环到 0，可以不用下发，进一步省带宽
} payload_stabilize_t;

typedef struct {
    uint8_t target_mode;

    union {
        payload_manual_t    manual_cmd;
        payload_stabilize_t stab_cmd;
    } cmd;
    
    // --- 附加外设目标 ---
    uint8_t servo_angle;  // 机械手舵机角度 (0-180)
    uint8_t light1_pwm;   // 探照灯1 亮度 (0-100%)
    uint8_t light2_pwm;   // 探照灯2 亮度 (0-100%)
} bot_target_t;

// ============================================================================
// 参数池 (Params) - 包含运行模式、PID参数等，由串口 4 慢速配置或 Flash 初始加载
// ============================================================================
typedef struct {
    float kp;
    float ki;
    float kd;
    float max_out;        // 积分限幅/输出限幅
} pid_param_t;

typedef enum {
    SYS_MODE_STANDBY         = 0, // 待机/低功耗 (推进器断电)
    SYS_MODE_ACTIVE_DISARMED = 1, // 正常工作但加锁 (传感器全开，推力输出锁定为 1500中位)
    SYS_MODE_MOTION_ARMED    = 2  // 运动解锁 (推进器可以真正转动)
} bot_sys_mode_e;

typedef enum {
    MOTION_STATE_MANUAL    = 0, // 纯手动 (摇杆直接映射推力)
    MOTION_STATE_STABILIZE = 1, // 定深定向自稳 (摇杆映射为期望角度和期望深度，走 PID)
    MOTION_STATE_AUTO      = 2  // 自主导航 (上位机下发航点或速度矢量)
} bot_run_mode_e;

typedef struct {
    bot_run_mode_e motion_mode;  // 当前机器人运动模式
    bot_sys_mode_e sys_mode;     // 当前系统状态 (待机/加锁/解锁)
    
    // --- PID 参数矩阵 ---
    pid_param_t pid_roll;
    pid_param_t pid_pitch;
    pid_param_t pid_yaw;
    pid_param_t pid_depth;
    
    // --- 安全保护参数 ---
    float failsafe_max_depth;    // 最大允许下潜深度
    float failsafe_low_voltage;  // 低压报警线
} bot_params_t;


// ============================================================================
// 全局 API 接口 (在 bot_data_pool.c 中实现临界区保护)
// ============================================================================

// 初始化数据池 (赋初值、从Flash读取PID等)
void Bot_Data_Pool_Init(void);

// ---------------------------------------------------------
// [拉取] Getter API
// ---------------------------------------------------------
void Bot_State_Pull(bot_state_t *out_state);
void Bot_Target_Pull(bot_target_t *out_target);
void Bot_Params_Pull(bot_params_t *out_params);

// ---------------------------------------------------------
// [推送] Setter API
// ---------------------------------------------------------

// 高频更新：仅由 Task_IMU 调用，只刷新姿态，不碰深度和温度
void Bot_State_Push_IMU(float r, float p, float y, float gx, float gy, float gz);

// 中低频更新：由 Task_env_sensing 调用
void Bot_State_Push_DepthTemp(float depth, float water_temp);
void Bot_State_Push_CabinEnv(float temp, float humi, bool leak);
void Bot_State_Push_Power(float vol, float cur);

// 目标更新：由 Task_Comm_RT 接收到指令后调用
void Bot_Target_Push_All(const bot_target_t *new_target);

// 参数更新：由 Task_Comm_NRT 接收到调参指令后调用
void Bot_Params_Push_PID(uint8_t pid_id, float p, float i, float d);
void Bot_Params_Set_Mode(bot_run_mode_e mode);

// ---------------------------------------------------------
// [切换] change mode API
// ---------------------------------------------------------

// API: 尝试切换系统模式 (加锁/解锁)
bool Bot_Params_Request_SysMode(bot_sys_mode_e requested_mode);

// API: 尝试切换运动状态 (手动/自稳/自动)
bool Bot_Params_Request_MotionState(bot_run_mode_e requested_state);

#endif // __BOT_DATA_POOL_H

