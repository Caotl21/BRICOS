#include <string.h>

#include "bsp_watchdog.h"
#include "bsp_cpu.h"

#include "driver_hydrocore.h"
#include "driver_param.h"
#include "sys_data_pool.h"
#include "sys_port.h"
#include "sys_boot_flag.h"
#include "sys_log.h"

static void On_Receive_OTA_Cmd(const uint8_t *payload, uint16_t len){
    if(len == 4 && payload[0] == 0xDE && payload[1] == 0xAD && payload[2] == 0xBE && payload[3] == 0xEF){      
        Sys_BootFlag_RequestEnterBootloader();

        // bsp_wdg_feed(); 
        bsp_cpu_reset();
    }
}

static void On_Receive_Set_PID_Param_Cmd(const uint8_t *payload, uint16_t len){
    if(len != sizeof(bot_params_t)){
        return;
    }

    bot_params_t new_params;
    memcpy(&new_params, payload, sizeof(bot_params_t));

    if (new_params.pid_roll.kp < 0 || new_params.pid_pitch.kp < 0 || new_params.pid_yaw.kp < 0 || new_params.pid_depth.kp < 0) {
        return;
    }

    // 将新的参数写入全局数据池，供控制任务读取
    Bot_Params_Push_PID(PARAM_ID_ROLL, new_params.pid_roll.kp, new_params.pid_roll.ki, new_params.pid_roll.kd);
    Bot_Params_Push_PID(PARAM_ID_PITCH, new_params.pid_pitch.kp, new_params.pid_pitch.ki, new_params.pid_pitch.kd);
    Bot_Params_Push_PID(PARAM_ID_YAW, new_params.pid_yaw.kp, new_params.pid_yaw.ki, new_params.pid_yaw.kd);
    Bot_Params_Push_PID(PARAM_ID_DEPTH, new_params.pid_depth.kp, new_params.pid_depth.ki, new_params.pid_depth.kd);
    Driver_PidParam_Save(&new_params);

    bsp_cpu_reset();
}

void Task_NRT_Cmd_Init(void){
    Driver_Protocol_Register(DATA_TYPE_OTA, On_Receive_OTA_Cmd);
    Driver_Protocol_Register(DATA_TYPE_SET_PID_PARAM, On_Receive_Set_PID_Param_Cmd);
}

