#include "stm32f10x.h"                  // Device header
#include "Delay.h"
#include "OLED.h"
#include "Motor.h"
#include "Key.h"
#include "LED.h"
#include "Serial.h"

uint8_t KeyNum;		//定义用于接收按键键码的变量
int8_t Speed;		//定义速度变量
uint8_t RxData;		//定义用于接收串口数据的变量

int main(void)
{
	/*模块初始化*/
	OLED_Init();		//OLED初始化
	Motor_Init();		//直流电机初始化
	Key_Init();			//按键初始化
	Serial_Init();		//串口初始化
	
	/*ESC解锁*/
	Delay_s(2);
	Motor_Unlock();		// 添加ESC解锁序列
	
	/*显示静态字符串*/
	OLED_ShowString(1, 1, "Speed:");		//1行1列显示字符串Speed:
	
	while (1)
	{
//		KeyNum = Key_GetNum();				//获取按键键码
//		if (KeyNum == 1)					//按键1按下
//		{
//			Speed += 20;					//速度变量自增20
//			if (Speed > 100)				//速度变量超过100后
//			{
//				Speed = 100;				//速度变量变为-100
//											//此操作会让电机旋转方向突然改变，可能会因供电不足而导致单片机复位
//											//若出现了此现象，则应避免使用这样的操作
//			}
//		}
		if (Serial_GetRxFlag() == 1)			//检查串口接收数据的标志位
		{
			RxData = Serial_GetRxData();		//获取串口接收的数据
			Serial_SendByte(RxData);			//串口将收到的数据回传回去，用于测试
			Speed = (int)RxData;
			Motor_SetSpeed(Speed);				//设置直流电机的速度为速度变量
			OLED_ShowSignedNum(1, 7, Speed, 3);	//显示串口接收的数据
		}
		
	}
}
