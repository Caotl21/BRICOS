#ifndef __TASK_SHELL_TRANSPORT_NRT_H
#define __TASK_SHELL_TRANSPORT_NRT_H

#include "sys_shell_transport.h"

/*
 * Shell over NRT 协议通道
 * 说明：
 * 1. 请求方向（上位机 -> MCU）使用 DATA_TYPE_SHELL_REQ；
 * 2. 响应方向（MCU -> 上位机）使用 DATA_TYPE_SHELL_RESP；
 * 3. 当前采用 NRT 帧承载“一帧一条命令行”模式。
 */
#define DATA_TYPE_SHELL_REQ   (0x20u)
#define DATA_TYPE_SHELL_RESP  (0x21u)

/* 获取 NRT 传输适配器 vtable */
const shell_transport_vtable_t *Task_ShellTransportNRT_GetVTable(void);

#endif /* __TASK_SHELL_TRANSPORT_NRT_H */
