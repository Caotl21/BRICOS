#include "Delay.h"
#include "stm32f4xx_rcc.h"
#include "stm32f4xx_tim.h"

static uint32_t Delay_GetTIM2Clock(void)
{
    RCC_ClocksTypeDef clocks;

    RCC_GetClocksFreq(&clocks);

    if ((RCC->CFGR & RCC_CFGR_PPRE1) == 0x00000000U)
    {
        return clocks.PCLK1_Frequency;
    }

    return clocks.PCLK1_Frequency * 2U;
}

void Delay_Init(void)
{
    TIM_TimeBaseInitTypeDef tim_init;
    uint32_t tim_clk;
    uint16_t prescaler;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    TIM_DeInit(TIM2);

    tim_clk = Delay_GetTIM2Clock();
    prescaler = (uint16_t)((tim_clk / 1000000U) - 1U);

    tim_init.TIM_Prescaler = prescaler;
    tim_init.TIM_CounterMode = TIM_CounterMode_Up;
    tim_init.TIM_Period = 0xFFFFFFFFU;
    tim_init.TIM_ClockDivision = TIM_CKD_DIV1;
    tim_init.TIM_RepetitionCounter = 0;

    TIM_TimeBaseInit(TIM2, &tim_init);
    TIM_SetCounter(TIM2, 0);
    TIM_Cmd(TIM2, ENABLE);
}

void Delay_us(uint32_t us)
{
    uint32_t start = TIM_GetCounter(TIM2);

    while ((uint32_t)(TIM_GetCounter(TIM2) - start) < us)
    {
    }
}

void Delay_ms(uint32_t ms)
{
    while (ms--)
    {
        Delay_us(1000U);
    }
}

void Delay_s(uint32_t s)
{
    while (s--)
    {
        Delay_ms(1000U);
    }
}
