#include "driver_ws2812.h"

#include "bsp_delay.h"
#include "bsp_pwm.h"

#include <string.h>

#define WS2812_POWER_ON_SETTLE_MS   2U
#define WS2812_WAIT_IDLE_TIMEOUT_US 3000U

typedef struct {
    bsp_pwm_ch_t pwm_ch;
    uint16_t led_count;
    ws2812_rgb_t *pixel_buf;
    uint16_t *waveform_buf;
    uint16_t waveform_len;
} ws2812_ctx_t;

static ws2812_rgb_t s_strip_1_pixels[WS2812_STRIP_1_LED_COUNT];
static ws2812_rgb_t s_strip_2_pixels[WS2812_STRIP_2_LED_COUNT];

static uint16_t s_strip_1_waveform[WS2812_STRIP_1_LED_COUNT * WS2812_BITS_PER_LED + WS2812_RESET_SLOT_COUNT];
static uint16_t s_strip_2_waveform[WS2812_STRIP_2_LED_COUNT * WS2812_BITS_PER_LED + WS2812_RESET_SLOT_COUNT];

static ws2812_ctx_t s_ws2812_ctx[WS2812_STRIP_MAX] = {
    [WS2812_STRIP_1] = {
        .pwm_ch = BSP_PWM_LED_1,
        .led_count = WS2812_STRIP_1_LED_COUNT,
        .pixel_buf = s_strip_1_pixels,
        .waveform_buf = s_strip_1_waveform,
        .waveform_len = (WS2812_STRIP_1_LED_COUNT * WS2812_BITS_PER_LED + WS2812_RESET_SLOT_COUNT)
    },
    [WS2812_STRIP_2] = {
        .pwm_ch = BSP_PWM_LED_2,
        .led_count = WS2812_STRIP_2_LED_COUNT,
        .pixel_buf = s_strip_2_pixels,
        .waveform_buf = s_strip_2_waveform,
        .waveform_len = (WS2812_STRIP_2_LED_COUNT * WS2812_BITS_PER_LED + WS2812_RESET_SLOT_COUNT)
    }
};

static ws2812_init_status_t s_ws2812_last_init_status = WS2812_INIT_STATUS_OK;

static bool prv_ws2812_strip_is_valid(ws2812_strip_t strip)
{
    return strip < WS2812_STRIP_MAX;
}

static ws2812_ctx_t *prv_ws2812_get_ctx(ws2812_strip_t strip)
{
    if (!prv_ws2812_strip_is_valid(strip)) {
        return NULL;
    }

    return &s_ws2812_ctx[strip];
}

static bool prv_ws2812_any_strip_busy(void)
{
    uint8_t i;

    for (i = 0U; i < WS2812_STRIP_MAX; i++) {
        if (bsp_pwm_is_dma_waveform_busy(s_ws2812_ctx[i].pwm_ch)) {
            return true;
        }
    }

    return false;
}

static void prv_ws2812_abort_all(void)
{
    uint8_t i;

    for (i = 0U; i < WS2812_STRIP_MAX; i++) {
        bsp_pwm_abort_dma_waveform(s_ws2812_ctx[i].pwm_ch);
    }
}

static void prv_ws2812_poll_all(void)
{
    uint8_t i;

    for (i = 0U; i < WS2812_STRIP_MAX; i++) {
        bsp_pwm_poll_dma_waveform(s_ws2812_ctx[i].pwm_ch);
    }
}

static bool prv_ws2812_wait_all_idle(uint32_t timeout_us)
{
    uint32_t elapsed_us = 0U;

    while (prv_ws2812_any_strip_busy()) {
        /*
         * Init runs before the scheduler. Polling the DMA status here makes
         * completion independent from whether the DMA IRQ has already fired.
         */
        prv_ws2812_poll_all();
        if (!prv_ws2812_any_strip_busy()) {
            return true;
        }

        if (elapsed_us >= timeout_us) {
            prv_ws2812_abort_all();
            return false;
        }

        bsp_delay_us(1U);
        elapsed_us++;
    }

    return true;
}

static uint16_t *prv_ws2812_encode_byte(uint16_t *buf, uint8_t value)
{
    uint8_t mask;

    for (mask = 0x80U; mask != 0U; mask >>= 1U) {
        *buf++ = ((value & mask) != 0U) ? WS2812_CCR_BIT_1 : WS2812_CCR_BIT_0;
    }

    return buf;
}

static void prv_ws2812_encode_strip(ws2812_ctx_t *ctx)
{
    uint16_t i;
    uint16_t *buf;

    if (ctx == NULL) {
        return;
    }

    buf = ctx->waveform_buf;
    for (i = 0U; i < ctx->led_count; i++) {
        buf = prv_ws2812_encode_byte(buf, ctx->pixel_buf[i].g);
        buf = prv_ws2812_encode_byte(buf, ctx->pixel_buf[i].r);
        buf = prv_ws2812_encode_byte(buf, ctx->pixel_buf[i].b);
    }

    for (i = 0U; i < WS2812_RESET_SLOT_COUNT; i++) {
        *buf++ = WS2812_CCR_RESET;
    }
}

bool Driver_WS2812_Init(void)
{
    uint8_t i;
    ws2812_rgb_t power_on_color = WS2812_POWER_ON_COLOR;

    s_ws2812_last_init_status = WS2812_INIT_STATUS_OK;

    for (i = 0U; i < WS2812_STRIP_MAX; i++) {
        ws2812_ctx_t *ctx = &s_ws2812_ctx[i];

        if ((ctx->led_count == 0U) || !bsp_pwm_supports_dma_waveform(ctx->pwm_ch)) {
            s_ws2812_last_init_status = WS2812_INIT_STATUS_INVALID_CONFIG;
            return false;
        }

        memset(ctx->waveform_buf, 0, (size_t)ctx->waveform_len * sizeof(uint16_t));
        Driver_WS2812_SetAllRGB((ws2812_strip_t)i, power_on_color.r, power_on_color.g, power_on_color.b);
    }

    bsp_delay_ms(WS2812_POWER_ON_SETTLE_MS);

    for (i = 0U; i < WS2812_STRIP_MAX; i++) {
        if (!Driver_WS2812_Refresh((ws2812_strip_t)i)) {
            s_ws2812_last_init_status = WS2812_INIT_STATUS_REFRESH_FAILED;
            return false;
        }

        if (!prv_ws2812_wait_all_idle(WS2812_WAIT_IDLE_TIMEOUT_US)) {
            s_ws2812_last_init_status = WS2812_INIT_STATUS_WAIT_IDLE_TIMEOUT;
            return false;
        }
    }

    return true;
}

ws2812_init_status_t Driver_WS2812_GetLastInitStatus(void)
{
    return s_ws2812_last_init_status;
}

const char *Driver_WS2812_InitStatusString(ws2812_init_status_t status)
{
    switch (status) {
    case WS2812_INIT_STATUS_OK:
        return "OK";
    case WS2812_INIT_STATUS_INVALID_CONFIG:
        return "INVALID_CONFIG";
    case WS2812_INIT_STATUS_REFRESH_FAILED:
        return "REFRESH_FAILED";
    case WS2812_INIT_STATUS_WAIT_IDLE_TIMEOUT:
        return "WAIT_IDLE_TIMEOUT";
    default:
        return "UNKNOWN";
    }
}

uint16_t Driver_WS2812_GetLedCount(ws2812_strip_t strip)
{
    ws2812_ctx_t *ctx = prv_ws2812_get_ctx(strip);

    if (ctx == NULL) {
        return 0U;
    }

    return ctx->led_count;
}

bool Driver_WS2812_IsBusy(ws2812_strip_t strip)
{
    ws2812_ctx_t *ctx = prv_ws2812_get_ctx(strip);

    if (ctx == NULL) {
        return false;
    }

    return bsp_pwm_is_dma_waveform_busy(ctx->pwm_ch);
}

bool Driver_WS2812_SetPixelRGB(ws2812_strip_t strip, uint16_t index, uint8_t r, uint8_t g, uint8_t b)
{
    ws2812_ctx_t *ctx = prv_ws2812_get_ctx(strip);

    if ((ctx == NULL) || (index >= ctx->led_count)) {
        return false;
    }

    ctx->pixel_buf[index].r = r;
    ctx->pixel_buf[index].g = g;
    ctx->pixel_buf[index].b = b;
    return true;
}

bool Driver_WS2812_SetPixel(ws2812_strip_t strip, uint16_t index, ws2812_rgb_t color)
{
    return Driver_WS2812_SetPixelRGB(strip, index, color.r, color.g, color.b);
}

void Driver_WS2812_SetAllRGB(ws2812_strip_t strip, uint8_t r, uint8_t g, uint8_t b)
{
    ws2812_ctx_t *ctx = prv_ws2812_get_ctx(strip);
    uint16_t i;

    if (ctx == NULL) {
        return;
    }

    for (i = 0U; i < ctx->led_count; i++) {
        ctx->pixel_buf[i].r = r;
        ctx->pixel_buf[i].g = g;
        ctx->pixel_buf[i].b = b;
    }
}

void Driver_WS2812_Clear(ws2812_strip_t strip)
{
    Driver_WS2812_SetAllRGB(strip, 0U, 0U, 0U);
}

void Driver_WS2812_ClearAll(void)
{
    Driver_WS2812_Clear(WS2812_STRIP_1);
    Driver_WS2812_Clear(WS2812_STRIP_2);
}

bool Driver_WS2812_Refresh(ws2812_strip_t strip)
{
    ws2812_ctx_t *ctx = prv_ws2812_get_ctx(strip);

    if (ctx == NULL) {
        return false;
    }

    prv_ws2812_poll_all();
    if (prv_ws2812_any_strip_busy()) {
        return false;
    }

    prv_ws2812_encode_strip(ctx);
    return bsp_pwm_start_dma_waveform(ctx->pwm_ch, ctx->waveform_buf, ctx->waveform_len);
}
