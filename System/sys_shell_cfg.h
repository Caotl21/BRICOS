#ifndef __SYS_SHELL_CFG_H
#define __SYS_SHELL_CFG_H

/*
 * Shell 配置项（V1）
 * 说明：
 * 1. 仅定义核心容量与行为开关；
 * 2. 当前先支持单会话，后续可按 session_id 扩展；
 * 3. 建议与任务栈、队列深度一起评估。
 */

#define SHELL_MAX_COMMANDS             24u
#define SHELL_MAX_LINE_LEN             96u
#define SHELL_MAX_ARGS                 10u
#define SHELL_OUTPUT_BUF_SIZE          256u
#define SHELL_MAX_SESSIONS             1u

/* 1=仅解析可打印 ASCII（便于串口调试） */
#define SHELL_ASCII_ONLY               1u

#endif /* __SYS_SHELL_CFG_H */
