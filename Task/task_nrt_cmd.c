#include <string.h>

#include "bsp_watchdog.h"
#include "bsp_cpu.h"

#include "driver_hydrocore.h"
#include "driver_param.h"
#include "driver_servo.h"

#include "sys_data_pool.h"
#include "sys_mode_manager.h"
#include "sys_port.h"
#include "sys_boot_flag.h"
#include "sys_log.h"

#include "task_nrt_cmd.h"


// 接收OTA升级命令的回调函数
static void On_Receive_OTA_Cmd(const uint8_t *payload, uint16_t len){
    if(len == 4 && payload[0] == 0xDE && payload[1] == 0xAD && payload[2] == 0xBE && payload[3] == 0xEF){      
        Sys_BootFlag_RequestEnterBootloader();

        Driver_Protocol_SendAck(BSP_UART_OPI_NRT, DATA_TYPE_OTA, ACK_SUCCESS, 0, USE_DMA);
        bsp_cpu_reset();
    } else {
        Driver_Protocol_SendAck(BSP_UART_OPI_NRT, DATA_TYPE_OTA, INVALID_PARAM, 0, USE_DMA);
    }
}


static void prv_extract_pid_params(PID_Controller_t *dest, const uint8_t *src)
{
    float temp_buf[5];
    
    memcpy(temp_buf, src, PAYLOAD_SIZE_PER_PID);
    
    dest->kp           = temp_buf[0];
    dest->ki           = temp_buf[1];
    dest->kd           = temp_buf[2];
    dest->integral_max = temp_buf[3];
    dest->output_max   = temp_buf[4];
}

// 接收设置PID参数命令的回调函数(需要更新参数再写入Flash后重启)
static void On_Receive_Set_PID_Param_Cmd(const uint8_t *payload, uint16_t len){
    
    if (payload == NULL) return;

    // 严格的长度校验
    // 预期：7 个控制器 (Roll外/内, Pitch外/内, Yaw外/内, Depth单) * 每个 20 字节
    uint16_t expected_len = 7 * PAYLOAD_SIZE_PER_PID; // 7 * 20 = 140 字节
    if (len != expected_len) {
        Driver_Protocol_SendAck(BSP_UART_OPI_NRT, DATA_TYPE_SET_PID_PARAM, LENGTH_ERROR, 0, USE_DMA);
        return; // 长度对不上（串口丢包或上位机配错），直接拦截拒绝写入
    }

    // 拉取当前系统参数快照 (保护 TAM 矩阵和 failsafe 参数不被清空)
    bot_params_t temp_params;
    Bot_Params_Pull(&temp_params);

    // 按严格的约定顺序，逐个解析并覆盖 PID 参数
    uint16_t offset = 0;

    // --- Roll 串级 (外环 -> 内环) ---
    prv_extract_pid_params(&temp_params.pid_roll.outer, &payload[offset]);
    offset += PAYLOAD_SIZE_PER_PID;
    prv_extract_pid_params(&temp_params.pid_roll.inner, &payload[offset]);
    offset += PAYLOAD_SIZE_PER_PID;

    // --- Pitch 串级 (外环 -> 内环) ---
    prv_extract_pid_params(&temp_params.pid_pitch.outer, &payload[offset]);
    offset += PAYLOAD_SIZE_PER_PID;
    prv_extract_pid_params(&temp_params.pid_pitch.inner, &payload[offset]);
    offset += PAYLOAD_SIZE_PER_PID;

    // --- Yaw 串级 (外环 -> 内环) ---
    prv_extract_pid_params(&temp_params.pid_yaw.outer, &payload[offset]);
    offset += PAYLOAD_SIZE_PER_PID;
    prv_extract_pid_params(&temp_params.pid_yaw.inner, &payload[offset]);
    offset += PAYLOAD_SIZE_PER_PID;

    // --- Depth 单环 ---
    prv_extract_pid_params(&temp_params.pid_depth, &payload[offset]);
    
    // offset 此时刚好等于 140

    // 将修改后的完整参数包写入 Flash，掉电保存
    Driver_PidParam_Save(&temp_params);

    Driver_Protocol_SendAck(BSP_UART_OPI_NRT, DATA_TYPE_SET_PID_PARAM, ACK_SUCCESS, 0, USE_DMA);

    // 硬复位，让系统重新走一遍完整的开机初始化流程
    // 这样开机时会自动清空 PID 的 I 项累加器 (error_int)，防止带着旧状态起飞炸机
    bsp_cpu_reset();
}

// 接收设置系统模式命令的回调函数(切换待机/加锁/解锁)
static void On_Receive_Sys_Mode_Cmd(const uint8_t *payload, uint16_t len){

    if(len != 1) {
        Driver_Protocol_SendAck(BSP_UART_OPI_NRT, DATA_TYPE_SET_SYS_MODE, LENGTH_ERROR, 0, USE_DMA);
        return;
    }

    bool res;
    bot_sys_mode_e new_mode = (bot_sys_mode_e)payload[0];
    if(new_mode > SYS_MODE_MOTION_ARMED) {
        Driver_Protocol_SendAck(BSP_UART_OPI_NRT, DATA_TYPE_SET_SYS_MODE, INVALID_PARAM, 0, USE_DMA);
        return;
    }

    res = (System_ModeManager_RequestSysMode(new_mode) == SYS_MODE_MGR_OK);
    if (!res) {
        Driver_Protocol_SendAck(BSP_UART_OPI_NRT, DATA_TYPE_SET_SYS_MODE, INVALID_PARAM, 0, USE_DMA);
    } else {
        Driver_Protocol_SendAck(BSP_UART_OPI_NRT, DATA_TYPE_SET_SYS_MODE, ACK_SUCCESS, 0, USE_DMA);
    }
}

// 接收设置运动模式命令的回调函数(切换手动/自稳/自主导航)
static void On_Receive_Motion_Mode_Cmd(const uint8_t *payload, uint16_t len){
    if(len != 1) {
        Driver_Protocol_SendAck(BSP_UART_OPI_NRT, DATA_TYPE_SET_MOTION_MODE, LENGTH_ERROR, 0, USE_DMA);
        return;
    }

    bool res;
    bot_run_mode_e new_mode = (bot_run_mode_e)payload[0];
    if(new_mode > MOTION_STATE_AUTO) {
        Driver_Protocol_SendAck(BSP_UART_OPI_NRT, DATA_TYPE_SET_MOTION_MODE, INVALID_PARAM, 0, USE_DMA);
        return;
    }

    res = (System_ModeManager_RequestMotionMode(new_mode) == SYS_MODE_MGR_OK);
    if (!res) {
        Driver_Protocol_SendAck(BSP_UART_OPI_NRT, DATA_TYPE_SET_MOTION_MODE, INVALID_PARAM, 0, USE_DMA);
    } else {
        Driver_Protocol_SendAck(BSP_UART_OPI_NRT, DATA_TYPE_SET_MOTION_MODE, ACK_SUCCESS, 0, USE_DMA);
    }
}

// 接收设置舵机命令的回调函数
static void On_Receive_Servo_Cmd(const uint8_t *payload, uint16_t len){
    if(len != 1) {
        Driver_Protocol_SendAck(BSP_UART_OPI_NRT, DATA_TYPE_SET_SERVO, LENGTH_ERROR, 0, USE_DMA);
        return;
    }

    uint8_t servo_angle = payload[0];

    Driver_Servo_SetAngle(BSP_PWM_SERVO_2, servo_angle); // 相机云台舵机角度 (0-180)
    Driver_Protocol_SendAck(BSP_UART_OPI_NRT, DATA_TYPE_SET_SERVO, ACK_SUCCESS, 0, USE_DMA);
}

// 接收设置探照灯强度命令的回调函数 (暂未实现，后续可以根据协议定义增加)
static void On_Receive_Light_Cmd(const uint8_t *payload, uint16_t len){
    // 解析 payload，设置探照灯亮度
    // 例如：payload[0] = light1_pwm (0-100)，payload[1] = light2_pwm (0-100)
}   

static void On_Receive_TAM_Cmd(const uint8_t *payload, uint16_t len){

    if (payload == NULL || len < 1) {
        Driver_Protocol_SendAck(BSP_UART_OPI_NRT, DATA_TYPE_SET_TAM, LENGTH_ERROR, 0, USE_DMA);
        return;
    }

    // 取并校验推进器数量
    uint8_t thruster_count = payload[0];
    if (thruster_count == 0 || thruster_count > TAM_MAX_THRUSTERS) {
        Driver_Protocol_SendAck(BSP_UART_OPI_NRT, DATA_TYPE_SET_TAM, INVALID_PARAM, 0, USE_DMA);
        return; 
    }

    // 严格的载荷长度校验
    // 预期长度 = 1字节(数量) + 推进器数量 * 6(自由度) * 4字节(sizeof(float))
    uint16_t expected_len = 1 + (thruster_count * TAM_MAX_DOF * sizeof(float));
    if (len != expected_len) {
        Driver_Protocol_SendAck(BSP_UART_OPI_NRT, DATA_TYPE_SET_TAM, LENGTH_ERROR, 0, USE_DMA);
        return; 
    }

    // ==========================================
    // 核心流转：拉取(Pull) -> 修改(Modify) -> 保存(Save)
    // ==========================================
    
    // 拉取当前系统参数快照
    bot_params_t temp_params;
    Bot_Params_Pull(&temp_params);

    // 更新分配矩阵配置
    temp_params.tam_config.active_thrusters = thruster_count;

    // 先暴力清零，清除掉之前可能残留的第 7、8 个推进器的旧数据
    memset(temp_params.tam_config.matrix, 0, sizeof(temp_params.tam_config.matrix));

    // 由于矩阵现在是 [推进器][自由度] (8x6)
    // 所以每一“行”代表一个推进器的 6 个自由度系数
    const uint8_t *matrix_payload = &payload[1]; 
    uint32_t row_size_bytes = TAM_MAX_DOF * sizeof(float); // 固定为 6 * 4 = 24 字节

    // 遍历每一个被激活的推进器，逐个拷贝它的 6 个自由度系数
    for (int t = 0; t < thruster_count; t++) {
        // 将载荷中属于第 t 个推进器的数据，放入矩阵的第 t 行
        memcpy(&temp_params.tam_config.matrix[t][0], 
               matrix_payload + (t * row_size_bytes), 
               row_size_bytes);
    }

    // 重新计算 Checksum 并完整写入 Flash 
    Driver_PidParam_Save(&temp_params);

    Driver_Protocol_SendAck(BSP_UART_OPI_NRT, DATA_TYPE_SET_TAM, ACK_SUCCESS, 0, USE_DMA);

    // 安全收尾
    bsp_cpu_reset();
}

void Task_NRT_Cmd_Init(void){
    Driver_Protocol_Register(DATA_TYPE_OTA, On_Receive_OTA_Cmd);
    Driver_Protocol_Register(DATA_TYPE_SET_PID_PARAM, On_Receive_Set_PID_Param_Cmd);
    Driver_Protocol_Register(DATA_TYPE_SET_SYS_MODE, On_Receive_Sys_Mode_Cmd);
    Driver_Protocol_Register(DATA_TYPE_SET_MOTION_MODE, On_Receive_Motion_Mode_Cmd);
    Driver_Protocol_Register(DATA_TYPE_SET_SERVO, On_Receive_Servo_Cmd);
    Driver_Protocol_Register(DATA_TYPE_SET_TAM, On_Receive_TAM_Cmd);
}

