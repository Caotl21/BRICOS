#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "sys_port.h"
#include "sys_shell_cfg.h"
#include "sys_shell_core.h"

/* 单条命令执行期输出缓存 */
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

#if defined(__CC_ARM) || defined(__ARMCC_VERSION)
#define SHELL_CORE_SECTION_USED   __attribute__((used))
#define SHELL_CORE_SECTION_BASE   __attribute__((section("ShellCmd$$Table$$A")))
#define SHELL_CORE_SECTION_LIMIT  __attribute__((section("ShellCmd$$Table$$Z")))
#else
#define SHELL_CORE_SECTION_USED
#define SHELL_CORE_SECTION_BASE
#define SHELL_CORE_SECTION_LIMIT
#endif

/* 命令段边界标记：scatter 内部按 Base -> Table -> Limit 顺序放置 */
static const shell_cmd_desc_t s_shell_cmd_base_marker SHELL_CORE_SECTION_BASE SHELL_CORE_SECTION_USED = {0};
static const shell_cmd_desc_t s_shell_cmd_limit_marker SHELL_CORE_SECTION_LIMIT SHELL_CORE_SECTION_USED = {0};

/* 安全 strlen：最多扫描 max_len 个字节 */
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

/* 统一发送出口 */
static int prv_send_raw(const shell_peer_t *peer, const uint8_t *data, uint16_t len)
{
    if ((!s_shell_inited) || (s_transport.send == NULL) || (peer == NULL) || (data == NULL) || (len == 0u)) {
        return -1;
    }
    return s_transport.send(peer, data, len);
}

/* 根据 peer 获取会话对象 */
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

/* 将命令行拆分成 argc/argv（支持双引号） */
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

/* 去除首尾空白字符（原地修改） */
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

/* 当 handler 无输出时，按返回码给默认文本 */
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

/* transport 回调桥接 */
static void prv_transport_rx_cb(const shell_peer_t *peer, const uint8_t *data, uint16_t len)
{
    (void)System_ShellCore_OnRxBytes(peer, data, len);
}

/*
 * Tab 自动补全（仅首 token）：
 * 1. 0 个匹配：响铃
 * 2. 1 个匹配：直接补全
 * 3. 多个匹配：输出候选并重绘当前输入
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
            (void)prv_send_raw(&session->peer,
                               (const uint8_t *)list_buf,
                               (uint16_t)prv_safe_strlen(list_buf, sizeof(list_buf)));
        }
    }
    return 0;
}

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

uint16_t System_ShellCore_GetCmdCount(void)
{
    return s_cmd_count;
}

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

            if ((ch == '\n') && (session->last_ch == '\r')) {
                session->last_ch = ch;
                continue;
            }

            if ((ch == '\r') || (ch == '\n')) {
                session->line_buf[session->line_len] = '\0';
                (void)System_ShellCore_ExecuteLine(peer, session->line_buf);
                session->line_len = 0u;
                session->line_buf[0] = '\0';
                session->last_ch = ch;
                continue;
            }

            if ((ch == '\b') || (ch == 0x7Fu)) {
                if (session->line_len > 0u) {
                    session->line_len--;
                    session->line_buf[session->line_len] = '\0';
                }
                session->last_ch = ch;
                continue;
            }

            if (ch == '\t') {
                (void)prv_tab_complete(session);
                session->last_ch = ch;
                continue;
            }

#if (SHELL_ASCII_ONLY == 1u)
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

int System_ShellCore_RegisterBuiltins(void)
{
    const shell_cmd_desc_t *cmd_it;
    const shell_cmd_desc_t *cmd_end;

    if (!s_shell_inited) {
        return -1;
    }
    if (s_builtin_registered) {
        return 0;
    }

    cmd_it = &s_shell_cmd_base_marker + 1;
    cmd_end = &s_shell_cmd_limit_marker;
    while (cmd_it < cmd_end) {
        if ((cmd_it->name != NULL) && (cmd_it->handler != NULL)) {
            if (System_ShellCore_Register(cmd_it) != 0) {
                return -2;
            }
        }
        cmd_it++;
    }

    s_builtin_registered = 1u;
    return 0;
}
