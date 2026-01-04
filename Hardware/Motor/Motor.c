#include "stm32f4xx.h"                  // Device header
#include "PWM.h"
#include "LED.h"
#include "Delay.h"
#include "OLED.h"

uint16_t motor_pulse[6] = {0};
uint16_t led_pulse[2] = {0};
uint16_t servo_pulse[2] = {0};
uint8_t motor_num = 6;
uint8_t led_num = 2;
uint8_t servo_num = 2;
/**
  * 函    数：直流电机设置速度
  * 参    数：Speed 要设置的速度
  * 返 回 值：无
  */
void Motor_SetSpeed(uint16_t pulse_width[], uint8_t num)
{
	
	// 将速度(-100到100)映射到脉宽(1100到1900微秒)
	// 1100us = 反转最大, 1500us = 停止, 1900us = 正转最大
	//pulse_width = 1500 + (Speed * 4);  // 1500±400us
	
	// 限制范围到ESC规格
	for(int i = 0; i < num; i++)
	{
		if (pulse_width[i] < 1100) pulse_width[i] = 1100;
		if (pulse_width[i] > 1900) pulse_width[i] = 1900;
	}

	Motor_SetPWM(pulse_width);

}

void LED_SetSpeed(uint16_t pulse_width[], uint8_t num)
{
	
	// 限制范围到ESC规格
	for(int i = 0; i < num; i++)
	{
		if (pulse_width[i] < 1100) pulse_width[i] = 1100;
		if (pulse_width[i] > 1900) pulse_width[i] = 1900;
	}

	LED_SetPWM(pulse_width);

}

void Servo_SetSpeed(uint16_t pulse_width[], uint8_t num)
{
	
	// 限制范围到ESC规格
	for(int i = 0; i < num; i++)
	{
		if (pulse_width[i] < 1100) pulse_width[i] = 1100;
		if (pulse_width[i] > 1900) pulse_width[i] = 1900;
	}

	Servo_SetPWM(pulse_width);

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
	for(int i = 0; i < motor_num; i++)
	{motor_pulse[i] = 1500;}
    Motor_SetSpeed(motor_pulse, motor_num);
    Delay_ms(2000);
    
    // 发送低油门信号确认解锁
    //PWM_SetCompare3(1100);
    //Delay_ms(500);
    
    // 回到中性位置
//    PWM_SetCompare3(1500);
//    Delay_ms(500);
    
    // LED2点亮表示解锁完成
    OLED_ShowString(2,1,"locked ");
}
