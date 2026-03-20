#include "bsp_timer.h"
#include "stm32f4xx.h"
#include "stm32f4xx_tim.h"

/*   --- 只读硬件结构体字典 ---   */
typedef struct {
    TIM_TypeDef *tim;
    uint32_t tim_rcc;
    void (*tim_rcc_cmd)(uint32_t, FunctionalState);
    uint32_t tim_clk_hz;    // 该定时器输入时钟
    uint8_t is_32bit;
} timer_hw_t;

// 硬件字典
static const timer_hw_t timer_hw_info[BSP_TIM_MAX] = {
    [BSP_TIM_SYSCOUNT] = {
        .tim = TIM2,
        .tim_rcc = RCC_APB1Periph_TIM2,
        .tim_rcc_cmd = RCC_APB1PeriphClockCmd,
        .tim_clk_hz = 84000000UL, // APB1 定时器时钟
        .is_32bit = 1
    }
};

static uint16_t prv_calc_prescaler(uint32_t tim_clk_hz, uint32_t tick_us)
{
    uint64_t psc64;

    if (tick_us == 0U) {
        tick_us = 50U;
    }

    // PSC = tim_clk_hz * tick_us / 1e6 - 1
    psc64 = ((uint64_t)tim_clk_hz * (uint64_t)tick_us) / 1000000ULL;
    if (psc64 == 0ULL) {
        psc64 = 1ULL;
    }
    psc64 -= 1ULL;

    if (psc64 > 0xFFFFULL) {
        psc64 = 0xFFFFULL;
    }

    return (uint16_t)psc64;
}

bool bsp_timer_init(const bsp_timer_cfg_t *cfg) 
{
    if (cfg == NULL || cfg->timer >= BSP_TIM_MAX) return false;

    const timer_hw_t *hw = &timer_hw_info[cfg->timer];

    // 1. 使能定时器时钟
    hw->tim_rcc_cmd(hw->tim_rcc, ENABLE);

    // 2. 配置定时器参数
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
    
    // 计算预分频器和重装载值
    uint32_t tick_us = (cfg->tick_us > 0) ? cfg->tick_us : 50; // 默认 50us
    uint32_t prescaler = prv_calc_prescaler(hw->tim_clk_hz, tick_us);
    uint32_t period = hw->is_32bit ? 0xFFFFFFFF : 0xFFFF; // 最大周期

    TIM_TimeBaseInitStructure.TIM_Prescaler = prescaler;
    TIM_TimeBaseInitStructure.TIM_Period = period;
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0U;

    TIM_TimeBaseInit(hw->tim, &TIM_TimeBaseInitStructure);
    TIM_SetCounter(hw->tim, 0); // 从0开始计数

    // 3. 启动定时器
    TIM_Cmd(hw->tim, ENABLE);

    return true;
}

uint32_t bsp_timer_get_ticks(const bsp_timer_cfg_t *cfg)
{
    const timer_hw_t *hw = &timer_hw_info[cfg->timer];
    return (uint32_t)(hw->tim->CNT);
}
