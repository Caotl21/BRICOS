#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "driver_hydrocore.h"
#include "sys_data_pool.h"
#include "sys_port.h"

static uint8_t xor_checksum(const uint8_t *data, uint16_t len) {
    uint8_t c = 0;
    uint16_t i;
    for (i = 0; i < len; i++) c ^= data[i];
    return c;
}

static void On_Motion_Ctrl_Received(const uint8_t *payload, uint16_t len){
    if(len!=sizeof(bot_target_t)){
        return;
    }

    Bot_Target_Push((const bot_target_t *)payload);

    // 释放一个信号量，立刻使能 task_control 任务去解算最新姿态
    Driver_Protocol_SignalMotionCtrlUpdated();
}

void Task_RT_Cmd_Init(void){
    Driver_Protocol_Register(DATA_TYPE_THRUSTER, On_Motion_Ctrl_Received);
}
