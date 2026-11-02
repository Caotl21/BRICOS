#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "bsp_delay.h"
#include "bsp_cpu.h"

#include "driver_param.h"
#include "fault_snapshot.h"
#include "sys_data_pool.h"
#include "sys_log.h"
#include "sys_mode_manager.h"
#include "sys_shell_export.h"

#define SHELL_FAILSAFE_MAX_DEPTH_MIN_M       0.01
#define SHELL_FAILSAFE_MAX_DEPTH_MAX_M    10000.0
#define SHELL_FAILSAFE_LOW_VOLTAGE_MIN_V      1.01
#define SHELL_FAILSAFE_LOW_VOLTAGE_MAX_V    100.0
#define SHELL_FAILSAFE_REBOOT_DELAY_MS       50u

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

static int prv_parse_failsafe_value(const char *s, double min, double max, float *out)
{
    char *end = NULL;
    double value;

    if ((s == NULL) || (out == NULL)) {
        return 0;
    }

    value = strtod(s, &end);
    if ((end == s) || (end == NULL) || (*end != '\0') ||
        !(value >= min) || !(value <= max)) {
        return 0;
    }

    *out = (float)value;
    return 1;
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
        case MOTION_STATE_DEBUG:     return "DEBUG";
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
    if (prv_streq_ignore_case(s, "debug")) {
        *out_mode = MOTION_STATE_DEBUG;
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
                            "  momode request | momode set manual|stabilize|auto|debug\r\n"
                            "  params failsafe request | params failsafe depth_max <depth_max_m> | params failsafe voltage_low <voltage_low_v>\r\n"
                            "  log clear\r\n"
                            "  persistlog clear\r\n"
                            "  persist_log clear\r\n"
                            "  fault\r\n"
                            "  fault clear_overflow\r\n"
                            "  thruster request | idle | all <pct> | set <id> <pct> | pulse <id> <pct> <ms>\r\n"
                            "  servo set <angle>\r\n"
                            "  led auto | solid | breath | chase | warn | clearwarn\r\n"
                            "  ws2812 request | clear | all | color | set | pixel | refresh\r\n"
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
                                "              <target>: manual | stabilize | auto | debug");
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
                                    "              <target>: manual | stabilize | auto | debug");
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
                            "              <target>: manual | stabilize | auto | debug");
    return SHELL_RET_BAD_ARGS;
}

static shell_ret_t prv_cmd_params(shell_cmd_ctx_t *ctx, int argc, char **argv)
{
    bot_params_t params;
    float value;

    if ((argc == 3) && prv_streq_ignore_case(argv[1], "failsafe") &&
        prv_streq_ignore_case(argv[2], "request")) {
        Bot_Params_Pull(&params);
        System_ShellCore_Printf(ctx,
                                "failsafe depth_max=%.2f m voltage_low=%.2f V",
                                params.failsafe_max_depth,
                                params.failsafe_low_voltage);
        return SHELL_RET_OK;
    }

    if ((argc == 4) && prv_streq_ignore_case(argv[1], "failsafe") &&
        prv_streq_ignore_case(argv[2], "depth_max")) {
        if (!prv_parse_failsafe_value(argv[3],
                                      SHELL_FAILSAFE_MAX_DEPTH_MIN_M,
                                      SHELL_FAILSAFE_MAX_DEPTH_MAX_M,
                                      &value)) {
            System_ShellCore_Printf(ctx,
                                    "invalid depth_max: %.2f..%.2f m",
                                    SHELL_FAILSAFE_MAX_DEPTH_MIN_M,
                                    SHELL_FAILSAFE_MAX_DEPTH_MAX_M);
            return SHELL_RET_BAD_ARGS;
        }

        Bot_Params_Pull(&params);
        params.failsafe_max_depth = value;

        if (!Driver_PidParam_SaveNoReset(&params)) {
            System_ShellCore_Printf(ctx, "failsafe depth_max was not saved to flash");
            return SHELL_RET_INTERNAL;
        }

        (void)System_ShellCore_SendText(&ctx->peer,
                                        SHELL_RET_OK,
                                        "failsafe depth_max saved; rebooting");
        bsp_delay_ms(SHELL_FAILSAFE_REBOOT_DELAY_MS);
        bsp_cpu_reset();
        return SHELL_RET_OK;
    }

    if ((argc == 4) && prv_streq_ignore_case(argv[1], "failsafe") &&
        prv_streq_ignore_case(argv[2], "voltage_low")) {
        if (!prv_parse_failsafe_value(argv[3],
                                      SHELL_FAILSAFE_LOW_VOLTAGE_MIN_V,
                                      SHELL_FAILSAFE_LOW_VOLTAGE_MAX_V,
                                      &value)) {
            System_ShellCore_Printf(ctx,
                                    "invalid voltage_low: %.2f..%.2f V",
                                    SHELL_FAILSAFE_LOW_VOLTAGE_MIN_V,
                                    SHELL_FAILSAFE_LOW_VOLTAGE_MAX_V);
            return SHELL_RET_BAD_ARGS;
        }

        Bot_Params_Pull(&params);
        params.failsafe_low_voltage = value;

        if (!Driver_PidParam_SaveNoReset(&params)) {
            System_ShellCore_Printf(ctx, "failsafe voltage_low was not saved to flash");
            return SHELL_RET_INTERNAL;
        }

        (void)System_ShellCore_SendText(&ctx->peer,
                                        SHELL_RET_OK,
                                        "failsafe voltage_low saved; rebooting");
        bsp_delay_ms(SHELL_FAILSAFE_REBOOT_DELAY_MS);
        bsp_cpu_reset();
        return SHELL_RET_OK;
    }

    System_ShellCore_Printf(ctx,
                            "usage: params failsafe request | params failsafe depth_max <depth_max_m> | params failsafe voltage_low <voltage_low_v>");
    return SHELL_RET_BAD_ARGS;
}

static shell_ret_t prv_cmd_fault(shell_cmd_ctx_t *ctx, int argc, char **argv)
{
    if ((argc == 2) && prv_streq_ignore_case(argv[1], "clear_overflow")) {
        if (!System_FaultSnapshot_ClearStackOverflowTask()) {
            return SHELL_RET_INTERNAL;
        }

        System_ShellCore_Printf(ctx, "overflow snapshot cleared");
        return SHELL_RET_OK;
    }

    if (argc != 1) {
        System_ShellCore_Printf(ctx, "usage: fault | fault clear_overflow");
        return SHELL_RET_BAD_ARGS;
    }

    System_ShellCore_Printf(ctx, "fault_flags=0x%08lX", (unsigned long)ctx->fault_flags_snapshot);
    return SHELL_RET_OK;
}

static shell_ret_t prv_cmd_log(shell_cmd_ctx_t *ctx, int argc, char **argv)
{
    (void)ctx;

    if ((argc == 2) && prv_streq_ignore_case(argv[1], "clear")) {
        if (!System_Log_PersistClear()) {
            return SHELL_RET_INTERNAL;
        }

        System_ShellCore_Printf(ctx, "persist_log cleared");
        return SHELL_RET_OK;
    }

    System_ShellCore_Printf(ctx, "usage: log clear");
    return SHELL_RET_BAD_ARGS;
}

static shell_ret_t prv_cmd_reboot(shell_cmd_ctx_t *ctx, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    System_ShellCore_Printf(ctx, "Rebooting...");
    bsp_delay_ms(100);
    bsp_cpu_reset();
    return SHELL_RET_OK; // 实际上不会执行到这里
}

EXPORT_SHELL_CMD("help", "show command list", prv_cmd_help, SHELL_PERM_READONLY, SHELL_MODE_ANY);
EXPORT_SHELL_CMD("echo", "echo text", prv_cmd_echo, SHELL_PERM_READONLY, SHELL_MODE_ANY);
EXPORT_SHELL_CMD("sysmode", "sysmode request | sysmode set standby|disarmed|armed|failsafe", prv_cmd_sysmode, SHELL_PERM_SAFE_CTRL, SHELL_MODE_ANY);
EXPORT_SHELL_CMD("momode", "momode request | momode set manual|stabilize|auto|debug", prv_cmd_momode, SHELL_PERM_SAFE_CTRL, SHELL_MODE_ANY);
EXPORT_SHELL_CMD("params", "params failsafe request|depth_max <m>|voltage_low <V>", prv_cmd_params, SHELL_PERM_SAFE_CTRL, SHELL_MODE_MASK(SYS_MODE_STANDBY));
EXPORT_SHELL_CMD("log", "log clear (clear persisted logs)", prv_cmd_log, SHELL_PERM_SAFE_CTRL, SHELL_MODE_ANY);
EXPORT_SHELL_CMD("persistlog", "persistlog clear", prv_cmd_log, SHELL_PERM_SAFE_CTRL, SHELL_MODE_ANY);
EXPORT_SHELL_CMD("persist_log", "persist_log clear", prv_cmd_log, SHELL_PERM_SAFE_CTRL, SHELL_MODE_ANY);
EXPORT_SHELL_CMD("reboot", "reboot the system", prv_cmd_reboot, SHELL_PERM_SAFE_CTRL, SHELL_MODE_ANY);
EXPORT_SHELL_CMD("fault", "show fault flags", prv_cmd_fault, SHELL_PERM_READONLY, SHELL_MODE_ANY);
