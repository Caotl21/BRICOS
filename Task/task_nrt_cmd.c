#include <string.h>
#include "driver_hydrocore.h"
#include "bsp_watchdog.h"
#include "bsp_cpu.h"

static void On_Receive_OTA_Cmd(const uint8_t *payload, uint16_t len){
    if(len == 4 && payload[0] == 0xDE && payload[1] == 0xAD && payload[2] == 0xBE && payload[3] == 0xEF){
        // 收到特定 OTA 命令，执行相关操作
        // 这里可以设置一个全局标志位，或者直接调用 OTA 模块的函数来启动升级流程
        bsp_wdg_feed(); 
        bsp_cpu_reset();
    }
}

void App_NRT_Control_Init(void){
    Driver_Protocol_Register(DATA_TYPE_OTA, On_Receive_OTA_Cmd);
}

