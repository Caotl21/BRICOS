#include "task_led.h"

#include <string.h>

#include "queue.h"

#include "sys_log.h"

#define LED_CMD_QUEUE_DEPTH        8u
#define LED_FRAME_PERIOD_MS        50u
#define LED_REFRESH_TIMEOUT_US     20000u
#define LED_DEFAULT_EFFECT_PERIOD  1000u
#define LED_BREATH_MS               2000u
#define LED_FAILSAFE_STROBE_MS     200u
#define LED_ARMING_CHASE_MS         600u

typedef enum {
    LED_CMD_SET_MODE = 0,
    LED_CMD_SET_BASE_EFFECT,
    LED_CMD_SET_WARNING_EFFECT,
    LED_CMD_CLEAR_WARNING_EFFECT
} led_cmd_type_t;

typedef struct {
    led_cmd_type_t type;
    union {
        bot_sys_mode_e mode;
        led_effect_t effect;
    } data;
} led_cmd_t;

typedef struct {
    bot_sys_mode_e mode;
    led_effect_t base_effect;
    led_effect_t warning_effect;
    uint8_t dirty;
} led_state_t;

TaskHandle_t LED_Task_Handler = NULL;

static QueueHandle_t s_led_cmd_queue = NULL;

static uint8_t prv_effect_is_dynamic(const led_effect_t *effect)
{
    if ((effect == NULL) || (!effect->enabled)) {
        return 0u;
    }

    return (uint8_t)(effect->type != LED_EFFECT_SOLID);
}

static uint16_t prv_effect_period_ms(const led_effect_t *effect)
{
    if ((effect == NULL) || (effect->period_ms == 0u)) {
        return LED_DEFAULT_EFFECT_PERIOD;
    }

    return effect->period_ms;
}

static uint8_t prv_scale_u8(uint8_t value, uint8_t brightness)
{
    return (uint8_t)(((uint16_t)value * (uint16_t)brightness) / 255u);
}

static led_effect_t prv_mode_to_base_effect(bot_sys_mode_e mode)
{
    led_effect_t effect;

    memset(&effect, 0, sizeof(effect));
    effect.enabled = 1u;
    effect.type = LED_EFFECT_SOLID;
    effect.period_ms = LED_DEFAULT_EFFECT_PERIOD;

    switch (mode) {
        case SYS_MODE_STANDBY:
            effect.type = LED_EFFECT_BREATH;
            effect.color = WS2812_COLOR_WHITE;
            effect.period_ms = LED_BREATH_MS;
            break;
        case SYS_MODE_ACTIVE_DISARMED:
            effect.type = LED_EFFECT_BREATH;
            effect.color = WS2812_COLOR_MAGENTA;
            effect.period_ms = LED_BREATH_MS;
            break;
        case SYS_MODE_MOTION_ARMED:
            effect.color = WS2812_COLOR_BLUE;
            break;
        case SYS_MODE_FAILSAFE:
            effect.color = WS2812_COLOR_BLACK;
            break;
        default:
            effect.color = WS2812_COLOR_BLACK;
            break;
    }

    return effect;
}

static led_effect_t prv_mode_to_warning_effect(bot_sys_mode_e mode)
{
    led_effect_t effect;

    memset(&effect, 0, sizeof(effect));

    if (mode == SYS_MODE_FAILSAFE) {
        effect.enabled = 1u;
        effect.type = LED_EFFECT_STROBE;
        effect.color = WS2812_COLOR_RED;
        effect.period_ms = LED_FAILSAFE_STROBE_MS;
    }

    return effect;
}

static uint8_t prv_effect_active_on_frame(const led_effect_t *effect, TickType_t now_tick)
{
    TickType_t period_ticks;
    TickType_t phase_ticks;

    if ((effect == NULL) || (!effect->enabled)) {
        return 0u;
    }

    if (effect->type != LED_EFFECT_STROBE) {
        return 1u;
    }

    period_ticks = pdMS_TO_TICKS(prv_effect_period_ms(effect));
    if (period_ticks == 0u) {
        period_ticks = 1u;
    }

    phase_ticks = (TickType_t)(now_tick % period_ticks);
    return (uint8_t)(phase_ticks < (period_ticks / 2u)) ? 1u : 0u;
}

static uint8_t prv_effect_breath_brightness(const led_effect_t *effect, TickType_t now_tick)
{
    TickType_t period_ticks;
    TickType_t half_ticks;
    TickType_t phase_ticks;

    period_ticks = pdMS_TO_TICKS(prv_effect_period_ms(effect));
    if (period_ticks < 2u) {
        return 255u;
    }

    half_ticks = period_ticks / 2u;
    if (half_ticks == 0u) {
        return 255u;
    }

    phase_ticks = (TickType_t)(now_tick % period_ticks);
    if (phase_ticks < half_ticks) {
        return (uint8_t)((phase_ticks * 255u) / half_ticks);
    }

    phase_ticks -= half_ticks;
    return (uint8_t)(255u - ((phase_ticks * 255u) / half_ticks));
}

static void prv_render_solid_or_breath(const led_effect_t *effect, TickType_t now_tick)
{
    ws2812_rgb_t color = effect->color;

    if (effect->type == LED_EFFECT_BREATH) {
        uint8_t brightness = prv_effect_breath_brightness(effect, now_tick);
        color.r = prv_scale_u8(color.r, brightness);
        color.g = prv_scale_u8(color.g, brightness);
        color.b = prv_scale_u8(color.b, brightness);
    }

    Driver_WS2812_SetAllStripsRGB(color.r, color.g, color.b);
}

static void prv_render_chase(const led_effect_t *effect, TickType_t now_tick)
{
    uint8_t strip_idx;
    TickType_t period_ticks;

    period_ticks = pdMS_TO_TICKS(prv_effect_period_ms(effect));
    if (period_ticks == 0u) {
        period_ticks = 1u;
    }

    for (strip_idx = 0u; strip_idx < WS2812_STRIP_MAX; strip_idx++) {
        uint16_t led_count = Driver_WS2812_GetLedCount((ws2812_strip_t)strip_idx);
        TickType_t step_ticks;
        uint16_t active_led;
        uint16_t led_idx;

        Driver_WS2812_SetAllRGB((ws2812_strip_t)strip_idx, 0u, 0u, 0u);
        if (led_count == 0u) {
            continue;
        }

        step_ticks = period_ticks / led_count;
        if (step_ticks == 0u) {
            step_ticks = 1u;
        }

        active_led = (uint16_t)((now_tick / step_ticks) % led_count);
        for (led_idx = 0u; led_idx < led_count; led_idx++) {
            if (led_idx == active_led) {
                (void)Driver_WS2812_SetPixel((ws2812_strip_t)strip_idx, led_idx, effect->color);
            }
        }
    }
}

static void prv_render_effect_frame(const led_effect_t *effect, TickType_t now_tick)
{
    if ((effect == NULL) || (!effect->enabled)) {
        Driver_WS2812_ClearAll();
        return;
    }

    switch (effect->type) {
        case LED_EFFECT_SOLID:
        case LED_EFFECT_BREATH:
        case LED_EFFECT_STROBE:
            prv_render_solid_or_breath(effect, now_tick);
            break;
        case LED_EFFECT_CHASE:
            prv_render_chase(effect, now_tick);
            break;
        default:
            Driver_WS2812_ClearAll();
            break;
    }
}

static uint8_t prv_refresh_all_blocking(void)
{
    uint8_t strip_idx;

    for (strip_idx = 0u; strip_idx < WS2812_STRIP_MAX; strip_idx++) {
        if (!Driver_WS2812_RefreshBlocking((ws2812_strip_t)strip_idx, LED_REFRESH_TIMEOUT_US)) {
            return 0u;
        }
    }

    return 1u;
}

static uint8_t prv_render_current_frame(const led_state_t *state, TickType_t now_tick)
{
    if (state == NULL) {
        return 0u;
    }

    prv_render_effect_frame(&state->base_effect, now_tick);

    if (prv_effect_active_on_frame(&state->warning_effect, now_tick)) {
        prv_render_effect_frame(&state->warning_effect, now_tick);
    }

    return prv_refresh_all_blocking();
}

static void prv_apply_mode(led_state_t *state, bot_sys_mode_e mode)
{
    if (state == NULL) {
        return;
    }

    state->mode = mode;
    state->base_effect = prv_mode_to_base_effect(mode);
    state->warning_effect = prv_mode_to_warning_effect(mode);
    state->dirty = 1u;
}

static void prv_handle_cmd(led_state_t *state, const led_cmd_t *cmd)
{
    if ((state == NULL) || (cmd == NULL)) {
        return;
    }

    switch (cmd->type) {
        case LED_CMD_SET_MODE:
            prv_apply_mode(state, cmd->data.mode);
            break;
        case LED_CMD_SET_BASE_EFFECT:
            state->base_effect = cmd->data.effect;
            state->dirty = 1u;
            break;
        case LED_CMD_SET_WARNING_EFFECT:
            state->warning_effect = cmd->data.effect;
            state->dirty = 1u;
            break;
        case LED_CMD_CLEAR_WARNING_EFFECT:
            memset(&state->warning_effect, 0, sizeof(state->warning_effect));
            state->dirty = 1u;
            break;
        default:
            break;
    }
}

static uint8_t prv_post_cmd(const led_cmd_t *cmd)
{
    if ((cmd == NULL) || (s_led_cmd_queue == NULL)) {
        return 0u;
    }

    return (uint8_t)(xQueueSend(s_led_cmd_queue, cmd, 0u) == pdPASS);
}

static void vTask_LED_Core(void *pvParameters)
{
    led_state_t state;
    led_cmd_t cmd;

    (void)pvParameters;

    memset(&state, 0, sizeof(state));
    prv_apply_mode(&state, SYS_MODE_STANDBY);

    for (;;) {
        TickType_t wait_ticks;
        uint8_t has_dynamic;
        TickType_t now_tick;

        has_dynamic = (uint8_t)(prv_effect_is_dynamic(&state.base_effect) ||
                                prv_effect_is_dynamic(&state.warning_effect));
        wait_ticks = has_dynamic ? pdMS_TO_TICKS(LED_FRAME_PERIOD_MS) : portMAX_DELAY;

        if ((s_led_cmd_queue != NULL) && (xQueueReceive(s_led_cmd_queue, &cmd, wait_ticks) == pdPASS)) {
            prv_handle_cmd(&state, &cmd);
        }

        now_tick = xTaskGetTickCount();
        has_dynamic = (uint8_t)(prv_effect_is_dynamic(&state.base_effect) ||
                                prv_effect_is_dynamic(&state.warning_effect));

        if ((!state.dirty) && (!has_dynamic)) {
            continue;
        }

        if (!prv_render_current_frame(&state, now_tick)) {
            LOG_WARNING("LED frame render failed");
        }
        state.dirty = 0u;
    }
}

void Task_LED_Init(void)
{
    if (s_led_cmd_queue == NULL) {
        s_led_cmd_queue = xQueueCreate(LED_CMD_QUEUE_DEPTH, sizeof(led_cmd_t));
    }

    if ((s_led_cmd_queue != NULL) && (LED_Task_Handler == NULL)) {
        xTaskCreate((TaskFunction_t)vTask_LED_Core,
                    (const char *)"Task_LED",
                    (uint16_t)LED_STK_SIZE,
                    NULL,
                    (UBaseType_t)LED_TASK_PRIO,
                    &LED_Task_Handler);
    }
}

void Task_LED_SetMode(bot_sys_mode_e mode)
{
    led_cmd_t cmd;

    memset(&cmd, 0, sizeof(cmd));
    cmd.type = LED_CMD_SET_MODE;
    cmd.data.mode = mode;
    (void)prv_post_cmd(&cmd);
}

void Task_LED_SetArmingEffect(void)
{
    led_effect_t effect;

    memset(&effect, 0, sizeof(effect));
    effect.enabled = 1u;
    effect.type = LED_EFFECT_CHASE;
    effect.color = WS2812_COLOR_CYAN;
    effect.period_ms = LED_ARMING_CHASE_MS;

    Task_LED_SetBaseEffect(&effect);
}

void Task_LED_SetBaseEffect(const led_effect_t *effect)
{
    led_cmd_t cmd;

    if (effect == NULL) {
        return;
    }

    memset(&cmd, 0, sizeof(cmd));
    cmd.type = LED_CMD_SET_BASE_EFFECT;
    cmd.data.effect = *effect;
    (void)prv_post_cmd(&cmd);
}

void Task_LED_SetWarningEffect(const led_effect_t *effect)
{
    led_cmd_t cmd;

    if (effect == NULL) {
        return;
    }

    memset(&cmd, 0, sizeof(cmd));
    cmd.type = LED_CMD_SET_WARNING_EFFECT;
    cmd.data.effect = *effect;
    (void)prv_post_cmd(&cmd);
}

void Task_LED_ClearWarningEffect(void)
{
    led_cmd_t cmd;

    memset(&cmd, 0, sizeof(cmd));
    cmd.type = LED_CMD_CLEAR_WARNING_EFFECT;
    (void)prv_post_cmd(&cmd);
}
