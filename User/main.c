#include "stm32f4xx.h"
#include "Delay.h"
#include "Timer.h"
#include "LED.h"
#include "Serial.h"
#include "DHT11.h"
#include "OLED.h"
#include "MS5837_I2C.h"
#include "Motor.h"
#include "PWM.h"
#include "Watchdog.h"
#include "JY901B.h"
#include "TaskScheduler.h"
#include "AD.h"
#include "im948_CMD.h"
#include "Dma.h"


int main()
{
	//NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
	/*模块初始化*/
	LED_Init();
	USART1_DMA_Init();
	Serial_Init();
	DHT11_Init();
	//OLED_Init();
	MS5837_init();
	PWM_Init();
	JY901B_Init();
	AD_Init();
	IM948_Init();
	//OLED_ShowString(1, 1, "Ready");
	// 初始化任务调度器
	TaskScheduler_Init();	
	// 显示启动信息
    //OLED_ShowString(1, 1, "System Ready");
    Delay_ms(1000);
    //OLED_Clear();	
	Watchdog_Init();
	printf("hello world!");
	while(1)
	{
		TaskScheduler_Run();
	}
}
