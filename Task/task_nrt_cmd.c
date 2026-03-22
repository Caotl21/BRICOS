#include <string.h>

#include "bsp_watchdog.h"
#include "bsp_cpu.h"

#include "driver_hydrocore.h"
#include "driver_param.h"
#include "driver_servo.h"

#include "sys_data_pool.h"
#include "sys_port.h"
#include "sys_boot_flag.h"
#include "sys_log.h"

// 接收OTA升级命令的回调函数
static void On_Receive_OTA_Cmd(const uint8_t *payload, uint16_t len){
    if(len == 4 && payload[0] == 0xDE && payload[1] == 0xAD && payload[2] == 0xBE && payload[3] == 0xEF){      
        Sys_BootFlag_RequestEnterBootloader();

        // bsp_wdg_feed(); 
        bsp_cpu_reset();
    }
}

// 接收设置PID参数命令的回调函数(暂无实现，需要更新参数再写入Flash后重启)
static void On_Receive_Set_PID_Param_Cmd(const uint8_t *payload, uint16_t len){
    

    bsp_cpu_reset();
}

// 接收设置系统模式命令的回调函数(切换待机/加锁/解锁)
static void On_Receive_Sys_Mode_Cmd(const uint8_t *payload, uint16_t len){
    if(len != 1) return;

    bot_sys_mode_e new_mode = (bot_sys_mode_e)payload[0];
    if(new_mode > SYS_MODE_MOTION_ARMED) return;

    Bot_Params_Request_SysMode(new_mode);
}

// 接收设置运动模式命令的回调函数(切换手动/自稳/自主导航)
static void On_Receive_Motion_Mode_Cmd(const uint8_t *payload, uint16_t len){
    if(len != 1) return;

    bot_run_mode_e new_mode = (bot_run_mode_e)payload[0];
    if(new_mode > MOTION_STATE_AUTO) return;

    Bot_Params_Request_MotionState(new_mode);
}

// 接收设置舵机命令的回调函数
static void On_Receive_Servo_Cmd(const uint8_t *payload, uint16_t len){
    if(len != 1) return;

    uint8_t servo_angle = payload[0];

    Driver_Servo_SetAngle(BSP_PWM_SERVO_2, servo_angle); // 相机云台舵机角度 (0-180)
}

// 接收设置探照灯强度命令的回调函数 (暂未实现，后续可以根据协议定义增加)
static void On_Receive_Light_Cmd(const uint8_t *payload, uint16_t len){
    // 解析 payload，设置探照灯亮度
    // 例如：payload[0] = light1_pwm (0-100)，payload[1] = light2_pwm (0-100)
}   

void Task_NRT_Cmd_Init(void){
    Driver_Protocol_Register(DATA_TYPE_OTA, On_Receive_OTA_Cmd);
    Driver_Protocol_Register(DATA_TYPE_SET_PID_PARAM, On_Receive_Set_PID_Param_Cmd);
    Driver_Protocol_Register(DATA_TYPE_SET_SYS_MODE, On_Receive_Sys_Mode_Cmd);
    Driver_Protocol_Register(DATA_TYPE_SET_MOTION_MODE, On_Receive_Motion_Mode_Cmd);
    Driver_Protocol_Register(DATA_TYPE_SET_SERVO, On_Receive_Servo_Cmd);
}

