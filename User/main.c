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
#include "Types.h"
#include "boot_flag.h"

int main()
{
	//NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
	//LED_Init();
	//USART1_DMA_Init();
	//USART3_DMA_Init();
	Serial_Init();
	USART2_Init(115200);
//	DHT11_Init();
//	MS5837_init();
//	PWM_Init();
//	ESC_Init();
//	USART2_DMA_Init();
//	JY901B_Init();
//	AD_Init();
//	IM948_Init();
//	Ctl_WatchDog_Timer_Init();
	
	BootFlag_MarkBootSuccess();
	// ��ʼ�����������
		
	// ��ʾ������Ϣ
    Delay_ms(DELAY_TIME);
    //OLED_Clear();	
	//Watchdog_Init();
	printf("This is APP1!\r\n");
	//TaskScheduler_Init();
	while(1)
	{
		printf("This is APP1!\r\n");
		//TaskScheduler_Run();
	}
}
