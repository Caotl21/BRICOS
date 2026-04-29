#ifndef __TASK_SHELL_TRANSPORT_NRT_H
#define __TASK_SHELL_TRANSPORT_NRT_H

#include "sys_shell_transport.h"

/*
 * Shell transport adapter.
 * Current implementation uses UART5 (BSP_UART_DEBUG) character stream.
 */
const shell_transport_vtable_t *Task_ShellTransportNRT_GetVTable(void);

#endif /* __TASK_SHELL_TRANSPORT_NRT_H */
