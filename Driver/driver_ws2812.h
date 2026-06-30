#ifndef __WS2812_DRIVER_H
#define __WS2812_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

#define WS2812_STRIP_COUNT         2U

#ifndef WS2812_STRIP_1_LED_COUNT
#define WS2812_STRIP_1_LED_COUNT   10U
#endif

#ifndef WS2812_STRIP_2_LED_COUNT
#define WS2812_STRIP_2_LED_COUNT   10U
#endif

#define WS2812_BITS_PER_LED        24U
#define WS2812_RESET_SLOT_COUNT    64U
#define WS2812_CCR_BIT_0           29U
#define WS2812_CCR_BIT_1           58U
#define WS2812_CCR_RESET           1U

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

#define WS2812_POWER_ON_COLOR  WS2812_COLOR_WHITE

typedef enum {
    WS2812_INIT_STATUS_OK = 0,
    WS2812_INIT_STATUS_INVALID_CONFIG,
    WS2812_INIT_STATUS_REFRESH_FAILED,
    WS2812_INIT_STATUS_WAIT_IDLE_TIMEOUT
} ws2812_init_status_t;

bool Driver_WS2812_Init(void);
ws2812_init_status_t Driver_WS2812_GetLastInitStatus(void);
const char *Driver_WS2812_InitStatusString(ws2812_init_status_t status);

uint16_t Driver_WS2812_GetLedCount(ws2812_strip_t strip);
bool Driver_WS2812_IsBusy(ws2812_strip_t strip);

bool Driver_WS2812_SetPixelRGB(ws2812_strip_t strip, uint16_t index, uint8_t r, uint8_t g, uint8_t b);
bool Driver_WS2812_SetPixel(ws2812_strip_t strip, uint16_t index, ws2812_rgb_t color);
void Driver_WS2812_SetAllRGB(ws2812_strip_t strip, uint8_t r, uint8_t g, uint8_t b);
void Driver_WS2812_SetAllStripsRGB(uint8_t r, uint8_t g, uint8_t b);
void Driver_WS2812_Clear(ws2812_strip_t strip);
void Driver_WS2812_ClearAll(void);
bool Driver_WS2812_Refresh(ws2812_strip_t strip);
bool Driver_WS2812_RefreshBlocking(ws2812_strip_t strip, uint32_t timeout_us);
bool Driver_WS2812_ApplySolidColorAll(ws2812_rgb_t color, uint32_t timeout_us);

#endif
