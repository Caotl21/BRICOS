#include <ctype.h>
#include <stdlib.h>

#include "bsp_delay.h"
#include "bsp_pwm.h"

#include "driver_servo.h"
#include "driver_thruster.h"

#include "sys_mode_manager.h"
#include "sys_shell_export.h"

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

static int prv_parse_int(const char *s, int *out)
{
    char *end = NULL;
    long value;

    if ((s == NULL) || (out == NULL)) {
        return 0;
    }

    value = strtol(s, &end, 10);
    if ((end == NULL) || (*end != '\0')) {
        return 0;
    }

    *out = (int)value;
    return 1;
}

static int prv_parse_percent(const char *s, float *out)
{
    char *end = NULL;
    double value;

    if ((s == NULL) || (out == NULL)) {
        return 0;
    }

    value = strtod(s, &end);
    if ((end == NULL) || (*end != '\0')) {
        return 0;
    }
    if ((value < -100.0f) || (value > 100.0f)) {
        return 0;
    }

    *out = (float)value;
    return 1;
}

static shell_ret_t prv_thruster_mode_guard(shell_cmd_ctx_t *ctx)
{
    bot_sys_mode_e mode = System_ModeManager_GetSysMode();

    if ((mode != SYS_MODE_STANDBY) && (mode != SYS_MODE_ACTIVE_DISARMED)) {
        System_ShellCore_Printf(ctx,
                                "blocked: mode=%u, allow standby/disarmed only",
                                (unsigned int)mode);
        return SHELL_RET_MODE_BLOCKED;
    }

    return SHELL_RET_OK;
}

static void prv_thruster_print_state(shell_cmd_ctx_t *ctx)
{
    uint16_t pwm[THRUSTER_COUNT];
    int i;

    for (i = 0; i < THRUSTER_COUNT; i++) {
        pwm[i] = bsp_pwm_get_pulse_us((bsp_pwm_ch_t)(BSP_PWM_THRUSTER_1 + i));
    }

    System_ShellCore_Printf(ctx,
                            "thruster pwm: [%u, %u, %u, %u, %u, %u]",
                            (unsigned int)pwm[0],
                            (unsigned int)pwm[1],
                            (unsigned int)pwm[2],
                            (unsigned int)pwm[3],
                            (unsigned int)pwm[4],
                            (unsigned int)pwm[5]);
}

static shell_ret_t prv_cmd_thruster(shell_cmd_ctx_t *ctx, int argc, char **argv)
{
    shell_ret_t guard_ret;
    float percent;
    int idx;
    int ms;

    if (argc == 1) {
        System_ShellCore_Printf(ctx,
                                "usage: thruster request | idle | all <pct> | set <id 1-6> <pct> | pulse <id> <pct> <ms>");
        return SHELL_RET_OK;
    }

    if ((argc == 2) && prv_streq_ignore_case(argv[1], "request")) {
        prv_thruster_print_state(ctx);
        return SHELL_RET_OK;
    }

    guard_ret = prv_thruster_mode_guard(ctx);
    if (guard_ret != SHELL_RET_OK) {
        return guard_ret;
    }

    if ((argc == 2) && (prv_streq_ignore_case(argv[1], "idle") || prv_streq_ignore_case(argv[1], "stop"))) {
        Driver_Thruster_Set_Idle();
        System_ShellCore_Printf(ctx, "thruster idle");
        return SHELL_RET_OK;
    }

    if ((argc == 3) && prv_streq_ignore_case(argv[1], "all")) {
        int i;

        if (!prv_parse_percent(argv[2], &percent)) {
            System_ShellCore_Printf(ctx, "invalid pct: %s (range -100..100)", argv[2]);
            return SHELL_RET_BAD_ARGS;
        }

        for (i = 0; i < THRUSTER_COUNT; i++) {
            Driver_Thruster_SetSpeed((bsp_pwm_ch_t)(BSP_PWM_THRUSTER_1 + i), percent);
        }

        System_ShellCore_Printf(ctx, "thruster all set to %.1f%%", (double)percent);
        return SHELL_RET_OK;
    }

    if ((argc == 4) && prv_streq_ignore_case(argv[1], "set")) {
        if (!prv_parse_int(argv[2], &idx) || (idx < 1) || (idx > THRUSTER_COUNT)) {
            System_ShellCore_Printf(ctx, "invalid id: %s (range 1..6)", argv[2]);
            return SHELL_RET_BAD_ARGS;
        }
        if (!prv_parse_percent(argv[3], &percent)) {
            System_ShellCore_Printf(ctx, "invalid pct: %s (range -100..100)", argv[3]);
            return SHELL_RET_BAD_ARGS;
        }

        Driver_Thruster_SetSpeed((bsp_pwm_ch_t)(BSP_PWM_THRUSTER_1 + (idx - 1)), percent);
        System_ShellCore_Printf(ctx, "thruster %d set to %.1f%%", idx, (double)percent);
        return SHELL_RET_OK;
    }

    if ((argc == 5) && prv_streq_ignore_case(argv[1], "pulse")) {
        if (!prv_parse_int(argv[2], &idx) || (idx < 1) || (idx > THRUSTER_COUNT)) {
            System_ShellCore_Printf(ctx, "invalid id: %s (range 1..6)", argv[2]);
            return SHELL_RET_BAD_ARGS;
        }
        if (!prv_parse_percent(argv[3], &percent)) {
            System_ShellCore_Printf(ctx, "invalid pct: %s (range -100..100)", argv[3]);
            return SHELL_RET_BAD_ARGS;
        }
        if (!prv_parse_int(argv[4], &ms) || (ms < 1) || (ms > 60000)) {
            System_ShellCore_Printf(ctx, "invalid ms: %s (range 1..60000)", argv[4]);
            return SHELL_RET_BAD_ARGS;
        }

        Driver_Thruster_SetSpeed((bsp_pwm_ch_t)(BSP_PWM_THRUSTER_1 + (idx - 1)), percent);
        bsp_delay_ms((uint32_t)ms);
        Driver_Thruster_Set_Idle();
        System_ShellCore_Printf(ctx,
                                "thruster %d pulse %.1f%% for %dms, then idle",
                                idx,
                                (double)percent,
                                ms);
        return SHELL_RET_OK;
    }

    System_ShellCore_Printf(ctx,
                            "usage: thruster request | idle | all <pct> | set <id 1-6> <pct> | pulse <id> <pct> <ms>");
    return SHELL_RET_BAD_ARGS;
}

static shell_ret_t prv_cmd_servo(shell_cmd_ctx_t *ctx, int argc, char **argv)
{
    if (argc == 1) {
        System_ShellCore_Printf(ctx, "usage: servo set <angle>");
        return SHELL_RET_OK;
    }

    if ((argc == 3) && prv_streq_ignore_case(argv[1], "set")) {
        int angle = atoi(argv[2]);
        Driver_Servo_SetAngle(BSP_PWM_SERVO_2, angle);
        System_ShellCore_Printf(ctx, "servo angle set to %d", angle);
        return SHELL_RET_OK;
    }

    System_ShellCore_Printf(ctx, "usage: servo set <angle>");
    return SHELL_RET_BAD_ARGS;
}

EXPORT_SHELL_CMD("thruster", "thruster request|idle|all|set|pulse", prv_cmd_thruster, SHELL_PERM_SAFE_CTRL, SHELL_MODE_ANY);
EXPORT_SHELL_CMD("servo", "control servo angle", prv_cmd_servo, SHELL_PERM_SAFE_CTRL, SHELL_MODE_ANY);
