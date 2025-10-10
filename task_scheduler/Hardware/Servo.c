#include "stm32f10x.h"                  // Device header
#include "PWM.h"

/**
  * 函    数：舵机初始化
  * 参    数：无
  * 返 回 值：无
  */
void Servo_Init(void)
{
	PWM_Init();									//初始化舵机的底层PWM
}

/**
  * 函    数：偏航角舵机设置角度
  * 参    数：Angle 要设置的舵机角度，范围：0~180
  * 返 回 值：无
  */
void Servo_yaw_SetAngle(uint16_t Angle)
{
	PWM_SetCompare2(Angle);	//设置占空比
												//将角度线性变换，对应到舵机要求的占空比范围上
}

/**
  * 函    数：俯仰角舵机设置角度
  * 参    数：Angle 要设置的舵机角度，范围：0~180
  * 返 回 值：无
  */
void Servo_pitch_SetAngle(uint16_t Angle)
{
	PWM_SetCompare1(Angle);	//设置占空比
												//将角度线性变换，对应到舵机要求的占空比范围上
}
