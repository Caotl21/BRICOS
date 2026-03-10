#include "stm32f4xx.h"                  // Device header
#include "Delay.h"
#include "Motor.h"

/**
  * 函    数：ESC解锁序列
  * 参    数：无
  * 返 回 值：无
  */
void ESC_Init(void)
{
	TIM3->CCR1 = PWM_DEFAULT;  // Thruster 1
	TIM3->CCR2 = PWM_DEFAULT;  // Thruster 2
	TIM3->CCR3 = PWM_DEFAULT;  // Thruster 3
	TIM3->CCR4 = PWM_DEFAULT;  // Thruster 4
	TIM4->CCR1 = PWM_DEFAULT;  // Thruster 5
	TIM4->CCR2 = PWM_DEFAULT;  // Thruster 6

    Delay_ms(2000);

}
