#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "sys_port.h"
#include "sys_shell_cfg.h"
#include "sys_shell_core.h"

/*
 * Shell Core 实现说明：
 * 1. 持有命令注册表与会话状态（字符流场景）；
 * 2. 对外仅暴露统一入口，底层传输通过 vtable 回调；
 * 3. 执行前做模式/权限校验，执行后统一格式回包。
 */

/* 单次命令执行的输出缓冲 */
typedef struct {
    char buf[SHELL_OUTPUT_BUF_SIZE];
    uint16_t len;
} shell_out_buf_t;

/* 会话状态（主要用于 UART 字符流输入） */
typedef struct {
    uint8_t in_use;
    shell_peer_t peer;
    char line_buf[SHELL_MAX_LINE_LEN + 1u];
    uint16_t line_len;
    uint8_t last_ch;
} shell_session_t;

static shell_transport_vtable_t s_transport;
static uint8_t s_shell_inited = 0u;
static const shell_cmd_desc_t *s_cmd_table[SHELL_MAX_COMMANDS];
static uint16_t s_cmd_count = 0u;
static shell_session_t s_sessions[SHELL_MAX_SESSIONS];
static uint8_t s_builtin_registered = 0u;

/* ----------------------------- 基础工具函数 ----------------------------- */

/* 安全版 strlen：最多扫描 max_len 个字节，避免无终止符越界 */
static uint16_t prv_safe_strlen(const char *s, uint16_t max_len)
{
    uint16_t n = 0u;
    if (s == NULL) {
        return 0u;
    }
    while ((n < max_len) && (s[n] != '\0')) {
        n++;
    }
    return n;
}

/* 大小写不敏感的字符串相等比较 */
static int prv_streq_ignore_case(const char *a, const char *b)
{
    if ((a == NULL) || (b == NULL)) {
        return 0;
    }
    while ((*a != '\0') && (*b != '\0')) {
        unsigned char ca = (unsigned char)*a;
        unsigned char cb = (unsigned char)*b;
        if ((char)tolower(ca) != (char)tolower(cb)) {
            return 0;
        }
        a++;
        b++;
    }
    return ((*a == '\0') && (*b == '\0')) ? 1 : 0;
}

/* 大小写不敏感的前缀匹配 */
static int prv_starts_with_ignore_case(const char *full, const char *prefix)
{
    if ((full == NULL) || (prefix == NULL)) {
        return 0;
    }
    while (*prefix != '\0') {
        unsigned char ca = (unsigned char)*full;
        unsigned char cb = (unsigned char)*prefix;
        if (*full == '\0') {
            return 0;
        }
        if ((char)tolower(ca) != (char)tolower(cb)) {
            return 0;
        }
        full++;
        prefix++;
    }
    return 1;
}

/* 统一发送出口：所有文本最终都由 transport.send 发出 */
static int prv_send_raw(const shell_peer_t *peer, const uint8_t *data, uint16_t len)
{
    if ((!s_shell_inited) || (s_transport.send == NULL) || (peer == NULL) || (data == NULL) || (len == 0u)) {
        return -1;
    }
    return s_transport.send(peer, data, len);
}

/* 根据 peer 获取会话对象（V1 默认单会话） */
static shell_session_t *prv_get_session(const shell_peer_t *peer)
{
    uint16_t idx = 0u;
    shell_session_t *session;

    if (peer == NULL) {
        return NULL;
    }

#if (SHELL_MAX_SESSIONS > 1u)
    idx = (uint16_t)(peer->session_id % SHELL_MAX_SESSIONS);
#endif

    session = &s_sessions[idx];
    if (!session->in_use) {
        memset(session, 0, sizeof(*session));
        session->in_use = 1u;
    }
    session->peer = *peer;
    return session;
}

/* 按命令名查找注册表 */
static const shell_cmd_desc_t *prv_find_command(const char *name)
{
    uint16_t i;
    for (i = 0u; i < s_cmd_count; i++) {
        const shell_cmd_desc_t *cmd = s_cmd_table[i];
        if ((cmd != NULL) && prv_streq_ignore_case(cmd->name, name)) {
            return cmd;
        }
    }
    return NULL;
}

/* 检查命令在当前系统模式下是否允许执行 */
static uint8_t prv_mode_allowed(const shell_cmd_desc_t *cmd, bot_sys_mode_e mode)
{
    uint32_t bit;
    if (cmd == NULL) {
        return 0u;
    }
    if (cmd->allowed_mode_mask == SHELL_MODE_ANY) {
        return 1u;
    }
    bit = SHELL_MODE_MASK(mode);
    return ((cmd->allowed_mode_mask & bit) != 0u) ? 1u : 0u;
}

/* 将命令行拆分为 argc/argv（支持双引号包裹参数） */
static int prv_split_args(char *line, char **argv, int max_args)
{
    int argc = 0;
    char *p = line;

    while ((*p != '\0') && (argc < max_args)) {
        while ((*p != '\0') && isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == '\0') {
            break;
        }

        if (*p == '"') {
            p++;
            argv[argc++] = p;
            while ((*p != '\0') && (*p != '"')) {
                p++;
            }
            if (*p == '"') {
                *p = '\0';
                p++;
            }
        } else {
            argv[argc++] = p;
            while ((*p != '\0') && (!isspace((unsigned char)*p))) {
                p++;
            }
            if (*p != '\0') {
                *p = '\0';
                p++;
            }
        }
    }
    return argc;
}

/* 去除首尾空白字符，原地修改 */
static void prv_trim_inplace(char *buf)
{
    uint16_t len;
    uint16_t start = 0u;
    uint16_t end;
    uint16_t i;

    if (buf == NULL) {
        return;
    }

    len = (uint16_t)strlen(buf);
    if (len == 0u) {
        return;
    }

    while ((start < len) && isspace((unsigned char)buf[start])) {
        start++;
    }

    if (start >= len) {
        buf[0] = '\0';
        return;
    }

    end = len;
    while ((end > start) && isspace((unsigned char)buf[end - 1u])) {
        end--;
    }

    if (start > 0u) {
        for (i = 0u; (start + i) < end; i++) {
            buf[i] = buf[start + i];
        }
        buf[i] = '\0';
    } else {
        buf[end] = '\0';
    }
}

/* 根据返回码给默认提示文本（当 handler 未输出内容时使用） */
static const char *prv_default_text_by_ret(shell_ret_t ret)
{
    switch (ret) {
        case SHELL_RET_OK:            return "OK";
        case SHELL_RET_UNKNOWN_CMD:   return "UNKNOWN_CMD";
        case SHELL_RET_BAD_ARGS:      return "BAD_ARGS";
        case SHELL_RET_DENIED:        return "DENIED";
        case SHELL_RET_MODE_BLOCKED:  return "MODE_BLOCKED";
        case SHELL_RET_BUSY:          return "BUSY";
        case SHELL_RET_INTERNAL:      return "INTERNAL_ERROR";
        default:                      return "UNKNOWN_ERROR";
    }
}

/* 将 ModeManager 的状态码映射为 Shell 返回码 */
static shell_ret_t prv_mode_mgr_status_to_shell_ret(sys_mode_mgr_status_t st)
{
    switch (st) {
        case SYS_MODE_MGR_OK:                 return SHELL_RET_OK;
        case SYS_MODE_MGR_INVALID_PARAM:      return SHELL_RET_BAD_ARGS;
        case SYS_MODE_MGR_INVALID_TRANSITION: return SHELL_RET_MODE_BLOCKED;
        case SYS_MODE_MGR_SAFETY_BLOCKED:     return SHELL_RET_DENIED;
        case SYS_MODE_MGR_FAULT_LATCHED:      return SHELL_RET_DENIED;
        default:                              return SHELL_RET_INTERNAL;
    }
}

/* 系统模式枚举转字符串 */
static const char *prv_sys_mode_str(bot_sys_mode_e mode)
{
    switch (mode) {
        case SYS_MODE_STANDBY:         return "STANDBY";
        case SYS_MODE_ACTIVE_DISARMED: return "DISARMED";
        case SYS_MODE_MOTION_ARMED:    return "ARMED";
        case SYS_MODE_FAILSAFE:        return "FAILSAFE";
        default:                       return "UNKNOWN";
    }
}

/* 运动模式枚举转字符串 */
static const char *prv_motion_mode_str(bot_run_mode_e mode)
{
    switch (mode) {
        case MOTION_STATE_MANUAL:    return "MANUAL";
        case MOTION_STATE_STABILIZE: return "STABILIZE";
        case MOTION_STATE_AUTO:      return "AUTO";
        default:                     return "UNKNOWN";
    }
}

/* 文本解析为系统模式 */
static uint8_t prv_parse_sys_mode(const char *s, bot_sys_mode_e *out_mode)
{
    if ((s == NULL) || (out_mode == NULL)) {
        return 0u;
    }
    if (prv_streq_ignore_case(s, "standby")) {
        *out_mode = SYS_MODE_STANDBY;
        return 1u;
    }
    if (prv_streq_ignore_case(s, "disarmed")) {
        *out_mode = SYS_MODE_ACTIVE_DISARMED;
        return 1u;
    }
    if (prv_streq_ignore_case(s, "armed")) {
        *out_mode = SYS_MODE_MOTION_ARMED;
        return 1u;
    }
    if (prv_streq_ignore_case(s, "failsafe")) {
        *out_mode = SYS_MODE_FAILSAFE;
        return 1u;
    }
    return 0u;
}

/* 文本解析为运动模式 */
static uint8_t prv_parse_motion_mode(const char *s, bot_run_mode_e *out_mode)
{
    if ((s == NULL) || (out_mode == NULL)) {
        return 0u;
    }
    if (prv_streq_ignore_case(s, "manual")) {
        *out_mode = MOTION_STATE_MANUAL;
        return 1u;
    }
    if (prv_streq_ignore_case(s, "stabilize")) {
        *out_mode = MOTION_STATE_STABILIZE;
        return 1u;
    }
    if (prv_streq_ignore_case(s, "auto")) {
        *out_mode = MOTION_STATE_AUTO;
        return 1u;
    }
    return 0u;
}

/* 传输层回调桥接：把 transport 收到的数据转给 core 入口 */
static void prv_transport_rx_cb(const shell_peer_t *peer, const uint8_t *data, uint16_t len)
{
    (void)System_ShellCore_OnRxBytes(peer, data, len);
}

/*
 * Tab 自动补全（仅首 token）：
 * - 0 个匹配：发送响铃字符；
 * - 1 个匹配：直接补全；
 * - 多个匹配：输出候选列表并重绘当前输入。
 */
static int prv_tab_complete(shell_session_t *session)
{
    uint16_t i;
    uint16_t hit_count = 0u;
    const shell_cmd_desc_t *hit_cmd = NULL;
    char prefix[SHELL_MAX_LINE_LEN + 1u];
    uint16_t prefix_len;

    if ((session == NULL) || (session->line_len == 0u)) {
        return 0;
    }

    memcpy(prefix, session->line_buf, session->line_len);
    prefix[session->line_len] = '\0';

    /* 仅对首 token 做补全 */
    if (strchr(prefix, ' ') != NULL) {
        return 0;
    }

    prefix_len = (uint16_t)strlen(prefix);
    for (i = 0u; i < s_cmd_count; i++) {
        const shell_cmd_desc_t *cmd = s_cmd_table[i];
        if ((cmd != NULL) && prv_starts_with_ignore_case(cmd->name, prefix)) {
            hit_count++;
            hit_cmd = cmd;
        }
    }

    if (hit_count == 0u) {
        static const uint8_t bell[] = "\a";
        (void)prv_send_raw(&session->peer, bell, (uint16_t)(sizeof(bell) - 1u));
        return 0;
    }

    if ((hit_count == 1u) && (hit_cmd != NULL)) {
        uint16_t cmd_len = prv_safe_strlen(hit_cmd->name, SHELL_MAX_LINE_LEN);
        if (cmd_len > prefix_len) {
            const char *tail = &hit_cmd->name[prefix_len];
            uint16_t tail_len = (uint16_t)strlen(tail);
            if ((session->line_len + tail_len) < SHELL_MAX_LINE_LEN) {
                memcpy(&session->line_buf[session->line_len], tail, tail_len);
                session->line_len += tail_len;
                session->line_buf[session->line_len] = '\0';
                (void)prv_send_raw(&session->peer, (const uint8_t *)tail, tail_len);
            }
        }
        return 0;
    }

    {
        char list_buf[SHELL_OUTPUT_BUF_SIZE];
        uint16_t used = 0u;
        uint16_t room;
        int n;

        memset(list_buf, 0, sizeof(list_buf));
        room = (uint16_t)(sizeof(list_buf) - used);
        n = snprintf(&list_buf[used], room, "\r\n");
        if (n > 0) {
            used = (uint16_t)((n >= room) ? (sizeof(list_buf) - 1u) : (used + (uint16_t)n));
        }

        for (i = 0u; i < s_cmd_count; i++) {
            const shell_cmd_desc_t *cmd = s_cmd_table[i];
            if ((cmd != NULL) && prv_starts_with_ignore_case(cmd->name, prefix)) {
                room = (uint16_t)(sizeof(list_buf) - used);
                if (room <= 4u) {
                    break;
                }
                n = snprintf(&list_buf[used], room, "%s  ", cmd->name);
                if (n > 0) {
                    used = (uint16_t)((n >= room) ? (sizeof(list_buf) - 1u) : (used + (uint16_t)n));
                }
                if (used >= (sizeof(list_buf) - 4u)) {
                    break;
                }
            }
        }

        room = (uint16_t)(sizeof(list_buf) - used);
        if (room > 1u) {
            n = snprintf(&list_buf[used], room, "\r\n%s", session->line_buf);
            if (n > 0) {
                used = (uint16_t)((n >= room) ? (sizeof(list_buf) - 1u) : (used + (uint16_t)n));
            }
        }

        if (used > 0u) {
            (void)prv_send_raw(&session->peer, (const uint8_t *)list_buf, (uint16_t)prv_safe_strlen(list_buf, sizeof(list_buf)));
        }
    }
    return 0;
}

/* ----------------------------- 对外 API ----------------------------- */

/* 供命令 handler 拼接输出文本 */
int System_ShellCore_Printf(shell_cmd_ctx_t *ctx, const char *fmt, ...)
{
    shell_out_buf_t *out;
    uint16_t remain;
    int written;
    va_list args;

    if ((ctx == NULL) || (fmt == NULL)) {
        return -1;
    }

    out = (shell_out_buf_t *)ctx->internal;
    if (out == NULL) {
        return -1;
    }

    if (out->len >= (SHELL_OUTPUT_BUF_SIZE - 1u)) {
        return 0;
    }

    remain = (uint16_t)(SHELL_OUTPUT_BUF_SIZE - out->len);
    va_start(args, fmt);
    written = vsnprintf(&out->buf[out->len], remain, fmt, args);
    va_end(args);

    if (written < 0) {
        return -1;
    }

    if ((uint16_t)written >= remain) {
        out->len = (uint16_t)(SHELL_OUTPUT_BUF_SIZE - 1u);
    } else {
        out->len = (uint16_t)(out->len + (uint16_t)written);
    }

    return 0;
}

/* 统一文本回包（NRT 带 ret 前缀，UART 走纯文本） */
int System_ShellCore_SendText(const shell_peer_t *peer, shell_ret_t ret, const char *text)
{
    char frame[SHELL_OUTPUT_BUF_SIZE + 32u];
    int n;
    const char *payload = text;

    if (payload == NULL) {
        payload = "";
    }

    if (peer == NULL) {
        return -1;
    }

    if (peer->type == SHELL_TP_UART_STREAM) {
        n = snprintf(frame, sizeof(frame), "%s\r\n", payload);
    } else {
        n = snprintf(frame, sizeof(frame), "ret=%u %s\r\n", (unsigned int)ret, payload);
    }

    if (n < 0) {
        return -1;
    }
    if ((uint16_t)n >= sizeof(frame)) {
        n = (int)(sizeof(frame) - 1u);
    }

    return prv_send_raw(peer, (const uint8_t *)frame, (uint16_t)n);
}

/* 注册单条命令 */
int System_ShellCore_Register(const shell_cmd_desc_t *cmd)
{
    uint16_t i;

    if ((!s_shell_inited) || (cmd == NULL) || (cmd->name == NULL) || (cmd->handler == NULL)) {
        return -1;
    }

    SYS_ENTER_CRITICAL();
    for (i = 0u; i < s_cmd_count; i++) {
        if ((s_cmd_table[i] != NULL) && prv_streq_ignore_case(s_cmd_table[i]->name, cmd->name)) {
            SYS_EXIT_CRITICAL();
            return -2;
        }
    }

    if (s_cmd_count >= SHELL_MAX_COMMANDS) {
        SYS_EXIT_CRITICAL();
        return -3;
    }

    s_cmd_table[s_cmd_count++] = cmd;
    SYS_EXIT_CRITICAL();
    return 0;
}

/* 批量注册命令 */
int System_ShellCore_RegisterArray(const shell_cmd_desc_t *cmds, uint16_t cmd_count)
{
    uint16_t i;
    int ret;
    if (cmds == NULL) {
        return -1;
    }
    for (i = 0u; i < cmd_count; i++) {
        ret = System_ShellCore_Register(&cmds[i]);
        if (ret != 0) {
            return ret;
        }
    }
    return 0;
}

/* 获取当前命令总数 */
uint16_t System_ShellCore_GetCmdCount(void)
{
    return s_cmd_count;
}

/*
 * 执行一行命令主流程：
 * 1. 拷贝并 trim 输入；
 * 2. 拆分参数、查找命令；
 * 3. 拉取模式快照并做模式/权限检查；
 * 4. 调用 handler；
 * 5. 统一回包。
 */
int System_ShellCore_ExecuteLine(const shell_peer_t *peer, const char *line)
{
    char local_line[SHELL_MAX_LINE_LEN + 1u];
    char *argv[SHELL_MAX_ARGS];
    int argc;
    const shell_cmd_desc_t *cmd;
    shell_cmd_ctx_t ctx;
    shell_out_buf_t out;
    shell_ret_t ret;
    const char *ret_text;

    if ((!s_shell_inited) || (peer == NULL) || (line == NULL)) {
        return -1;
    }

    memset(local_line, 0, sizeof(local_line));
    memcpy(local_line, line, prv_safe_strlen(line, SHELL_MAX_LINE_LEN));
    local_line[SHELL_MAX_LINE_LEN] = '\0';
    prv_trim_inplace(local_line);

    if (local_line[0] == '\0') {
        return 0;
    }

    argc = prv_split_args(local_line, argv, SHELL_MAX_ARGS);
    if (argc <= 0) {
        return 0;
    }

    cmd = prv_find_command(argv[0]);
    if (cmd == NULL) {
        return System_ShellCore_SendText(peer, SHELL_RET_UNKNOWN_CMD, "unknown command");
    }

    memset(&ctx, 0, sizeof(ctx));
    memset(&out, 0, sizeof(out));
    ctx.peer = *peer;
    ctx.internal = &out;
    System_ModeManager_Pull(&ctx.sys_mode_snapshot, &ctx.motion_mode_snapshot, &ctx.fault_flags_snapshot);

    if (!prv_mode_allowed(cmd, ctx.sys_mode_snapshot)) {
        return System_ShellCore_SendText(peer, SHELL_RET_MODE_BLOCKED, "blocked by system mode");
    }

    /* V1: 未认证会话默认禁止危险命令 */
    if (((cmd->permission_mask & SHELL_PERM_DANGEROUS) != 0u) && (ctx.auth_level == 0u)) {
        return System_ShellCore_SendText(peer, SHELL_RET_DENIED, "dangerous command requires auth");
    }

    ret = cmd->handler(&ctx, argc, argv);

    if (out.len == 0u) {
        ret_text = prv_default_text_by_ret(ret);
        return System_ShellCore_SendText(peer, ret, ret_text);
    }

    return System_ShellCore_SendText(peer, ret, out.buf);
}

/*
 * 统一字节流入口：
 * - NRT_FRAME：直接视为完整命令行；
 * - UART_STREAM：按字符处理（回车执行、退格编辑、Tab 补全）。
 */
int System_ShellCore_OnRxBytes(const shell_peer_t *peer, const uint8_t *data, uint16_t len)
{
    uint16_t i;

    if ((!s_shell_inited) || (peer == NULL) || (data == NULL) || (len == 0u)) {
        return -1;
    }

    if (peer->type == SHELL_TP_NRT_FRAME) {
        char line[SHELL_MAX_LINE_LEN + 1u];
        uint16_t copy_len = (len > SHELL_MAX_LINE_LEN) ? SHELL_MAX_LINE_LEN : len;
        memcpy(line, data, copy_len);
        line[copy_len] = '\0';
        return System_ShellCore_ExecuteLine(peer, line);
    }

    {
        shell_session_t *session = prv_get_session(peer);
        if (session == NULL) {
            return -2;
        }

        for (i = 0u; i < len; i++) {
            uint8_t ch = data[i];

            /* 兼容 CRLF，避免回车触发两次执行 */
            if ((ch == '\n') && (session->last_ch == '\r')) {
                session->last_ch = ch;
                continue;
            }

            /* Enter：提交当前命令行 */
            if ((ch == '\r') || (ch == '\n')) {
                session->line_buf[session->line_len] = '\0';
                (void)System_ShellCore_ExecuteLine(peer, session->line_buf);
                session->line_len = 0u;
                session->line_buf[0] = '\0';
                session->last_ch = ch;
                continue;
            }

            /* Backspace / DEL：删除一个字符 */
            if ((ch == '\b') || (ch == 0x7Fu)) {
                if (session->line_len > 0u) {
                    session->line_len--;
                    session->line_buf[session->line_len] = '\0';
                }
                session->last_ch = ch;
                continue;
            }

            /* Tab：命令补全 */
            if (ch == '\t') {
                (void)prv_tab_complete(session);
                session->last_ch = ch;
                continue;
            }

#if (SHELL_ASCII_ONLY == 1u)
            /* 仅接受可打印 ASCII，过滤控制字符与高位字节 */
            if ((ch < 0x20u) || (ch > 0x7Eu)) {
                session->last_ch = ch;
                continue;
            }
#endif

            if (session->line_len < SHELL_MAX_LINE_LEN) {
                session->line_buf[session->line_len++] = (char)ch;
                session->line_buf[session->line_len] = '\0';
            }
            session->last_ch = ch;
        }
    }

    return 0;
}

/* 初始化 core 与 transport，并注册内置命令 */
int System_ShellCore_Init(const shell_transport_vtable_t *transport)
{
    if ((transport == NULL) || (transport->send == NULL)) {
        return -1;
    }

    memset(&s_transport, 0, sizeof(s_transport));
    memset(s_cmd_table, 0, sizeof(s_cmd_table));
    memset(s_sessions, 0, sizeof(s_sessions));
    s_cmd_count = 0u;
    s_builtin_registered = 0u;

    s_transport = *transport;

    if (s_transport.init != NULL) {
        if (s_transport.init() != 0) {
            return -2;
        }
    }

    s_shell_inited = 1u;
    return System_ShellCore_RegisterBuiltins();
}

/* 启动 transport 接收回调 */
int System_ShellCore_Start(void)
{
    if (!s_shell_inited) {
        return -1;
    }
    if (s_transport.start == NULL) {
        return 0;
    }
    return s_transport.start(prv_transport_rx_cb);
}

/* 停止 transport */
int System_ShellCore_Stop(void)
{
    if (!s_shell_inited) {
        return -1;
    }
    if (s_transport.stop == NULL) {
        return 0;
    }
    return s_transport.stop();
}

/* =========================== 内置命令（V1） =========================== */

/* help：输出命令清单 */
static shell_ret_t prv_cmd_help(shell_cmd_ctx_t *ctx, int argc, char **argv)
{
    uint16_t i;
    (void)argc;
    (void)argv;

    System_ShellCore_Printf(ctx, "commands(%u):\r\n", (unsigned int)s_cmd_count);
    for (i = 0u; i < s_cmd_count; i++) {
        const shell_cmd_desc_t *cmd = s_cmd_table[i];
        if ((cmd != NULL) && (cmd->name != NULL)) {
            System_ShellCore_Printf(ctx, "  %-12s %s\r\n", cmd->name, (cmd->help != NULL) ? cmd->help : "");
        }
    }
    return SHELL_RET_OK;
}

/* echo：回显参数 */
static shell_ret_t prv_cmd_echo(shell_cmd_ctx_t *ctx, int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        System_ShellCore_Printf(ctx, "%s%s", argv[i], (i == (argc - 1)) ? "" : " ");
    }
    return SHELL_RET_OK;
}

/* mode：查询/切换系统模式 */
static shell_ret_t prv_cmd_mode(shell_cmd_ctx_t *ctx, int argc, char **argv)
{
    if (argc == 1) {
        System_ShellCore_Printf(ctx, "sys=%s motion=%s faults=0x%08lX",
                                prv_sys_mode_str(ctx->sys_mode_snapshot),
                                prv_motion_mode_str(ctx->motion_mode_snapshot),
                                (unsigned long)ctx->fault_flags_snapshot);
        return SHELL_RET_OK;
    }

    if ((argc == 3) && prv_streq_ignore_case(argv[1], "set")) {
        bot_sys_mode_e target_mode;
        sys_mode_mgr_status_t st;
        if (!prv_parse_sys_mode(argv[2], &target_mode)) {
            return SHELL_RET_BAD_ARGS;
        }

        if (target_mode == SYS_MODE_FAILSAFE) {
            st = System_ModeManager_EnterFailsafe(SYS_FAULT_LOS);
        } else {
            st = System_ModeManager_RequestSysMode(target_mode);
        }

        System_ModeManager_Pull(&ctx->sys_mode_snapshot, &ctx->motion_mode_snapshot, &ctx->fault_flags_snapshot);
        System_ShellCore_Printf(ctx, "set sys=%s, now=%s, st=%u",
                                argv[2], prv_sys_mode_str(ctx->sys_mode_snapshot), (unsigned int)st);
        return prv_mode_mgr_status_to_shell_ret(st);
    }

    return SHELL_RET_BAD_ARGS;
}

/* motion：查询/切换运动模式 */
static shell_ret_t prv_cmd_motion(shell_cmd_ctx_t *ctx, int argc, char **argv)
{
    if (argc == 1) {
        System_ShellCore_Printf(ctx, "motion=%s", prv_motion_mode_str(ctx->motion_mode_snapshot));
        return SHELL_RET_OK;
    }

    if ((argc == 3) && prv_streq_ignore_case(argv[1], "set")) {
        bot_run_mode_e target_mode;
        sys_mode_mgr_status_t st;
        if (!prv_parse_motion_mode(argv[2], &target_mode)) {
            return SHELL_RET_BAD_ARGS;
        }

        st = System_ModeManager_RequestMotionMode(target_mode);
        System_ModeManager_Pull(&ctx->sys_mode_snapshot, &ctx->motion_mode_snapshot, &ctx->fault_flags_snapshot);
        System_ShellCore_Printf(ctx, "set motion=%s, now=%s, st=%u",
                                argv[2], prv_motion_mode_str(ctx->motion_mode_snapshot), (unsigned int)st);
        return prv_mode_mgr_status_to_shell_ret(st);
    }

    return SHELL_RET_BAD_ARGS;
}

/* fault：查看故障位 */
static shell_ret_t prv_cmd_fault(shell_cmd_ctx_t *ctx, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    System_ShellCore_Printf(ctx, "fault_flags=0x%08lX", (unsigned long)ctx->fault_flags_snapshot);
    return SHELL_RET_OK;
}

/* 注册内置命令集合 */
int System_ShellCore_RegisterBuiltins(void)
{
    static const shell_cmd_desc_t s_builtin_cmds[] = {
        {
            "help",
            "show command list",
            prv_cmd_help,
            SHELL_PERM_READONLY,
            SHELL_MODE_ANY
        },
        {
            "echo",
            "echo text",
            prv_cmd_echo,
            SHELL_PERM_READONLY,
            SHELL_MODE_ANY
        },
        {
            "mode",
            "mode [set standby|disarmed|armed|failsafe]",
            prv_cmd_mode,
            SHELL_PERM_SAFE_CTRL,
            SHELL_MODE_ANY
        },
        {
            "motion",
            "motion [set manual|stabilize|auto]",
            prv_cmd_motion,
            SHELL_PERM_SAFE_CTRL,
            SHELL_MODE_ANY
        },
        {
            "fault",
            "show fault flags",
            prv_cmd_fault,
            SHELL_PERM_READONLY,
            SHELL_MODE_ANY
        }
    };

    if (!s_shell_inited) {
        return -1;
    }
    if (s_builtin_registered) {
        return 0;
    }
    if (System_ShellCore_RegisterArray(s_builtin_cmds, (uint16_t)(sizeof(s_builtin_cmds) / sizeof(s_builtin_cmds[0]))) != 0) {
        return -2;
    }
    s_builtin_registered = 1u;
    return 0;
}
