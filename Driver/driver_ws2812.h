#ifndef __WS2812_DRIVER_H
#define __WS2812_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

// 当前工程固定支持两路 WS2812 灯带。
#define WS2812_STRIP_COUNT         2U

// 每路灯带的灯珠数量。
// 若后续与硬件核对后数量不同，直接改这里即可。
#ifndef WS2812_STRIP_1_LED_COUNT
#define WS2812_STRIP_1_LED_COUNT   20U
#endif

#ifndef WS2812_STRIP_2_LED_COUNT
#define WS2812_STRIP_2_LED_COUNT   20U
#endif

// WS2812 协议参数：
// - 每颗灯 24bit，发送顺序为 GRB
// - 每个 bit 对应一个 PWM 周期
// - reset 低电平时间要求大于 50us，这里取 64 个 slot => 80us
#define WS2812_BITS_PER_LED        24U
#define WS2812_RESET_SLOT_COUNT    64U

// 在 ARR = 104（总计 105 count）下的 CCR 高电平宽度：
// - bit0 高电平约 0.35us => 29 tick
// - bit1 高电平约 0.70us => 58 tick
#define WS2812_CCR_BIT_0           29U
#define WS2812_CCR_BIT_1           58U

typedef enum {
    WS2812_STRIP_1 = 0,
    WS2812_STRIP_2,
    WS2812_STRIP_MAX
} ws2812_strip_t;

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} ws2812_rgb_t;

// 常用颜色宏，便于直接作为 ws2812_rgb_t 使用。
#define WS2812_COLOR_BLACK     ((ws2812_rgb_t){0U,   0U,   0U})
#define WS2812_COLOR_WHITE     ((ws2812_rgb_t){255U, 255U, 255U})
#define WS2812_COLOR_RED       ((ws2812_rgb_t){255U, 0U,   0U})
#define WS2812_COLOR_GREEN     ((ws2812_rgb_t){0U,   255U, 0U})
#define WS2812_COLOR_BLUE      ((ws2812_rgb_t){0U,   0U,   255U})
#define WS2812_COLOR_YELLOW    ((ws2812_rgb_t){255U, 255U, 0U})
#define WS2812_COLOR_CYAN      ((ws2812_rgb_t){0U,   255U, 255U})
#define WS2812_COLOR_MAGENTA   ((ws2812_rgb_t){255U, 0U,   255U})
#define WS2812_COLOR_ORANGE    ((ws2812_rgb_t){255U, 128U, 0U})
#define WS2812_COLOR_PURPLE    ((ws2812_rgb_t){128U, 0U,   255U})

// 上电默认颜色。
#define WS2812_POWER_ON_COLOR  WS2812_COLOR_PURPLE

/**
 * @brief  初始化 WS2812 驱动。
 * @return true 表示初始化成功，false 表示底层 PWM+DMA 能力未就绪。
 * @note   本函数会将两路灯带颜色缓存设置为默认颜色，并立即刷新一次。
 */
bool Driver_WS2812_Init(void);

/**
 * @brief  获取指定灯带的灯珠数量。
 * @param  strip - 灯带编号。
 * @return 灯珠数量；若 strip 无效则返回 0。
 */
uint16_t Driver_WS2812_GetLedCount(ws2812_strip_t strip);

/**
 * @brief  查询指定灯带当前是否正在发送波形。
 * @param  strip - 灯带编号。
 * @return true 表示发送中，false 表示空闲。
 */
bool Driver_WS2812_IsBusy(ws2812_strip_t strip);

/**
 * @brief  设置指定灯珠的 RGB 颜色缓存。
 * @param  strip - 灯带编号。
 * @param  index - 灯珠下标，从 0 开始。
 * @param  r - 红色分量。
 * @param  g - 绿色分量。
 * @param  b - 蓝色分量。
 * @return true 表示写入成功，false 表示参数越界。
 * @note   这里只改缓存，不会立即发波形；需后续显式调用 Refresh。
 */
bool Driver_WS2812_SetPixelRGB(ws2812_strip_t strip, uint16_t index, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief  设置指定灯珠的颜色缓存。
 * @param  strip - 灯带编号。
 * @param  index - 灯珠下标，从 0 开始。
 * @param  color - RGB 颜色结构体。
 * @return true 表示写入成功，false 表示参数越界。
 */
bool Driver_WS2812_SetPixel(ws2812_strip_t strip, uint16_t index, ws2812_rgb_t color);

/**
 * @brief  将整条灯带的颜色缓存设置为同一个 RGB 值。
 * @param  strip - 灯带编号。
 * @param  r - 红色分量。
 * @param  g - 绿色分量。
 * @param  b - 蓝色分量。
 */
void Driver_WS2812_SetAllRGB(ws2812_strip_t strip, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief  清空指定灯带的颜色缓存（全部置黑）。
 * @param  strip - 灯带编号。
 */
void Driver_WS2812_Clear(ws2812_strip_t strip);

/**
 * @brief  清空所有灯带的颜色缓存（全部置黑）。
 */
void Driver_WS2812_ClearAll(void);

/**
 * @brief  将指定灯带当前颜色缓存编码成 GRB 波形，并启动一次 DMA 发送。
 * @param  strip - 灯带编号。
 * @return true 表示成功启动发送，false 表示当前忙或参数非法。
 */
bool Driver_WS2812_Refresh(ws2812_strip_t strip);

#endif // __WS2812_DRIVER_H
