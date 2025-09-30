#include "stm32f10x.h"                  // Device header
#include "Delay.h"
#include "OLED.h"
#include "Serial.h"
#include "Key.h"
#include "PWM.h"
#include "LED.h"
#include "servo.h"
/*********JY901B**********/
#include "JY901B_UART.h"
#include "wit_c_sdk.h"
/********DHT11************/
#include "DHT11.h"

uint8_t KeyNum;			//定义用于接收按键键码的变量

/********************JY901B**************************/
#define ACC_UPDATE		0x01
#define GYRO_UPDATE		0x02
#define ANGLE_UPDATE	0x04
#define MAG_UPDATE		0x08
#define READ_UPDATE		0x80

static volatile char s_cDataUpdate = 0, s_cCmd = 0xff;
const uint32_t c_uiBaud[10] = {0, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};
static void CmdProcess(void);
static void AutoScanSensor(void);
static void SensorUartSend(uint8_t *p_data, uint32_t uiSize);
static void SensorDataUpdata(uint32_t uiReg, uint32_t uiRegNum);
static void Delayms(uint16_t ucMs);
/*********DHT11更新***********/
u8 temperature;
u8 humidity;

int main(void)
{
	/*模块初始化*/
	OLED_Init();		//OLED初始化
	Key_Init();			//按键初始化
	Serial_Init();		//串口初始化
	//PWM_Init();
	DHT11_Init();		//DHT11初始化
	Servo_Init();		//舵机初始化
	/******************JY901B****************/
	JY901B_UartInit(9600);			//初始化获取模块数据串口
	WitInit(WIT_PROTOCOL_NORMAL, 0x50);	//初始化标准协议，此处为normal协议，无效
	WitSerialWriteRegister(SensorUartSend);//注册写回调函数
	WitRegisterCallBack(SensorDataUpdata);//注册获取传感器数据回调函数
	WitDelayMsRegister(Delayms);
	AutoScanSensor();
	/********************end*******************/
	/********************变量定义*************************/
	float fAcc[3], fGyro[3], fAngle[3];
	int i;
	/*显示静态字符串*/
	//OLED_ShowString(1, 1, "PWM Values:");
	//OLED_ShowString(3, 1, "Press Key2->Send");
	while (1)
	{
		KeyNum = Key_GetNum();
		//if (KeyNum == 2) // 按键2按下，发送传感器数据
		DHT11_Read_Data(&temperature, &humidity);
/************************************更新J901B数据********************************************/
		if(s_cDataUpdate)			//模块自动上传数据，进行更新，通过SensorDataUpdate进行
		{
			   for(i = 0; i < 3; i++)
			   {
			    	fAcc[i] = sReg[AX+i] / 32768.0f * 16.0f;
				    fGyro[i] = sReg[GX+i] / 32768.0f * 2000.0f;
					fAngle[i] = sReg[Roll+i] / 32768.0f * 180.0f;
			   }
			   if(s_cDataUpdate & ACC_UPDATE)
			   {
				    //printf("acc:%.3f %.3f %.3f\r\n", fAcc[0], fAcc[1], fAcc[2]);
				    s_cDataUpdate &= ~ACC_UPDATE;
			   }
			   if(s_cDataUpdate & GYRO_UPDATE)
			   {
					//printf("gyro:%.3f %.3f %.3f\r\n", fGyro[0], fGyro[1], fGyro[2]);
					s_cDataUpdate &= ~GYRO_UPDATE;
			   }
			   if(s_cDataUpdate & ANGLE_UPDATE)
			   {
					//printf("angle:%.3f %.3f %.3f\r\n", fAngle[0], fAngle[1], fAngle[2]);
					s_cDataUpdate &= ~ANGLE_UPDATE;
			   }
			   if(s_cDataUpdate & MAG_UPDATE)
			   {
					//printf("mag:%d %d %d\r\n", sReg[HX], sReg[HY], sReg[HZ]);
					s_cDataUpdate &= ~MAG_UPDATE;
			   }
		}
/******************************************end*************************************/
		if(1)
		{
			// 模拟传感器数据变化
//			Serial_SensorData.accel_x += 10;
//			Serial_SensorData.accel_y += 20;
//			Serial_SensorData.depth += 5;
//			Serial_SensorData.temperature += 1;
			Serial_SensorData.accel_x = (int16_t)(fAcc[0]*100);
			Serial_SensorData.accel_y = (int16_t)(fAcc[1]*100);
			Serial_SensorData.accel_z = (int16_t)(fAcc[2]*100);
			Serial_SensorData.gyro_x = (int16_t)(fGyro[0]*100);
			Serial_SensorData.gyro_y = (int16_t)(fGyro[0]*100);
			Serial_SensorData.gyro_z = (int16_t)(fGyro[0]*100);
			Serial_SensorData.mag_x = (int16_t)(sReg[HX]*100);
			Serial_SensorData.mag_y = (int16_t)(sReg[HY]*100);
			Serial_SensorData.mag_z = (int16_t)(sReg[HZ]*100);
			Serial_SensorData.angle_x = (int16_t)(fAngle[0]*100);
			Serial_SensorData.angle_y = (int16_t)(fAngle[1]*100);
			Serial_SensorData.angle_z = (int16_t)(fAngle[2]*100);
			Serial_SensorData.temperature = (int16_t)(temperature);
			Serial_SensorData.humidity = (int16_t)(humidity);
			
			Serial_SendSensorPacket(); // 发送传感器数据包
			Delay_ms(50);
		}
		if (Serial_GetRxFlag() == 1) // 接收到PWM数据包
		{
			// 显示6个PWM值
			OLED_ShowNum(2, 1, Serial_RxPWM[0], 4);
			Servo_yaw_SetAngle(Serial_RxPWM[0]);
			OLED_ShowNum(2, 6, Serial_RxPWM[1], 4);
			Servo_pitch_SetAngle(Serial_RxPWM[1]);
			OLED_ShowNum(3, 1, Serial_RxPWM[2], 4);
			OLED_ShowNum(3, 6, Serial_RxPWM[3], 4);
			OLED_ShowNum(4, 1, Serial_RxPWM[4], 4);
			OLED_ShowNum(4, 6, Serial_RxPWM[5], 4);
		}
		
	}
}


/**********************JY901B********************************/
void CopeCmdData(unsigned char ucData)
{
	 static unsigned char s_ucData[50], s_ucRxCnt = 0;
	
	 s_ucData[s_ucRxCnt++] = ucData;
	 if(s_ucRxCnt<3)return;										//Less than three data returned
	 if(s_ucRxCnt >= 50) s_ucRxCnt = 0;
	 if(s_ucRxCnt >= 3)
	 {
		 if((s_ucData[1] == '\r') && (s_ucData[2] == '\n'))
		 {
		  	s_cCmd = s_ucData[0];
			  memset(s_ucData,0,50);//
			  s_ucRxCnt = 0;
	   } 
		 else 
		 {
			 s_ucData[0] = s_ucData[1];
			 s_ucData[1] = s_ucData[2];
			 s_ucRxCnt = 2;
			}
	  }
}

static void ShowHelp(void)
{
	printf("\r\n************************	 WIT_SDK_DEMO	************************");
	printf("\r\n************************          HELP           ************************\r\n");
	printf("UART SEND:a\\r\\n   Acceleration calibration.\r\n");
	printf("UART SEND:m\\r\\n   Magnetic field calibration,After calibration send:   e\\r\\n   to indicate the end\r\n");
	printf("UART SEND:U\\r\\n   Bandwidth increase.\r\n");
	printf("UART SEND:u\\r\\n   Bandwidth reduction.\r\n");
	printf("UART SEND:B\\r\\n   Baud rate increased to 115200.\r\n");
	printf("UART SEND:b\\r\\n   Baud rate reduction to 9600.\r\n");
	printf("UART SEND:R\\r\\n   The return rate increases to 10Hz.\r\n");
	printf("UART SEND:r\\r\\n   The return rate reduction to 1Hz.\r\n");
	printf("UART SEND:C\\r\\n   Basic return content: acceleration, angular velocity, angle, magnetic field.\r\n");
	printf("UART SEND:c\\r\\n   Return content: acceleration.\r\n");
	printf("UART SEND:h\\r\\n   help.\r\n");
	printf("******************************************************************************\r\n");
}

static void CmdProcess(void)
{
	switch(s_cCmd)
	{
		case 'a':	
			if(WitStartAccCali() != WIT_HAL_OK) 
				printf("\r\nSet AccCali Error\r\n");
			else
				printf("\r\nSet AccCali OK\r\n");
			break;
		case 'm':	
			if(WitStartMagCali() != WIT_HAL_OK) 
				printf("\r\nSet MagCali Error\r\n");
			break;
		case 'e':	
			if(WitStopMagCali() != WIT_HAL_OK)
				printf("\r\nSet MagCali Error\r\n");
			break;
		case 'u':	
			if(WitSetBandwidth(BANDWIDTH_5HZ) != WIT_HAL_OK) 
				printf("\r\nSet Bandwidth Error\r\n");
			break;
		case 'U':	
			if(WitSetBandwidth(BANDWIDTH_256HZ) != WIT_HAL_OK) 
				printf("\r\nSet Bandwidth Error\r\n");
			break;
		case 'B':	
			if(WitSetUartBaud(WIT_BAUD_115200) != WIT_HAL_OK) 
				printf("\r\nSet Baud Error\r\n");
			else 
				JY901B_UartInit(c_uiBaud[WIT_BAUD_115200]);											
			break;
		case 'b':	
			if(WitSetUartBaud(WIT_BAUD_9600) != WIT_HAL_OK)
				printf("\r\nSet Baud Error\r\n");
			else 
				JY901B_UartInit(c_uiBaud[WIT_BAUD_9600]);												
			break;
		case 'R':	
			if(WitSetOutputRate(RRATE_10HZ) != WIT_HAL_OK) 
				printf("\r\nSet Rate Error\r\n");
			break;
		case 'r':	
			if(WitSetOutputRate(RRATE_1HZ) != WIT_HAL_OK) 
				printf("\r\nSet Rate Error\r\n");
			break;
		case 'C':	
			if(WitSetContent(RSW_ACC|RSW_GYRO|RSW_ANGLE|RSW_MAG) != WIT_HAL_OK) 
				printf("\r\nSet RSW Error\r\n");
			break;
		case 'c':	
			if(WitSetContent(RSW_ACC) != WIT_HAL_OK) 
				printf("\r\nSet RSW Error\r\n");
			break;
		case 'h':
			ShowHelp();
			break;
	}
	s_cCmd = 0xff;
}

static void SensorUartSend(uint8_t *p_data, uint32_t uiSize)
{
	JY901B_UartSend(p_data, uiSize);
}

static void Delayms(uint16_t ucMs)
{
	Delay_ms(ucMs);
}

static void SensorDataUpdata(uint32_t uiReg, uint32_t uiRegNum)
{
	int i;
    for(i = 0; i < uiRegNum; i++)
    {
        switch(uiReg)
        {
//            case AX:
//            case AY:
            case AZ:
				s_cDataUpdate |= ACC_UPDATE;
            break;
//            case GX:
//            case GY:
            case GZ:
				s_cDataUpdate |= GYRO_UPDATE;
            break;
//            case HX:
//            case HY:
            case HZ:
				s_cDataUpdate |= MAG_UPDATE;
            break;
//            case Roll:
//            case Pitch:
            case Yaw:
				s_cDataUpdate |= ANGLE_UPDATE;
            break;
            default:
				s_cDataUpdate |= READ_UPDATE;
			break;
        }
		uiReg++;
    }
}



static void AutoScanSensor(void)
{
	int i, iRetry;
	for(i = 1; i < 10; i++)
	{
		JY901B_UartInit(c_uiBaud[i]);
		iRetry = 2;
		do
		{
			s_cDataUpdate = 0;
			
			WitReadReg(AX, 3);
			
			Delay_ms(100);
			if(s_cDataUpdate != 0)
			{
				printf("%d baud find sensor\r\n\r\n", c_uiBaud[i]);
				ShowHelp();
				return ;
			}
			iRetry--;
		}while(iRetry);		
	}
	printf("can not find sensor\r\n");
	printf("please check your connection\r\n");
}
