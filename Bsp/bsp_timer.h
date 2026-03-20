#ifndef __BSP_TIMER_H
#define __BSP_TIMER_H

#include <stdint.h>
#include <stdbool.h>
#include "bsp_core.h"

typedef struct {
    bsp_timer_t timer;      // 选择硬件定时器
    uint32_t tick_us;       // 计数步进，单位us；0表示用默认值
} bsp_timer_cfg_t;

// 仅硬件初始化：开时钟 + 配PSC/ARR + 启动
bool bsp_timer_init(const bsp_timer_cfg_t *cfg);

// 仅硬件读计数
uint32_t bsp_timer_get_ticks(void);

#endif /* __BSP_TIMER_H */
