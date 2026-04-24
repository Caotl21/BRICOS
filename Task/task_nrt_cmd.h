#ifndef __TASK_NRT_CMD_H
#define __TASK_NRT_CMD_H

#include <stdint.h>

#define PAYLOAD_SIZE_PER_PID (5 * sizeof(float)) 

typedef struct
{
    uint32_t seq;
    uint32_t t_cmd_enter_tick;
    uint32_t t_mode_req_done_tick;
    uint32_t t_ack_start_tick;
    uint32_t t_ack_done_tick;
    uint16_t last_payload_len;
    uint8_t last_req_mode;
    uint8_t last_ack_code;
    uint8_t last_mode_mgr_status;
} nrt_sysmode_probe_t;

extern volatile nrt_sysmode_probe_t g_nrt_sysmode_probe;

/* ============================================================================
 * 对外开放的 API 接口
 * ============================================================================ */
/**
 * @brief  初始化并注册串口处理函数
 * @note   在 main 函数启动 FreeRTOS 调度器 (vTaskStartScheduler) 之前调用。
 */
void Task_NRT_Cmd_Init(void);

#endif /* __TASK_NRT_CMD_H */

