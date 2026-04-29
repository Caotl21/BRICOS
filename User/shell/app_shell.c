#include "app_shell.h"

#include "sys_log.h"
#include "sys_shell_core.h"
#include "task_shell_transport_nrt.h"

/*
 * Shell transport is bound to UART5 (BSP_UART_DEBUG).
 * Keep transport selection in this file to avoid touching main.c.
 */
void App_Shell_Init(void)
{
    const shell_transport_vtable_t *shell_tp = Task_ShellTransportNRT_GetVTable();
    int shell_ret = System_ShellCore_Init(shell_tp);
    if (shell_ret != 0) {
        LOG_ERROR("ShellCore init failed: %d", shell_ret);
        return;
    }

    shell_ret = System_ShellCore_Start();
    if (shell_ret != 0) {
        LOG_ERROR("ShellCore start failed: %d", shell_ret);
    } else {
        LOG_INFO("ShellCore started on UART5 debug transport");
    }
}
