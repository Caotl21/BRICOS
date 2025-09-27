#include "stm32f10x.h"                  // Device header
#include "Delay.h"
#include "OLED.h"
#include "Serial.h"
#include "Servo.h"

uint8_t RxData;			//定义用于接收串口数据的变量
float Angle;


int main(void)
{
	/*模块初始化*/
	OLED_Init();		//OLED初始化
	//PWM_Init();
	Servo_Init();
	//LED_Init();
	
	/*显示静态字符串*/
	OLED_ShowString(1, 1, "Rec_Angle:");
	
	/*串口初始化*/
	Serial_Init();		//串口初始化
	
	while (1)
	{
		
		if (Serial_GetRxFlag() == 1)			//检查串口接收数据的标志位
		{
			RxData = Serial_GetRxData();		//获取串口接收的数据
			OLED_ShowHexNum(2, 1, RxData, 3);
			Angle = (float)(RxData);
			Servo_SetAngle(Angle);
			Serial_SendByte(RxData);			//串口将收到的数据回传回去，用于测试
			OLED_ShowNum(1, 11, Angle, 3);	//显示串口接收的数据
		}
	}
}
