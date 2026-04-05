#ifndef __BOT_DATA_POOL_H
#define __BOT_DATA_POOL_H

#include <stdbool.h>
#include <stdint.h>

#include "sys_pid_algo.h"

/* ============================================================================
 * 参数槽位定义（用于参数下发时标识控制器）
 * ============================================================================ */
#define PARAM_ID_ROLL       0
#define PARAM_ID_PITCH      1
#define PARAM_ID_YAW        2
#define PARAM_ID_DEPTH      3

/* ============================================================================
 * 任务监控 ID（用于心跳看门狗）
 * ============================================================================ */
typedef enum {
    TASK_ID_CONTROL = 0,  /* 控制任务 */
    TASK_ID_IMU,          /* IMU 采集任务 */
    TASK_ID_MS5837,       /* 深度传感器任务 */
    TASK_ID_POWER,        /* 电源采样任务 */
    TASK_ID_DHT11,        /* 舱内温湿度任务 */
    TASK_ID_MONITOR,      /* 监控任务 */
    MAX_MONITOR_TASKS
} monitor_task_id_t;

/* ============================================================================
 * 状态池（State）：机器人本体姿态/速度状态
 * 说明：该结构以控制环为主，典型由高频任务持续更新。
 * ============================================================================ */
typedef struct {
    float Quater[4];  /* 姿态四元数 q0~q3 */

    float gyro_x;     /* X 轴角速度（deg/s） */
    float gyro_y;     /* Y 轴角速度（deg/s） */
    float gyro_z;     /* Z 轴角速度（deg/s） */

    float vel_x;      /* X 轴线速度（m/s） */
    float vel_y;      /* Y 轴线速度（m/s） */
    float vel_z;      /* Z 轴线速度（m/s） */

    float acc_x;      /* X 轴加速度（m/s^2） */
    float acc_y;      /* Y 轴加速度（m/s^2） */
    float acc_z;      /* Z 轴加速度（m/s^2） */

    float depth_m;    /* 深度（m） */
} bot_body_state_t;

/* 欧拉角快照结构（按需使用，不直接入池） */
typedef struct {
    float roll;
    float pitch;
    float yaw;
} bot_body_euler_t;

/* ============================================================================
 * 系统状态池（Sys State）：环境量/电源量/健康状态
 * ============================================================================ */
typedef struct {
    float water_temp_c;   /* 水温（℃） */
    float cabin_temp_c;   /* 舱内温度（℃） */
    float cabin_humi;     /* 舱内湿度（%） */

    float bat_voltage_v;  /* 电池电压（V） */
    float bat_current_a;  /* 电池电流（A） */
    float chip_temp;      /* 芯片温度（℃） */
    float cpu_usage;      /* CPU 占用率（%） */

    bool is_leak_detected;/* 漏水检测标志 */
    bool is_imu_error;    /* IMU 异常标志 */
} bot_sys_state_t;

/* 执行机构状态（舵机/灯光等） */
typedef struct {
    uint8_t servo_angle;  /* 舵机角度（0~180） */
    uint8_t light1_pwm;   /* 灯 1 PWM（0~100） */
    uint8_t light2_pwm;   /* 灯 2 PWM（0~100） */
} bot_actuator_state_t;

/* ============================================================================
 * 目标池（Target）：上位机下发的运动目标
 * 说明：target_mode 决定 union 中有效载荷解释方式。
 * ============================================================================ */

/* MANUAL 模式：直接推力/力矩指令 */
typedef struct {
    float surge;
    float sway;
    float heave;
    float yaw_cmd;
} payload_manual_t;

/* STABILIZE/AUTO 模式：姿态与深度目标 */
typedef struct {
    float surge;
    float sway;
    float target_depth;
    float target_yaw;
} payload_stabilize_t;

typedef struct {
    uint8_t target_mode;  /* 与 motion mode 枚举值对齐 */

    union {
        payload_manual_t manual_cmd;
        payload_stabilize_t stab_cmd;
    } cmd;
} bot_target_t;

/* ============================================================================
 * TAM 参数：推力分配矩阵
 * 行 = 推进器，列 = 6 DoF（Surge/Sway/Heave/Roll/Pitch/Yaw）
 * ============================================================================ */
#define TAM_MAX_THRUSTERS   8
#define TAM_MAX_DOF         6

typedef struct {
    uint8_t active_thrusters;                     /* 当前启用推进器数量 */
    uint8_t pad[3];                               /* 对齐保留 */
    float matrix[TAM_MAX_THRUSTERS][TAM_MAX_DOF];/* 推力分配矩阵 */
} bot_tam_t;

/* ============================================================================
 * 参数池（Params）：控制参数与安全阈值
 * 注意：系统模式/运动模式已迁移到 sys_mode_manager 管理，不在此结构中持有。
 * ============================================================================ */
typedef struct {
    Cascade_PID_t pid_roll;
    Cascade_PID_t pid_pitch;
    Cascade_PID_t pid_yaw;
    PID_Controller_t pid_depth;

    float failsafe_max_depth;   /* 最大允许深度阈值（m） */
    float failsafe_low_voltage; /* 低压阈值（V） */

    bot_tam_t tam_config;       /* 推力分配配置 */
} bot_params_t;

/* ============================================================================
 * 全局数据池 API（内部实现需保证临界区保护）
 * ============================================================================ */

/* 初始化数据池（默认值 + Flash 参数加载） */
void Bot_Data_Pool_Init(void);

/* ------------------------------ Pull 接口 ---------------------------------- */
void Bot_State_Pull(bot_body_state_t *out_state);
void Bot_Sys_State_Pull(bot_sys_state_t *out_state);
void Bot_Target_Pull(bot_target_t *out_target);
void Bot_Params_Pull(bot_params_t *out_params);
void Bot_State_LeakStatus_Pull(bool *out_is_leaking);
void Bot_Actuator_Pull(bot_actuator_state_t *out_state);

/* ------------------------------ Push 接口 ---------------------------------- */
void Bot_State_Push_IMU(bot_body_state_t *imu_data);
void Bot_State_Push_DepthTemp(float depth, float water_temp);
void Bot_State_Push_CabinEnv(float temp, float humi, bool leak);
void Bot_State_Push_Power(float vol, float cur);
void Bot_State_Push_SysStatus(float cpu_usage, float chip_temp);

void Bot_Target_Push(const bot_target_t *new_target);

/* 兼容接口：当前仓内暂未实现，保留声明供后续按需扩展。 */
void Bot_Params_Push_PID(uint8_t pid_id, float p, float i, float d);
void Bot_Params_Push_Servo(uint8_t angle);
void Bot_Params_Push_Light(uint8_t light1_pwm, uint8_t light2_pwm);

/* --------------------------- 任务心跳监控接口 ------------------------------- */
void Bot_Task_CheckIn_Monitor(monitor_task_id_t task_id);
void Bot_Task_LastTick_Pull(uint32_t *out_ticks, uint8_t len);

#endif // __BOT_DATA_POOL_H

