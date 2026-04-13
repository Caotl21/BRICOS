#ifndef __SYS_SHELL_CORE_H
#define __SYS_SHELL_CORE_H

#include <stdint.h>

#include "sys_mode_manager.h"
#include "sys_shell_transport.h"

/*
 * Shell Core 对外接口
 * 设计目标：
 * 1. 与具体传输通道解耦（NRT 帧 / 独立 UART）；
 * 2. 统一命令注册、解析、权限与模式校验；
 * 3. 为 Task 层提供单一输入入口（OnRxBytes）。
 */

/* 命令权限位 */
#define SHELL_PERM_READONLY     (1u << 0)
#define SHELL_PERM_SAFE_CTRL    (1u << 1)
#define SHELL_PERM_DANGEROUS    (1u << 2)

/* 模式掩码工具 */
#define SHELL_MODE_MASK(mode)   (1u << ((uint8_t)(mode)))
#define SHELL_MODE_ANY          (0xFFFFFFFFu)

typedef enum {
    SHELL_RET_OK = 0,        /* 成功 */
    SHELL_RET_UNKNOWN_CMD,   /* 未知命令 */
    SHELL_RET_BAD_ARGS,      /* 参数格式错误 */
    SHELL_RET_DENIED,        /* 权限不足或安全策略拒绝 */
    SHELL_RET_MODE_BLOCKED,  /* 当前系统模式不允许执行 */
    SHELL_RET_BUSY,          /* 资源繁忙 */
    SHELL_RET_INTERNAL       /* 内部错误 */
} shell_ret_t;

typedef struct shell_cmd_ctx_s {
    shell_peer_t peer;                     /* 当前命令来源 */
    bot_sys_mode_e sys_mode_snapshot;      /* 系统模式快照 */
    bot_run_mode_e motion_mode_snapshot;   /* 运动模式快照 */
    uint32_t fault_flags_snapshot;         /* 故障位快照 */
    uint8_t auth_level;                    /* 鉴权等级（V1 默认 0） */
    void *internal;                        /* core 内部缓冲指针，handler 不应直接访问 */
} shell_cmd_ctx_t;

/* 命令回调签名：argc/argv 风格，便于与常见 CLI 习惯对齐 */
typedef shell_ret_t (*shell_cmd_handler_t)(shell_cmd_ctx_t *ctx, int argc, char **argv);

typedef struct {
    const char *name;                /* 命令名（唯一） */
    const char *help;                /* 帮助文本 */
    shell_cmd_handler_t handler;     /* 执行函数 */
    uint32_t permission_mask;        /* 权限要求 */
    uint32_t allowed_mode_mask;      /* 允许执行的系统模式掩码 */
} shell_cmd_desc_t;

/* 生命周期 */
/*
 * @brief 初始化 Shell Core，并绑定传输适配器。
 * @param transport 传输层 vtable，必须至少实现 send。
 * @return 0 成功，负数失败。
 */
int System_ShellCore_Init(const shell_transport_vtable_t *transport);

/* @brief 启动传输接收（会注册内部 rx 回调）。 */
int System_ShellCore_Start(void);

/* @brief 停止传输接收。 */
int System_ShellCore_Stop(void);

/* 命令注册 */
/* @brief 注册单条命令，命令名冲突会失败。 */
int System_ShellCore_Register(const shell_cmd_desc_t *cmd);

/* @brief 批量注册命令。 */
int System_ShellCore_RegisterArray(const shell_cmd_desc_t *cmds, uint16_t cmd_count);

/* @brief 获取当前已注册命令数量。 */
uint16_t System_ShellCore_GetCmdCount(void);

/* 输入入口 */
/*
 * @brief 统一输入入口。
 *        - NRT_FRAME：按“一次一条命令”处理；
 *        - UART_STREAM：按字符流增量拼行处理。
 */
int System_ShellCore_OnRxBytes(const shell_peer_t *peer, const uint8_t *data, uint16_t len);

/* @brief 直接执行一行命令（测试/内部调用方便）。 */
int System_ShellCore_ExecuteLine(const shell_peer_t *peer, const char *line);

/* 输出工具（供 handler 使用） */
/* @brief 向当前命令输出缓冲追加格式化文本。 */
int System_ShellCore_Printf(shell_cmd_ctx_t *ctx, const char *fmt, ...);

/* @brief 直接发送文本响应（绕过 handler 缓冲拼装）。 */
int System_ShellCore_SendText(const shell_peer_t *peer, shell_ret_t ret, const char *text);

/* 内置命令注册 */
/* @brief 注册内置命令集合（help/mode/motion 等）。 */
int System_ShellCore_RegisterBuiltins(void);

#endif /* __SYS_SHELL_CORE_H */
