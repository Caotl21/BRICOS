#include "stm32f10x.h"
#include "Delay.h"
#include "OLED.h"
#include "im948_CMD.h"
#include "Serial.h"
#include "PWM.h"
#include "LED.h"
#include "servo.h"
#include "JY901B_UART.h"
#include "wit_c_sdk.h"
#include "DHT11.h"
#include "Motor.h"
#include "TaskScheduler.h"
#include "Tasks.h"
#include "Watchdog.h"


/********************JY901B**************************/
//#define ACC_UPDATE		0x01
//#define GYRO_UPDATE		0x02
//#define ANGLE_UPDATE	0x04
//#define MAG_UPDATE		0x08
//#define READ_UPDATE		0x80

//static volatile char s_cDataUpdate = 0, s_cCmd = 0xff;
//const uint32_t c_uiBaud[10] = {0, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};
//static void CmdProcess(void);
//static void AutoScanSensor(void);
//static void SensorUartSend(uint8_t *p_data, uint32_t uiSize);
//static void SensorDataUpdata(uint32_t uiReg, uint32_t uiRegNum);
//static void Delayms(uint16_t ucMs);
/**********************DHT11更新********************/
//u8 temperature;
//u8 humidity;
/***************************************************/

int main(void)
{
	/*模块初始化*/
	
	OLED_Init();		//OLED初始化
	//Key_Init();			//按键初始化
	Serial_Init();		//串口初始化
	LED_Init();			//LED初始化
	//DHT11_Init();		//DHT11初始化
	//Servo_Init();		//舵机初始化
	//Motor_Init();		//直流电机初始化
	//Motor_Unlock();		//电调初始化
	IM948_Init();

	/******************JY901B****************/
//	JY901B_UartInit(9600);			//初始化获取模块数据串口
//	WitInit(WIT_PROTOCOL_NORMAL, 0x50);	//初始化标准协议，此处为normal协议，无效
//	WitSerialWriteRegister(SensorUartSend);//注册写回调函数
//	WitRegisterCallBack(SensorDataUpdata);//注册获取传感器数据回调函数
//	WitDelayMsRegister(Delayms);
//	AutoScanSensor();
	/********************end*******************/
	
	// 初始化任务调度器
	TaskScheduler_Init();

	// 显示启动信息
    OLED_ShowString(1, 1, "System Ready");
    Delay_ms(1000);
    OLED_Clear();
	Watchdog_Init();

	while (1)
	{
		TaskScheduler_Run();
	}	
}


/**********************JY901B********************************/
//void CopeCmdData(unsigned char ucData)
//{
//	 static unsigned char s_ucData[50], s_ucRxCnt = 0;
//	
//	 s_ucData[s_ucRxCnt++] = ucData;
//	 if(s_ucRxCnt<3)return;										//Less than three data returned
//	 if(s_ucRxCnt >= 50) s_ucRxCnt = 0;
//	 if(s_ucRxCnt >= 3)
//	 {
//		 if((s_ucData[1] == '\r') && (s_ucData[2] == '\n'))
//		 {
//		  	s_cCmd = s_ucData[0];
//			  memset(s_ucData,0,50);//
//			  s_ucRxCnt = 0;
//	   } 
//		 else 
//		 {
//			 s_ucData[0] = s_ucData[1];
//			 s_ucData[1] = s_ucData[2];
//			 s_ucRxCnt = 2;
//			}
//	  }
//}

//static void ShowHelp(void)
//{
//	printf("\r\n************************	 WIT_SDK_DEMO	************************");
//	printf("\r\n************************          HELP           ************************\r\n");
//	printf("UART SEND:a\\r\\n   Acceleration calibration.\r\n");
//	printf("UART SEND:m\\r\\n   Magnetic field calibration,After calibration send:   e\\r\\n   to indicate the end\r\n");
//	printf("UART SEND:U\\r\\n   Bandwidth increase.\r\n");
//	printf("UART SEND:u\\r\\n   Bandwidth reduction.\r\n");
//	printf("UART SEND:B\\r\\n   Baud rate increased to 115200.\r\n");
//	printf("UART SEND:b\\r\\n   Baud rate reduction to 9600.\r\n");
//	printf("UART SEND:R\\r\\n   The return rate increases to 10Hz.\r\n");
//	printf("UART SEND:r\\r\\n   The return rate reduction to 1Hz.\r\n");
//	printf("UART SEND:C\\r\\n   Basic return content: acceleration, angular velocity, angle, magnetic field.\r\n");
//	printf("UART SEND:c\\r\\n   Return content: acceleration.\r\n");
//	printf("UART SEND:h\\r\\n   help.\r\n");
//	printf("******************************************************************************\r\n");
//}

//static void CmdProcess(void)
//{
//	switch(s_cCmd)
//	{
//		case 'a':	
//			if(WitStartAccCali() != WIT_HAL_OK) 
//				printf("\r\nSet AccCali Error\r\n");
//			else
//				printf("\r\nSet AccCali OK\r\n");
//			break;
//		case 'm':	
//			if(WitStartMagCali() != WIT_HAL_OK) 
//				printf("\r\nSet MagCali Error\r\n");
//			break;
//		case 'e':	
//			if(WitStopMagCali() != WIT_HAL_OK)
//				printf("\r\nSet MagCali Error\r\n");
//			break;
//		case 'u':	
//			if(WitSetBandwidth(BANDWIDTH_5HZ) != WIT_HAL_OK) 
//				printf("\r\nSet Bandwidth Error\r\n");
//			break;
//		case 'U':	
//			if(WitSetBandwidth(BANDWIDTH_256HZ) != WIT_HAL_OK) 
//				printf("\r\nSet Bandwidth Error\r\n");
//			break;
//		case 'B':	
//			if(WitSetUartBaud(WIT_BAUD_115200) != WIT_HAL_OK) 
//				printf("\r\nSet Baud Error\r\n");
//			else 
//				JY901B_UartInit(c_uiBaud[WIT_BAUD_115200]);											
//			break;
//		case 'b':	
//			if(WitSetUartBaud(WIT_BAUD_9600) != WIT_HAL_OK)
//				printf("\r\nSet Baud Error\r\n");
//			else 
//				JY901B_UartInit(c_uiBaud[WIT_BAUD_9600]);												
//			break;
//		case 'R':	
//			if(WitSetOutputRate(RRATE_10HZ) != WIT_HAL_OK) 
//				printf("\r\nSet Rate Error\r\n");
//			break;
//		case 'r':	
//			if(WitSetOutputRate(RRATE_1HZ) != WIT_HAL_OK) 
//				printf("\r\nSet Rate Error\r\n");
//			break;
//		case 'C':	
//			if(WitSetContent(RSW_ACC|RSW_GYRO|RSW_ANGLE|RSW_MAG) != WIT_HAL_OK) 
//				printf("\r\nSet RSW Error\r\n");
//			break;
//		case 'c':	
//			if(WitSetContent(RSW_ACC) != WIT_HAL_OK) 
//				printf("\r\nSet RSW Error\r\n");
//			break;
//		case 'h':
//			ShowHelp();
//			break;
//	}
//	s_cCmd = 0xff;
//}

//static void SensorUartSend(uint8_t *p_data, uint32_t uiSize)
//{
//	JY901B_UartSend(p_data, uiSize);
//}

//static void Delayms(uint16_t ucMs)
//{
//	Delay_ms(ucMs);
//}

//static void SensorDataUpdata(uint32_t uiReg, uint32_t uiRegNum)
//{
//	int i;
//    for(i = 0; i < uiRegNum; i++)
//    {
//        switch(uiReg)
//        {
////            case AX:
////            case AY:
//            case AZ:
//				s_cDataUpdate |= ACC_UPDATE;
//            break;
////            case GX:
////            case GY:
//            case GZ:
//				s_cDataUpdate |= GYRO_UPDATE;
//            break;
////            case HX:
////            case HY:
//            case HZ:
//				s_cDataUpdate |= MAG_UPDATE;
//            break;
////            case Roll:
////            case Pitch:
//            case Yaw:
//				s_cDataUpdate |= ANGLE_UPDATE;
//            break;
//            default:
//				s_cDataUpdate |= READ_UPDATE;
//			break;
//        }
//		uiReg++;
//    }
//}



//static void AutoScanSensor(void)
//{
//	int i, iRetry;
//	for(i = 1; i < 10; i++)
//	{
//		JY901B_UartInit(c_uiBaud[i]);
//		iRetry = 2;
//		do
//		{
//			s_cDataUpdate = 0;
//			
//			WitReadReg(AX, 3);
//			
//			Delay_ms(100);
//			if(s_cDataUpdate != 0)
//			{
//				printf("%d baud find sensor\r\n\r\n", c_uiBaud[i]);
//				ShowHelp();
//				return ;
//			}
//			iRetry--;
//		}while(iRetry);		
//	}
//	printf("can not find sensor\r\n");
//	printf("please check your connection\r\n");
//}


