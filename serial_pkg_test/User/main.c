#include "stm32f10x.h"                  // Device header
#include "Delay.h"
#include "OLED.h"
#include "Serial.h"
#include "Key.h"
#include "PWM.h"
#include "LED.h"

uint8_t KeyNum;			//定义用于接收按键键码的变量

int main(void)
{
	/*模块初始化*/
	OLED_Init();		//OLED初始化
	Key_Init();			//按键初始化
	Serial_Init();		//串口初始化
	PWM_Init();
	
	/*显示静态字符串*/
	OLED_ShowString(1, 1, "PWM Values:");
	//OLED_ShowString(3, 1, "Press Key2->Send");
	
	while (1)
	{
		KeyNum = Key_GetNum();
		//if (KeyNum == 2) // 按键2按下，发送传感器数据
		// 传感器数据接收

		if(1)
		{
			// 模拟传感器数据变化
			Serial_SensorData.accel_x += 10;
			Serial_SensorData.accel_y += 20;
			Serial_SensorData.depth += 5;
			Serial_SensorData.temperature += 1;
			
			Serial_SendSensorPacket(); // 发送传感器数据包
			Delay_ms(50);
		}
		
		if (Serial_GetRxFlag() == 1) // 接收到PWM数据包
		{
			// 显示6个PWM值
			OLED_ShowNum(2, 1, Serial_RxPWM[0], 4);
			OLED_ShowNum(2, 6, Serial_RxPWM[1], 4);
			OLED_ShowNum(3, 1, Serial_RxPWM[2], 4);
			OLED_ShowNum(3, 6, Serial_RxPWM[3], 4);
			OLED_ShowNum(4, 1, Serial_RxPWM[4], 4);
			OLED_ShowNum(4, 6, Serial_RxPWM[5], 4);
		}
	}
}
