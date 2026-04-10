#include <ctype.h>
#include <string.h>

#include "sys_mode_manager.h"
#include "sys_shell_export.h"

/* ModeManager 状态码映射到 Shell 返回码 */
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

/* 大小写不敏感字符串比较 */
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

/* 解析系统模式 */
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

/* 解析运动模式 */
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

static shell_ret_t prv_cmd_help(shell_cmd_ctx_t *ctx, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    System_ShellCore_Printf(ctx,
                            "commands:\r\n"
                            "  help\r\n"
                            "  echo <text>\r\n"
                            "  sysmode request | sysmode set standby|disarmed|armed|failsafe\r\n"
                            "  momode request | momode set manual|stabilize|auto\r\n"
                            "  fault\r\n"
                            "  euler request\r\n"
                            "  depthtemp request\r\n"
                            "  power request\r\n"
                            "  cabin request\r\n"
                            "  chip request");
    return SHELL_RET_OK;
}

static shell_ret_t prv_cmd_echo(shell_cmd_ctx_t *ctx, int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        System_ShellCore_Printf(ctx, "%s%s", argv[i], (i == (argc - 1)) ? "" : " ");
    }
    return SHELL_RET_OK;
}

static shell_ret_t prv_cmd_sysmode(shell_cmd_ctx_t *ctx, int argc, char **argv)
{
    if (argc == 1) {
        System_ShellCore_Printf(ctx,
                                "usage: sysmode [request | set <target>]\r\n"
                                "               <target>: standby | disarmed | armed | failsafe");
        return SHELL_RET_OK;
    }

    if ((argc == 2) && prv_streq_ignore_case(argv[1], "request")) {
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
            System_ShellCore_Printf(ctx, "invalid target: %s\r\n", argv[2]);
            System_ShellCore_Printf(ctx,
                                    "usage: sysmode [request | set <target>]\r\n"
                                    "               <target>: standby | disarmed | armed | failsafe");
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

    System_ShellCore_Printf(ctx,
                            "usage: sysmode [request | set <target>]\r\n"
                            "               <target>: standby | disarmed | armed | failsafe");
    return SHELL_RET_BAD_ARGS;
}

static shell_ret_t prv_cmd_momode(shell_cmd_ctx_t *ctx, int argc, char **argv)
{
    if (argc == 1) {
        System_ShellCore_Printf(ctx,
                                "usage: momode [request | set <target>]\r\n"
                                "              <target>: manual | stabilize | auto");
        return SHELL_RET_OK;
    }

    if ((argc == 2) && prv_streq_ignore_case(argv[1], "request")) {
        System_ShellCore_Printf(ctx, "motion=%s", prv_motion_mode_str(ctx->motion_mode_snapshot));
        return SHELL_RET_OK;
    }

    if ((argc == 3) && prv_streq_ignore_case(argv[1], "set")) {
        bot_run_mode_e target_mode;
        sys_mode_mgr_status_t st;
        if (!prv_parse_motion_mode(argv[2], &target_mode)) {
            System_ShellCore_Printf(ctx, "invalid target: %s\r\n", argv[2]);
            System_ShellCore_Printf(ctx,
                                    "usage: momode [request | set <target>]\r\n"
                                    "              <target>: manual | stabilize | auto");
            return SHELL_RET_BAD_ARGS;
        }

        st = System_ModeManager_RequestMotionMode(target_mode);
        System_ModeManager_Pull(&ctx->sys_mode_snapshot, &ctx->motion_mode_snapshot, &ctx->fault_flags_snapshot);
        System_ShellCore_Printf(ctx, "set motion=%s, now=%s, st=%u",
                                argv[2], prv_motion_mode_str(ctx->motion_mode_snapshot), (unsigned int)st);
        return prv_mode_mgr_status_to_shell_ret(st);
    }

    System_ShellCore_Printf(ctx,
                            "usage: momode [request | set <target>]\r\n"
                            "              <target>: manual | stabilize | auto");
    return SHELL_RET_BAD_ARGS;
}

static shell_ret_t prv_cmd_fault(shell_cmd_ctx_t *ctx, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    System_ShellCore_Printf(ctx, "fault_flags=0x%08lX", (unsigned long)ctx->fault_flags_snapshot);
    return SHELL_RET_OK;
}

EXPORT_SHELL_CMD("help", "show command list", prv_cmd_help, SHELL_PERM_READONLY, SHELL_MODE_ANY);
EXPORT_SHELL_CMD("echo", "echo text", prv_cmd_echo, SHELL_PERM_READONLY, SHELL_MODE_ANY);
EXPORT_SHELL_CMD("sysmode", "sysmode request | sysmode set standby|disarmed|armed|failsafe", prv_cmd_sysmode, SHELL_PERM_SAFE_CTRL, SHELL_MODE_ANY);
EXPORT_SHELL_CMD("momode", "momode request | momode set manual|stabilize|auto", prv_cmd_momode, SHELL_PERM_SAFE_CTRL, SHELL_MODE_ANY);
EXPORT_SHELL_CMD("fault", "show fault flags", prv_cmd_fault, SHELL_PERM_READONLY, SHELL_MODE_ANY);
