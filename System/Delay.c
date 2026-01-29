#include "stm32f4xx.h"

/**
  * @brief  微秒级延时
  * @param  xus 延时时长，范围：0~233015
  * @retval 无
  */

void Delay_us(unsigned int n)
{
	SysTick->CTRL = 0; // Disable SysTick
	SysTick->LOAD = n * 21 - 1; // Count from 255 to 0 (256 cycles)，0算一次，所以要减1
	SysTick->VAL = 0; // Clear current value as well as count fla
	
	// 0位控制使能，2位控制选择FCLK（1）还是STCLK（1），这里选择stclk
	SysTick->CTRL = 1; // Enable SysTick timer with processor clock
	//SysTick->CTRL  = SysTick_CTRL_CLKSOURCE_Msk|SysTick_CTRL_ENABLE_Msk;
	
	while ((SysTick->CTRL & 0x00010000)==0);// Wait until count flag is set，控制寄存器的第16位用于检测是否计数结束
	SysTick->CTRL = 0; // Disable SysTick
}

/**
  * @brief  毫秒级延时
  * @param  xms 延时时长，范围：0~4294967295
  * @retval 无
  */
void Delay_ms(uint32_t xms)
{
	while(xms--)
	{
		Delay_us(1000);
	}
}
 
/**
  * @brief  秒级延时
  * @param  xs 延时时长，范围：0~4294967295
  * @retval 无
  */
void Delay_s(uint32_t xs)
{
	while(xs--)
	{
		Delay_ms(1000);
	}
} 
