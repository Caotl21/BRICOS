#ifndef __BSP_TIMER_H
#define __BSP_TIMER_H

#include <stdint.h>
#include <stdbool.h>
#include "bsp_core.h"

typedef void (*bsp_timer_cb_t)(void);

// 3. 定时器配置结构体
typedef struct {
    bsp_timer_t timer;           // 定时器枚举
    uint32_t tick_us;            // 定时器时基 (微秒)，如 100 代表 100us 数一下
    uint32_t period_ticks;       // 周期计数值 (产生中断的阈值，填 0 则默认跑满最大值不中断)
    
    // 中断配置
    bool enable_nvic;            // 是否开启溢出中断
    uint8_t preemption_prio;     // 抢占优先级
    uint8_t sub_prio;            // 响应优先级
} bsp_timer_cfg_t;

// 仅硬件初始化：开时钟 + 配PSC/ARR + 启动
bool bsp_timer_init(const bsp_timer_cfg_t *cfg);

// 仅硬件读计数
uint32_t bsp_timer_get_ticks(const bsp_timer_cfg_t *cfg);

//清空计数值
void bsp_timer_reset_ticks(bsp_timer_t timer);

void bsp_timer_register_cb(bsp_timer_t timer, bsp_timer_cb_t cb);

/* 中断处理核心 (供 stm32f4xx_it.c 调用) */
void bsp_timer_isr_handler(bsp_timer_t timer);



#endif /* __BSP_TIMER_H */
