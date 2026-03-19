#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "driver_hydrocore.h"
#include "sys_data_pool.h"
#include "sys_port.h"

typedef struct __attribute__((packed)) {
    uint32_t tick_ms;
    float roll;
    float pitch;
    float yaw;
    float depth_m;
    float bat_v;
    float bat_a;
    uint8_t sys_mode;
    uint8_t motion_mode;
    uint8_t leak;
    uint8_t reserved;
} bricsbot_sensor_rt_t;

static uint8_t xor_checksum(const uint8_t *data, uint16_t len) {
    uint8_t c = 0;
    uint16_t i;
    for (i = 0; i < len; i++) c ^= data[i];
    return c;
}

bot_target_t g_motion_cmd = {0};

static void On_Motion_Ctrl_Received(const uint8_t *payload, uint16_t len){
    if(len!=sizeof(bot_target_t)){
        return;
    }

    SYS_ENTER_CRITICAL();
    memcpy(&g_motion_cmd, payload, len);
    SYS_EXIT_CRITICAL();

    // 释放一个信号量，立刻使能 PID 任务去解算最新姿态
    // extern SemaphoreHandle_t s_pid_sync_sem;
    // xSemaphoreGive(s_pid_sync_sem);
}

void Task_RT_Cmd_Init(void){
    Driver_Protocol_Register(DATA_TYPE_THRUSTER, On_Motion_Ctrl_Received);
}
