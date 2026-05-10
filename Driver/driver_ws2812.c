#include "driver_ws2812.h"

#include "bsp_delay.h"
#include "bsp_pwm.h"

#include <string.h>

// 上电后给灯带和电源留一点稳定时间，再发送默认颜色首帧。
// 这样可以避免刚上电时首帧过早，被灯带漏收或误收。
#define WS2812_POWER_ON_SETTLE_MS   2U

// 单路 WS2812 灯带的 Driver 运行时上下文：
// - pwm_ch       对应的底层 PWM 输出通道
// - led_count    该路灯带的灯珠数量
// - pixel_buf    上层可读写的 RGB 颜色缓存
// - waveform_buf 刷新时编码后的 CCR 波形缓冲区
// - waveform_len 本路波形缓冲区总长度
typedef struct {
    bsp_pwm_ch_t pwm_ch;
    uint16_t led_count;
    ws2812_rgb_t *pixel_buf;
    uint16_t *waveform_buf;
    uint16_t waveform_len;
} ws2812_ctx_t;

// 两路灯带各自的颜色缓存。
static ws2812_rgb_t s_strip_1_pixels[WS2812_STRIP_1_LED_COUNT];
static ws2812_rgb_t s_strip_2_pixels[WS2812_STRIP_2_LED_COUNT];

// 两路灯带各自的波形缓冲区：
// 长度 = 灯珠数 * 24bit + reset 尾码 slot 数。
static uint16_t s_strip_1_waveform[WS2812_STRIP_1_LED_COUNT * WS2812_BITS_PER_LED + WS2812_RESET_SLOT_COUNT];
static uint16_t s_strip_2_waveform[WS2812_STRIP_2_LED_COUNT * WS2812_BITS_PER_LED + WS2812_RESET_SLOT_COUNT];

// WS2812 Driver 硬件/缓存字典。
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

// 判断灯带编号是否合法。
static bool prv_ws2812_strip_is_valid(ws2812_strip_t strip)
{
    return strip < WS2812_STRIP_MAX;
}

// 获取指定灯带对应的 Driver 上下文。
static ws2812_ctx_t *prv_ws2812_get_ctx(ws2812_strip_t strip)
{
    if (!prv_ws2812_strip_is_valid(strip)) {
        return NULL;
    }

    return &s_ws2812_ctx[strip];
}

// 当前这两路 WS2812 都挂在 TIM1 上。
// 现阶段 BSP 的 waveform 发送接口是“按通道启动”，但启动时会重置整个定时器计数器，
// 因此不能让两路灯带同时发送，否则后启动的一路会打断前一路，导致乱色。
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

// 仅在初始化阶段使用，等待当前所有灯带发送结束。
static void prv_ws2812_wait_all_idle(void)
{
    while (prv_ws2812_any_strip_busy()) {
        ;
    }
}

// 按 WS2812 要求使用 MSB first，把一个字节编码成 8 个 CCR 点位。
static uint16_t *prv_ws2812_encode_byte(uint16_t *buf, uint8_t value)
{
    uint8_t mask;

    for (mask = 0x80U; mask != 0U; mask >>= 1U) {
        *buf++ = ((value & mask) != 0U) ? WS2812_CCR_BIT_1 : WS2812_CCR_BIT_0;
    }

    return buf;
}

// 把整条灯带的 RGB 缓存编码成一帧 GRB 波形，末尾补 reset 低电平。
// 编码顺序必须是 G -> R -> B，且每个字节都按 MSB first 发出。
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

    // 帧尾补足一段低电平，满足 WS2812 的 reset / latch 时间要求。
    for (i = 0U; i < WS2812_RESET_SLOT_COUNT; i++) {
        *buf++ = 0U;
    }
}

// 初始化 WS2812 Driver：
// - 检查底层 PWM 通道是否具备 DMA 波形发送能力
// - 把颜色缓存初始化为默认上电颜色
// - 清空波形缓冲区
// - 按顺序向两路灯带各发送一帧默认颜色
bool Driver_WS2812_Init(void)
{
    uint8_t i;
    ws2812_rgb_t power_on_color = WS2812_POWER_ON_COLOR;

    for (i = 0U; i < WS2812_STRIP_MAX; i++) {
        ws2812_ctx_t *ctx = &s_ws2812_ctx[i];

        if ((ctx->led_count == 0U) || !bsp_pwm_supports_dma_waveform(ctx->pwm_ch)) {
            return false;
        }

        memset(ctx->waveform_buf, 0, (size_t)ctx->waveform_len * sizeof(uint16_t));
        Driver_WS2812_SetAllRGB((ws2812_strip_t)i, power_on_color.r, power_on_color.g, power_on_color.b);
    }

    // PWM 初始化后数据线已保持低电平，这里额外等待一小段时间，
    // 让 WS2812 完成上电稳定并满足 reset/latch 的低电平窗口。
    bsp_delay_ms(WS2812_POWER_ON_SETTLE_MS);

    // 依次刷新两路灯带，避免共用 TIM1 时互相打断。
    for (i = 0U; i < WS2812_STRIP_MAX; i++) {
        if (!Driver_WS2812_Refresh((ws2812_strip_t)i)) {
            return false;
        }

        prv_ws2812_wait_all_idle();
    }

    return true;
}

// 返回指定灯带的灯珠数量。
uint16_t Driver_WS2812_GetLedCount(ws2812_strip_t strip)
{
    ws2812_ctx_t *ctx = prv_ws2812_get_ctx(strip);

    if (ctx == NULL) {
        return 0U;
    }

    return ctx->led_count;
}

// 查询指定灯带当前是否仍在发送上一帧波形。
bool Driver_WS2812_IsBusy(ws2812_strip_t strip)
{
    ws2812_ctx_t *ctx = prv_ws2812_get_ctx(strip);

    if (ctx == NULL) {
        return false;
    }

    return bsp_pwm_is_dma_waveform_busy(ctx->pwm_ch);
}

// 设置单颗灯珠的 RGB 缓存，不立即发送。
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

// 结构体形式设置单颗灯珠颜色。
bool Driver_WS2812_SetPixel(ws2812_strip_t strip, uint16_t index, ws2812_rgb_t color)
{
    return Driver_WS2812_SetPixelRGB(strip, index, color.r, color.g, color.b);
}

// 将整条灯带颜色缓存统一设置为同一个 RGB 值。
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

// 清空指定灯带颜色缓存。
void Driver_WS2812_Clear(ws2812_strip_t strip)
{
    Driver_WS2812_SetAllRGB(strip, 0U, 0U, 0U);
}

// 清空全部灯带颜色缓存。
void Driver_WS2812_ClearAll(void)
{
    Driver_WS2812_Clear(WS2812_STRIP_1);
    Driver_WS2812_Clear(WS2812_STRIP_2);
}

// 刷新指定灯带：
// - 现阶段同一时刻只允许一条 WS2812 灯带发送
// - 若任意一路仍在发送，则直接返回 false
// - 先把 RGB 缓存编码成 GRB 波形
// - 再调用 BSP 发起一次 DMA 波形发送
bool Driver_WS2812_Refresh(ws2812_strip_t strip)
{
    ws2812_ctx_t *ctx = prv_ws2812_get_ctx(strip);

    if (ctx == NULL) {
        return false;
    }

    if (prv_ws2812_any_strip_busy()) {
        return false;
    }

    prv_ws2812_encode_strip(ctx);
    return bsp_pwm_start_dma_waveform(ctx->pwm_ch, ctx->waveform_buf, ctx->waveform_len);
}
