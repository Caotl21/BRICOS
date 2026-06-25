#include <ctype.h>
#include <stdlib.h>

#include "bsp_delay.h"
#include "bsp_pwm.h"

#include "driver_servo.h"
#include "driver_thruster.h"
#include "driver_ws2812.h"

#include "sys_mode_manager.h"
#include "sys_shell_export.h"
#include "task_led.h"

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

static int prv_parse_u8(const char *s, uint8_t *out)
{
    int value;

    if ((s == NULL) || (out == NULL)) {
        return 0;
    }

    if (!prv_parse_int(s, &value) || (value < 0) || (value > 255)) {
        return 0;
    }

    *out = (uint8_t)value;
    return 1;
}

#define WS2812_SHELL_STRIP_1_MASK  (1U << WS2812_STRIP_1)
#define WS2812_SHELL_STRIP_2_MASK  (1U << WS2812_STRIP_2)
#define WS2812_SHELL_STRIP_ALL_MASK (WS2812_SHELL_STRIP_1_MASK | WS2812_SHELL_STRIP_2_MASK)
#define WS2812_SHELL_WAIT_TIMEOUT_MS 20U
#define LED_EFFECT_PERIOD_MS_MIN 50U
#define LED_EFFECT_PERIOD_MS_MAX 10000U

static int prv_parse_ws2812_strip_mask(const char *s, uint8_t *out_mask)
{
    if ((s == NULL) || (out_mask == NULL)) {
        return 0;
    }

    if (prv_streq_ignore_case(s, "1") || prv_streq_ignore_case(s, "strip1")) {
        *out_mask = WS2812_SHELL_STRIP_1_MASK;
        return 1;
    }

    if (prv_streq_ignore_case(s, "2") || prv_streq_ignore_case(s, "strip2")) {
        *out_mask = WS2812_SHELL_STRIP_2_MASK;
        return 1;
    }

    if (prv_streq_ignore_case(s, "all") || prv_streq_ignore_case(s, "both")) {
        *out_mask = WS2812_SHELL_STRIP_ALL_MASK;
        return 1;
    }

    return 0;
}

static int prv_parse_ws2812_single_strip(const char *s, ws2812_strip_t *out_strip)
{
    if ((s == NULL) || (out_strip == NULL)) {
        return 0;
    }

    if (prv_streq_ignore_case(s, "1") || prv_streq_ignore_case(s, "strip1")) {
        *out_strip = WS2812_STRIP_1;
        return 1;
    }

    if (prv_streq_ignore_case(s, "2") || prv_streq_ignore_case(s, "strip2")) {
        *out_strip = WS2812_STRIP_2;
        return 1;
    }

    return 0;
}

static int prv_parse_ws2812_color_name(const char *s, ws2812_rgb_t *out_color)
{
    if ((s == NULL) || (out_color == NULL)) {
        return 0;
    }

    if (prv_streq_ignore_case(s, "black") || prv_streq_ignore_case(s, "off")) {
        *out_color = WS2812_COLOR_BLACK;
        return 1;
    }
    if (prv_streq_ignore_case(s, "white")) {
        *out_color = WS2812_COLOR_WHITE;
        return 1;
    }
    if (prv_streq_ignore_case(s, "red")) {
        *out_color = WS2812_COLOR_RED;
        return 1;
    }
    if (prv_streq_ignore_case(s, "green")) {
        *out_color = WS2812_COLOR_GREEN;
        return 1;
    }
    if (prv_streq_ignore_case(s, "blue")) {
        *out_color = WS2812_COLOR_BLUE;
        return 1;
    }
    if (prv_streq_ignore_case(s, "yellow")) {
        *out_color = WS2812_COLOR_YELLOW;
        return 1;
    }
    if (prv_streq_ignore_case(s, "cyan")) {
        *out_color = WS2812_COLOR_CYAN;
        return 1;
    }
    if (prv_streq_ignore_case(s, "magenta") || prv_streq_ignore_case(s, "pink")) {
        *out_color = WS2812_COLOR_MAGENTA;
        return 1;
    }
    if (prv_streq_ignore_case(s, "orange")) {
        *out_color = WS2812_COLOR_ORANGE;
        return 1;
    }
    if (prv_streq_ignore_case(s, "purple") || prv_streq_ignore_case(s, "violet")) {
        *out_color = WS2812_COLOR_PURPLE;
        return 1;
    }

    return 0;
}

static int prv_parse_led_period_ms(const char *s, uint16_t *out_period_ms)
{
    int value;

    if ((s == NULL) || (out_period_ms == NULL)) {
        return 0;
    }

    if (!prv_parse_int(s, &value) ||
        (value < (int)LED_EFFECT_PERIOD_MS_MIN) ||
        (value > (int)LED_EFFECT_PERIOD_MS_MAX)) {
        return 0;
    }

    *out_period_ms = (uint16_t)value;
    return 1;
}

// 当前两路 WS2812 共用 TIM1，这里按顺序等待每一路发送完成，
// 避免第二路启动时把第一路还没发完的波形打断。
static int prv_ws2812_wait_strip_idle(ws2812_strip_t strip, uint32_t timeout_ms)
{
    uint32_t elapsed_ms;

    for (elapsed_ms = 0U; elapsed_ms < timeout_ms; elapsed_ms++) {
        if (!Driver_WS2812_IsBusy(strip)) {
            return 1;
        }

        bsp_delay_ms(1U);
    }

    return Driver_WS2812_IsBusy(strip) ? 0 : 1;
}

static int prv_ws2812_refresh_selected(uint8_t strip_mask)
{
    uint8_t i;

    for (i = 0U; i < WS2812_STRIP_MAX; i++) {
        ws2812_strip_t strip = (ws2812_strip_t)i;
        uint8_t bit = (uint8_t)(1U << i);

        if ((strip_mask & bit) == 0U) {
            continue;
        }

        if (!Driver_WS2812_Refresh(strip)) {
            return 0;
        }

        if (!prv_ws2812_wait_strip_idle(strip, WS2812_SHELL_WAIT_TIMEOUT_MS)) {
            return 0;
        }
    }

    return 1;
}

static void prv_ws2812_print_usage(shell_cmd_ctx_t *ctx)
{
    System_ShellCore_Printf(ctx,
                            "usage: ws2812 request | clear <strip 1|2|all> | all <strip> <r> <g> <b> | color <strip> <name> | set <strip 1|2> <led 1-N> <r> <g> <b> | pixel <strip 1|2> <led 1-N> <name> | refresh <strip>");
}

static void prv_ws2812_print_state(shell_cmd_ctx_t *ctx)
{
    System_ShellCore_Printf(ctx,
                            "ws2812 strip1: leds=%u busy=%u, strip2: leds=%u busy=%u",
                            (unsigned int)Driver_WS2812_GetLedCount(WS2812_STRIP_1),
                            (unsigned int)(Driver_WS2812_IsBusy(WS2812_STRIP_1) ? 1U : 0U),
                            (unsigned int)Driver_WS2812_GetLedCount(WS2812_STRIP_2),
                            (unsigned int)(Driver_WS2812_IsBusy(WS2812_STRIP_2) ? 1U : 0U));
    System_ShellCore_Printf(ctx,
                            "colors: black/off white red green blue yellow cyan magenta/pink orange purple/violet");
}

static void prv_led_print_usage(shell_cmd_ctx_t *ctx)
{
    System_ShellCore_Printf(ctx,
                            "usage: led auto | solid <color> | breath <color> [period_ms] | chase <color> [period_ms] | warn <color> [period_ms] | clearwarn");
    System_ShellCore_Printf(ctx,
                            "colors: black/off white red green blue yellow cyan magenta/pink orange purple/violet");
    System_ShellCore_Printf(ctx,
                            "period_ms range: %u..%u",
                            (unsigned int)LED_EFFECT_PERIOD_MS_MIN,
                            (unsigned int)LED_EFFECT_PERIOD_MS_MAX);
}

static shell_ret_t prv_cmd_led(shell_cmd_ctx_t *ctx, int argc, char **argv)
{
    led_effect_t effect;
    ws2812_rgb_t color;
    uint16_t period_ms = 1000u;
    bot_sys_mode_e mode;

    if (argc == 1) {
        prv_led_print_usage(ctx);
        return SHELL_RET_OK;
    }

    if ((argc == 2) && prv_streq_ignore_case(argv[1], "auto")) {
        mode = System_ModeManager_GetSysMode();
        Task_LED_SetMode(mode);
        System_ShellCore_Printf(ctx, "led auto mode restored for sysmode=%u", (unsigned int)mode);
        return SHELL_RET_OK;
    }

    if ((argc == 2) && prv_streq_ignore_case(argv[1], "clearwarn")) {
        Task_LED_ClearWarningEffect();
        System_ShellCore_Printf(ctx, "led warning effect cleared");
        return SHELL_RET_OK;
    }

    if ((argc == 3) || (argc == 4)) {
        if (!prv_parse_ws2812_color_name(argv[2], &color)) {
            System_ShellCore_Printf(ctx, "invalid color: %s", argv[2]);
            return SHELL_RET_BAD_ARGS;
        }

        if ((argc == 4) && (!prv_parse_led_period_ms(argv[3], &period_ms))) {
            System_ShellCore_Printf(ctx,
                                    "invalid period_ms: %s (range %u..%u)",
                                    argv[3],
                                    (unsigned int)LED_EFFECT_PERIOD_MS_MIN,
                                    (unsigned int)LED_EFFECT_PERIOD_MS_MAX);
            return SHELL_RET_BAD_ARGS;
        }

        memset(&effect, 0, sizeof(effect));
        effect.enabled = 1u;
        effect.color = color;
        effect.period_ms = period_ms;

        if (prv_streq_ignore_case(argv[1], "solid")) {
            effect.type = LED_EFFECT_SOLID;
            Task_LED_SetBaseEffect(&effect);
            Task_LED_ClearWarningEffect();
            System_ShellCore_Printf(ctx, "led solid color=%s", argv[2]);
            return SHELL_RET_OK;
        }

        if (prv_streq_ignore_case(argv[1], "breath")) {
            effect.type = LED_EFFECT_BREATH;
            Task_LED_SetBaseEffect(&effect);
            System_ShellCore_Printf(ctx, "led breath color=%s period_ms=%u", argv[2], (unsigned int)period_ms);
            return SHELL_RET_OK;
        }

        if (prv_streq_ignore_case(argv[1], "chase")) {
            effect.type = LED_EFFECT_CHASE;
            Task_LED_SetBaseEffect(&effect);
            System_ShellCore_Printf(ctx, "led chase color=%s period_ms=%u", argv[2], (unsigned int)period_ms);
            return SHELL_RET_OK;
        }

        if (prv_streq_ignore_case(argv[1], "warn")) {
            effect.type = LED_EFFECT_STROBE;
            Task_LED_SetWarningEffect(&effect);
            System_ShellCore_Printf(ctx, "led warning color=%s period_ms=%u", argv[2], (unsigned int)period_ms);
            return SHELL_RET_OK;
        }
    }

    prv_led_print_usage(ctx);
    return SHELL_RET_BAD_ARGS;
}

static shell_ret_t prv_cmd_ws2812(shell_cmd_ctx_t *ctx, int argc, char **argv)
{
    uint8_t strip_mask;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    int led_index;
    ws2812_strip_t strip;
    ws2812_rgb_t color;

    if (argc == 1) {
        prv_ws2812_print_usage(ctx);
        return SHELL_RET_OK;
    }

    if ((argc == 2) && prv_streq_ignore_case(argv[1], "request")) {
        prv_ws2812_print_state(ctx);
        return SHELL_RET_OK;
    }

    if ((argc == 3) && prv_streq_ignore_case(argv[1], "refresh")) {
        if (!prv_parse_ws2812_strip_mask(argv[2], &strip_mask)) {
            System_ShellCore_Printf(ctx, "invalid strip: %s (use 1|2|all)", argv[2]);
            return SHELL_RET_BAD_ARGS;
        }

        if (!prv_ws2812_refresh_selected(strip_mask)) {
            System_ShellCore_Printf(ctx, "ws2812 refresh failed or timeout");
            return SHELL_RET_INTERNAL;
        }

        System_ShellCore_Printf(ctx, "ws2812 refresh done for strip=%s", argv[2]);
        return SHELL_RET_OK;
    }

    if ((argc == 3) && prv_streq_ignore_case(argv[1], "clear")) {
        uint8_t i;

        if (!prv_parse_ws2812_strip_mask(argv[2], &strip_mask)) {
            System_ShellCore_Printf(ctx, "invalid strip: %s (use 1|2|all)", argv[2]);
            return SHELL_RET_BAD_ARGS;
        }

        for (i = 0U; i < WS2812_STRIP_MAX; i++) {
            if ((strip_mask & (uint8_t)(1U << i)) != 0U) {
                Driver_WS2812_Clear((ws2812_strip_t)i);
            }
        }

        if (!prv_ws2812_refresh_selected(strip_mask)) {
            System_ShellCore_Printf(ctx, "ws2812 clear refresh failed or timeout");
            return SHELL_RET_INTERNAL;
        }

        System_ShellCore_Printf(ctx, "ws2812 cleared strip=%s", argv[2]);
        return SHELL_RET_OK;
    }

    if ((argc == 6) && prv_streq_ignore_case(argv[1], "all")) {
        uint8_t i;

        if (!prv_parse_ws2812_strip_mask(argv[2], &strip_mask)) {
            System_ShellCore_Printf(ctx, "invalid strip: %s (use 1|2|all)", argv[2]);
            return SHELL_RET_BAD_ARGS;
        }
        if (!prv_parse_u8(argv[3], &r) || !prv_parse_u8(argv[4], &g) || !prv_parse_u8(argv[5], &b)) {
            System_ShellCore_Printf(ctx, "invalid rgb: use 0..255");
            return SHELL_RET_BAD_ARGS;
        }

        for (i = 0U; i < WS2812_STRIP_MAX; i++) {
            if ((strip_mask & (uint8_t)(1U << i)) != 0U) {
                Driver_WS2812_SetAllRGB((ws2812_strip_t)i, r, g, b);
            }
        }

        if (!prv_ws2812_refresh_selected(strip_mask)) {
            System_ShellCore_Printf(ctx, "ws2812 all refresh failed or timeout");
            return SHELL_RET_INTERNAL;
        }

        System_ShellCore_Printf(ctx,
                                "ws2812 strip=%s all rgb=(%u,%u,%u)",
                                argv[2],
                                (unsigned int)r,
                                (unsigned int)g,
                                (unsigned int)b);
        return SHELL_RET_OK;
    }

    if ((argc == 4) && prv_streq_ignore_case(argv[1], "color")) {
        uint8_t i;

        if (!prv_parse_ws2812_strip_mask(argv[2], &strip_mask)) {
            System_ShellCore_Printf(ctx, "invalid strip: %s (use 1|2|all)", argv[2]);
            return SHELL_RET_BAD_ARGS;
        }
        if (!prv_parse_ws2812_color_name(argv[3], &color)) {
            System_ShellCore_Printf(ctx, "invalid color: %s", argv[3]);
            return SHELL_RET_BAD_ARGS;
        }

        for (i = 0U; i < WS2812_STRIP_MAX; i++) {
            if ((strip_mask & (uint8_t)(1U << i)) != 0U) {
                Driver_WS2812_SetAllRGB((ws2812_strip_t)i, color.r, color.g, color.b);
            }
        }

        if (!prv_ws2812_refresh_selected(strip_mask)) {
            System_ShellCore_Printf(ctx, "ws2812 color refresh failed or timeout");
            return SHELL_RET_INTERNAL;
        }

        System_ShellCore_Printf(ctx, "ws2812 strip=%s color=%s", argv[2], argv[3]);
        return SHELL_RET_OK;
    }

    if ((argc == 7) && prv_streq_ignore_case(argv[1], "set")) {
        uint16_t led_count;

        if (!prv_parse_ws2812_single_strip(argv[2], &strip)) {
            System_ShellCore_Printf(ctx, "invalid strip: %s (use 1|2)", argv[2]);
            return SHELL_RET_BAD_ARGS;
        }
        if (!prv_parse_int(argv[3], &led_index) || (led_index < 1)) {
            System_ShellCore_Printf(ctx, "invalid led: %s (range 1..N)", argv[3]);
            return SHELL_RET_BAD_ARGS;
        }
        if (!prv_parse_u8(argv[4], &r) || !prv_parse_u8(argv[5], &g) || !prv_parse_u8(argv[6], &b)) {
            System_ShellCore_Printf(ctx, "invalid rgb: use 0..255");
            return SHELL_RET_BAD_ARGS;
        }

        led_count = Driver_WS2812_GetLedCount(strip);
        if ((uint16_t)led_index > led_count) {
            System_ShellCore_Printf(ctx, "invalid led: %s (range 1..%u)", argv[3], (unsigned int)led_count);
            return SHELL_RET_BAD_ARGS;
        }

        if (!Driver_WS2812_SetPixelRGB(strip, (uint16_t)(led_index - 1), r, g, b)) {
            System_ShellCore_Printf(ctx, "ws2812 set pixel failed");
            return SHELL_RET_INTERNAL;
        }
        if (!prv_ws2812_refresh_selected((uint8_t)(1U << strip))) {
            System_ShellCore_Printf(ctx, "ws2812 set refresh failed or timeout");
            return SHELL_RET_INTERNAL;
        }

        System_ShellCore_Printf(ctx,
                                "ws2812 strip=%s led=%d rgb=(%u,%u,%u)",
                                argv[2],
                                led_index,
                                (unsigned int)r,
                                (unsigned int)g,
                                (unsigned int)b);
        return SHELL_RET_OK;
    }

    if ((argc == 5) && prv_streq_ignore_case(argv[1], "pixel")) {
        uint16_t led_count;

        if (!prv_parse_ws2812_single_strip(argv[2], &strip)) {
            System_ShellCore_Printf(ctx, "invalid strip: %s (use 1|2)", argv[2]);
            return SHELL_RET_BAD_ARGS;
        }
        if (!prv_parse_int(argv[3], &led_index) || (led_index < 1)) {
            System_ShellCore_Printf(ctx, "invalid led: %s (range 1..N)", argv[3]);
            return SHELL_RET_BAD_ARGS;
        }
        if (!prv_parse_ws2812_color_name(argv[4], &color)) {
            System_ShellCore_Printf(ctx, "invalid color: %s", argv[4]);
            return SHELL_RET_BAD_ARGS;
        }

        led_count = Driver_WS2812_GetLedCount(strip);
        if ((uint16_t)led_index > led_count) {
            System_ShellCore_Printf(ctx, "invalid led: %s (range 1..%u)", argv[3], (unsigned int)led_count);
            return SHELL_RET_BAD_ARGS;
        }

        if (!Driver_WS2812_SetPixel(strip, (uint16_t)(led_index - 1), color)) {
            System_ShellCore_Printf(ctx, "ws2812 set pixel failed");
            return SHELL_RET_INTERNAL;
        }
        if (!prv_ws2812_refresh_selected((uint8_t)(1U << strip))) {
            System_ShellCore_Printf(ctx, "ws2812 pixel refresh failed or timeout");
            return SHELL_RET_INTERNAL;
        }

        System_ShellCore_Printf(ctx, "ws2812 strip=%s led=%d color=%s", argv[2], led_index, argv[4]);
        return SHELL_RET_OK;
    }

    prv_ws2812_print_usage(ctx);
    return SHELL_RET_BAD_ARGS;
}

static shell_ret_t prv_thruster_mode_guard(shell_cmd_ctx_t *ctx)
{
    bot_sys_mode_e mode = System_ModeManager_GetSysMode();

    if ((mode != SYS_MODE_STANDBY) && (mode != SYS_MODE_ACTIVE_DISARMED) && (mode != SYS_MODE_MOTION_ARMED)) {
        System_ShellCore_Printf(ctx,
                                "blocked: mode=%u, allow standby/disarmed/armed only",
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
EXPORT_SHELL_CMD("ws2812", "ws2812 request|clear|all|color|set|pixel|refresh", prv_cmd_ws2812, SHELL_PERM_SAFE_CTRL, SHELL_MODE_ANY);
EXPORT_SHELL_CMD("led", "led auto|solid|breath|chase|warn|clearwarn", prv_cmd_led, SHELL_PERM_SAFE_CTRL, SHELL_MODE_ANY);
