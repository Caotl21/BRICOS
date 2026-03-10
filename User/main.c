#include "stm32f4xx.h"
#include "Delay.h"
#include "Timer.h"
#include "Serial.h"
#include "DHT11.h"
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
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

	DMA_User_Init();
	USART1_Init(115200);
	USART2_Init(115200);
	USART3_Init(115200);
	//UART4_Init(115200);

	DHT11_Init();
	MS5837_init();
	PWM_Init();
	ESC_Init();

	AD_Init();
	JY901B_Init();
	IM948_Init();
	Ctl_WatchDog_Timer_Init();
	
	BootFlag_MarkBootSuccess();

    Delay_ms(DELAY_TIME);

	Watchdog_Init();
	
	TaskScheduler_Init();
	while(1)
	{
		TaskScheduler_Run();
	}
}
