#include <string.h>
#include "FreeRTOS.h"
#include "driver_hydrocore.h"

#pragma pack(push, 1)
typedef struct {
    float target_roll;
    float target_pitch;
    float target_yaw;
    float target_depth;
} Bricsbot_MotionControl_Cmd_t;
#pragma pack(pop)

Bricsbot_MotionControl_Cmd_t g_motion_cmd = {0};

static void On_Motion_Ctrl_Received(const uint8_t *payload, uint16_t len){
    if(len!=sizeof(Bricsbot_MotionControl_Cmd_t)){
        return;
    }

    SYS_ENTER_CRITICAL();
    memcpy(&g_motion_cmd, payload, len);
    SYS_EXIT_CRITICAL();

    // 释放一个信号量，立刻使能 PID 任务去解算最新姿态
    // extern SemaphoreHandle_t s_pid_sync_sem;
    // xSemaphoreGive(s_pid_sync_sem);
}

void App_Control_Init(void){
    Driver_Protocol_Register(DATA_TYPE_THRUSTER, On_Motion_Ctrl_Received);
}