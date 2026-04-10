#include <ctype.h>
#include <string.h>

#include "driver_imu.h"
#include "sys_data_pool.h"
#include "sys_shell_export.h"

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

/* euler：查询欧拉角 */
static shell_ret_t prv_cmd_euler(shell_cmd_ctx_t *ctx, int argc, char **argv)
{
    bot_body_state_t body_state;
    float roll = 0.0f;
    float pitch = 0.0f;
    float yaw = 0.0f;

    if (argc == 1) {
        System_ShellCore_Printf(ctx, "usage: euler request");
        return SHELL_RET_OK;
    }

    if ((argc == 2) && prv_streq_ignore_case(argv[1], "request")) {
        Bot_State_Pull(&body_state);
        Driver_IMU_Quaternion_ToEuler_Deg(body_state.Quater, &roll, &pitch, &yaw);
        System_ShellCore_Printf(ctx, "roll=%.2f pitch=%.2f yaw=%.2f", roll, pitch, yaw);
        return SHELL_RET_OK;
    }

    System_ShellCore_Printf(ctx, "usage: euler request");
    return SHELL_RET_BAD_ARGS;
}

/* depthtemp：查询深度与水温 */
static shell_ret_t prv_cmd_depthtemp(shell_cmd_ctx_t *ctx, int argc, char **argv)
{
    bot_body_state_t body_state;
    bot_sys_state_t sys_state;

    if (argc == 1) {
        System_ShellCore_Printf(ctx, "usage: depthtemp request");
        return SHELL_RET_OK;
    }

    if ((argc == 2) && prv_streq_ignore_case(argv[1], "request")) {
        Bot_State_Pull(&body_state);
        Bot_Sys_State_Pull(&sys_state);
        System_ShellCore_Printf(ctx, "depth=%.3f water_temp=%.2f", body_state.depth_m, sys_state.water_temp_c);
        return SHELL_RET_OK;
    }

    System_ShellCore_Printf(ctx, "usage: depthtemp request");
    return SHELL_RET_BAD_ARGS;
}

/* power：查询电源状态 */
static shell_ret_t prv_cmd_power(shell_cmd_ctx_t *ctx, int argc, char **argv)
{
    bot_sys_state_t sys_state;

    if (argc == 1) {
        System_ShellCore_Printf(ctx, "usage: power request");
        return SHELL_RET_OK;
    }

    if ((argc == 2) && prv_streq_ignore_case(argv[1], "request")) {
        Bot_Sys_State_Pull(&sys_state);
        System_ShellCore_Printf(ctx, "voltage=%.2f current=%.2f", sys_state.bat_voltage_v, sys_state.bat_current_a);
        return SHELL_RET_OK;
    }

    System_ShellCore_Printf(ctx, "usage: power request");
    return SHELL_RET_BAD_ARGS;
}

/* cabin：查询舱内温湿度 */
static shell_ret_t prv_cmd_cabin(shell_cmd_ctx_t *ctx, int argc, char **argv)
{
    bot_sys_state_t sys_state;

    if (argc == 1) {
        System_ShellCore_Printf(ctx, "usage: cabin request");
        return SHELL_RET_OK;
    }

    if ((argc == 2) && prv_streq_ignore_case(argv[1], "request")) {
        Bot_Sys_State_Pull(&sys_state);
        System_ShellCore_Printf(ctx, "cabin_temp=%.2f cabin_humi=%.2f", sys_state.cabin_temp_c, sys_state.cabin_humi);
        return SHELL_RET_OK;
    }

    System_ShellCore_Printf(ctx, "usage: cabin request");
    return SHELL_RET_BAD_ARGS;
}

/* chip：查询 CPU 占用和芯片温度 */
static shell_ret_t prv_cmd_chip(shell_cmd_ctx_t *ctx, int argc, char **argv)
{
    bot_sys_state_t sys_state;

    if (argc == 1) {
        System_ShellCore_Printf(ctx, "usage: chip request");
        return SHELL_RET_OK;
    }

    if ((argc == 2) && prv_streq_ignore_case(argv[1], "request")) {
        Bot_Sys_State_Pull(&sys_state);
        System_ShellCore_Printf(ctx, "cpu=%.2f chip_temp=%.2f", sys_state.cpu_usage, sys_state.chip_temp);
        return SHELL_RET_OK;
    }

    System_ShellCore_Printf(ctx, "usage: chip request");
    return SHELL_RET_BAD_ARGS;
}

EXPORT_SHELL_CMD("euler", "euler request", prv_cmd_euler, SHELL_PERM_READONLY, SHELL_MODE_ANY);
EXPORT_SHELL_CMD("depthtemp", "depthtemp request", prv_cmd_depthtemp, SHELL_PERM_READONLY, SHELL_MODE_ANY);
EXPORT_SHELL_CMD("power", "power request", prv_cmd_power, SHELL_PERM_READONLY, SHELL_MODE_ANY);
EXPORT_SHELL_CMD("cabin", "cabin request", prv_cmd_cabin, SHELL_PERM_READONLY, SHELL_MODE_ANY);
EXPORT_SHELL_CMD("chip", "chip request", prv_cmd_chip, SHELL_PERM_READONLY, SHELL_MODE_ANY);
