#include "stm32f10x.h"                  // Device header
#include "PWM.h"
#include "LED.h"
#include "Delay.h"
#include "OLED.h"

/**
  * 函    数：直流电机初始化
  * 参    数：无
  * 返 回 值：无
  */
void Motor_Init(void)
{
	/*开启时钟*/
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);		//开启GPIOA的时钟
	
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4 | GPIO_Pin_5;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);						//将PA4和PA5引脚初始化为推挽输出	
	
	PWM_Init();													//初始化直流电机的底层PWM
}

/**
  * 函    数：直流电机设置速度
  * 参    数：Speed 要设置的速度，范围：-100~100
  * 返 回 值：无
  */
void Motor_SetSpeed(int8_t Speed)
{
	uint16_t pulse_width;
	
	// 将速度(-100到100)映射到脉宽(1100到1900微秒)
	// 1100us = 反转最大, 1500us = 停止, 1900us = 正转最大
	pulse_width = 1500 + (Speed * 4);  // 1500±400us
	
	// 限制范围到ESC规格
	if (pulse_width < 1100) pulse_width = 1100;
	if (pulse_width > 1900) pulse_width = 1900;
	
	
	PWM_SetCompare3(pulse_width);
	//OLED_ShowSignedNum(3, 7, pulse_width, 3);	//OLED显示速度变量
}

/**
  * 函    数：ESC解锁序列
  * 参    数：无
  * 返 回 值：无
  */
void Motor_Unlock(void)
{
	OLED_ShowString(2, 1, "locking");
    
    // 发送中性信号1500us，持续2秒
    PWM_SetCompare3(1500);
    Delay_ms(2000);
    
    // 发送低油门信号确认解锁
    //PWM_SetCompare3(1100);
    //Delay_ms(500);
    
    // 回到中性位置
    PWM_SetCompare3(1500);
    Delay_ms(500);
    
    // LED2点亮表示解锁完成
    OLED_ShowString(2,1,"locked ");
}
