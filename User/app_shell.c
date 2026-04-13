#include "app_shell.h"

#include "sys_log.h"
#include "sys_shell_core.h"
#include "task_shell_transport_nrt.h"

/*
 * @brief 初始化并启动 Shell（当前绑定 NRT 传输）。
 * @note  若后续切换到独立 shell UART，只需在本文件替换 transport 选择逻辑，
 *        main.c 无需改动。
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
        LOG_INFO("ShellCore started on NRT transport");
    }
}
