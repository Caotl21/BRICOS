#ifndef __TASK_LED_H
#define __TASK_LED_H

#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include "driver_ws2812.h"
#include "sys_mode_manager.h"

#define LED_TASK_PRIO      2
#define LED_STK_SIZE       384

typedef enum {
    LED_EFFECT_SOLID = 0,
    LED_EFFECT_BREATH,
    LED_EFFECT_CHASE,
    LED_EFFECT_STROBE
} led_effect_type_t;

typedef struct {
    uint8_t enabled;
    led_effect_type_t type;
    ws2812_rgb_t color;
    uint16_t period_ms;
} led_effect_t;

extern TaskHandle_t LED_Task_Handler;

void Task_LED_Init(void);
void Task_LED_SetMode(bot_sys_mode_e mode);
void Task_LED_SetBaseEffect(const led_effect_t *effect);
void Task_LED_SetWarningEffect(const led_effect_t *effect);
void Task_LED_ClearWarningEffect(void);

#endif /* __TASK_LED_H */
