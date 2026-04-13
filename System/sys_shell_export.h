#ifndef __SYS_SHELL_EXPORT_H
#define __SYS_SHELL_EXPORT_H

#include "sys_shell_core.h"

#define SHELL_EXPORT_CONCAT_INNER(a, b) a##b
#define SHELL_EXPORT_CONCAT(a, b) SHELL_EXPORT_CONCAT_INNER(a, b)

#if defined(__CC_ARM) || defined(__ARMCC_VERSION)
#define SHELL_CMD_EXPORT_USED       __attribute__((used))
#define SHELL_CMD_EXPORT_SECTION    __attribute__((section("ShellCmd$$Table$$M")))
#else
#define SHELL_CMD_EXPORT_USED
#define SHELL_CMD_EXPORT_SECTION
#endif

/*
 * 命令导出宏：
 * 将命令描述符放入 Shell 专用链接段，Core 在初始化阶段自动扫描并注册。
 */
#define EXPORT_SHELL_CMD(cmd_name, cmd_help, cmd_handler, cmd_perm, cmd_mode_mask) \
    static const shell_cmd_desc_t SHELL_EXPORT_CONCAT(g_shell_cmd_desc_, __LINE__)  \
        SHELL_CMD_EXPORT_SECTION SHELL_CMD_EXPORT_USED = {                           \
            (cmd_name),                                                               \
            (cmd_help),                                                               \
            (cmd_handler),                                                            \
            (cmd_perm),                                                               \
            (cmd_mode_mask)                                                           \
    }

#endif /* __SYS_SHELL_EXPORT_H */
