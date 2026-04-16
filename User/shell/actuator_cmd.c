#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "driver_servo.h"
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

/* servo：设置舵机角度 */
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

EXPORT_SHELL_CMD("servo", "control servo angle", prv_cmd_servo, SHELL_PERM_SAFE_CTRL, SHELL_MODE_ANY);
