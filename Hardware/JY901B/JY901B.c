#include <stdio.h>
#include "stm32f4xx.h"
#include "stm32f4xx_conf.h"
#include "misc.h"
#include "wit_c_sdk.h"
#include "JY901B_UART.h"
#include "Delay.h"
#include "JY901B.h"
#include "TaskScheduler.h"

#define ACC_UPDATE		0x01
#define GYRO_UPDATE		0x02
#define ANGLE_UPDATE	0x04
#define MAG_UPDATE		0x08
#define READ_UPDATE		0x80

volatile char s_cDataUpdate = 0, s_cCmd = 0xff;
const uint32_t c_uiBaud[10] = {0, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};
struct_JY901BFifo JY901BFifo;
/**
  * 函    数：从串口中获取对JY901B操作的指令
  * 参    数：ucData为从串口获取的数据，串口的格式是指令字母加换行,例如：
	"a
	"
  * 返 回 值：无
  */
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
/**
  * 函    数：从串口输出信息
  * 参    数：无
  * 返 回 值：无
  */
void ShowHelp(void)
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
/**
  * 函    数：根据获取到的命令进行对应的操作
  * 参    数：无
  * 返 回 值：无
  */
void CmdProcess(void)
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
/**
  * 函    数：用来对JY901B发送数据
  * 参    数：p_data为要写入的数据的指针，uiSize为数据大小
  * 返 回 值：无
  */
void SensorUartSend(uint8_t *p_data, uint32_t uiSize)
{
	JY901B_UartSend(p_data, uiSize);
}
/**
  * 函    数：延时函数
  * 参    数：ucMs为毫秒数
  * 返 回 值：无
  */
void Delayms(uint16_t ucMs)
{
	Delay_ms(ucMs);
}
/**
  * 函    数：JY901B数据更新
  * 参    数：uiReg更新数据的类型，uiRegNum更新的数量
  * 返 回 值：无
  */
void SensorDataUpdata(uint32_t uiReg, uint32_t uiRegNum)
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

/**
  * 函    数：用不同分段的波特率初始化来扫描JY901B
  * 参    数：无
  * 返 回 值：无
  */
void AutoScanSensor(void)
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
/**
  * 函    数：JY901B初始化函数
  * 参    数：无
  * 返 回 值：无
  */
void JY901B_Init(void)
{
	JY901B_UartInit(115200);			//初始化获取模块数据串口
	WitInit(WIT_PROTOCOL_NORMAL, 0x50);	//初始化标准协议，此处为normal协议，无效
	WitSerialWriteRegister(SensorUartSend);//注册写回调函数
	WitRegisterCallBack(SensorDataUpdata);//注册获取传感器数据回调函数
	WitDelayMsRegister(Delayms);
	//AutoScanSensor();				//由于串口中断只进行数据存储，此时autoscan就不能用了
	JY901BFifo.Cnt = 0;
	JY901BFifo.In = 0;
	JY901BFifo.Out = 0;
}
/**
  * 函    数：处理JY901B的数据来获取数据
  * 参    数：fAcc[]加速度计信息，fGyro[]陀螺仪信息，fAngle[]角度信息
  * 返 回 值：无
  */
void JY901B_GetData(float fAcc[],float fGyro[],float fAngle[])
{
	if(s_cDataUpdate)
	{

		u8 i;
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

}
static uint8_t __CaliSum(uint8_t *data, uint32_t len)
{
    uint32_t i;
    uint8_t ucCheck = 0;
    for(i=0; i<len; i++) ucCheck += *(data + i);
    return ucCheck;
}
/**
  * 函    数：处理JY901B通过串口发来的数据
  * 参    数：无
  * 返 回 值：无
  */
void JY901B_process(void)
{
	//由于有多组数据包，因此不能只进行一次数据包处理
    while(JY901BFifo.Cnt > 0)
        {
            uint16_t usData[4];
			uint8_t ucSum;
			//1.检测包头
            while(JY901BFifo.JY901BRxBuf[0] != 0x55 && JY901BFifo.Cnt>0)
            {
               __disable_irq();
                JY901BFifo.Cnt--;
				__enable_irq();
                memcpy(JY901BFifo.JY901BRxBuf, &JY901BFifo.JY901BRxBuf[1], JY901BFifo.Cnt);
			}
			//队列数据满足一个包的长度，进行处理
            if(JY901BFifo.Cnt >= 11)
            {
				//校验
                ucSum = __CaliSum(JY901BFifo.JY901BRxBuf, 10);
                if(ucSum != JY901BFifo.JY901BRxBuf[10])
                {
                    __disable_irq();
					JY901BFifo.Cnt--;
					__enable_irq();
                    memcpy(JY901BFifo.JY901BRxBuf, &JY901BFifo.JY901BRxBuf[1], JY901BFifo.Cnt);
                    //不能退出，因为还要重新判断包头
                }
				else//没问题开始提取数据
				{
					usData[0] = ((uint16_t)JY901BFifo.JY901BRxBuf[3] << 8) | (uint16_t)JY901BFifo.JY901BRxBuf[2];
					usData[1] = ((uint16_t)JY901BFifo.JY901BRxBuf[5] << 8) | (uint16_t)JY901BFifo.JY901BRxBuf[4];
					usData[2] = ((uint16_t)JY901BFifo.JY901BRxBuf[7] << 8) | (uint16_t)JY901BFifo.JY901BRxBuf[6];
					usData[3] = ((uint16_t)JY901BFifo.JY901BRxBuf[9] << 8) | (uint16_t)JY901BFifo.JY901BRxBuf[8];
					CopeWitData_user(JY901BFifo.JY901BRxBuf[1], usData, 4);
					JY901BFifo.Cnt = JY901BFifo.Cnt - 11;
					memcpy(JY901BFifo.JY901BRxBuf, &JY901BFifo.JY901BRxBuf[11], JY901BFifo.Cnt);
					//如果数据不到11个字节了，那就不处理了等下次处理
					if(JY901BFifo.Cnt < 11) return;
					//JY901BFifo.Cnt = 0;
				}
				//将数据存储
            }
        }
		
}
void USART2_IRQHandler(void)
{
	unsigned char ucTemp;
	if(USART_GetITStatus(USART2, USART_IT_RXNE) != RESET)
	{
		ucTemp = USART_ReceiveData(USART2);
		JY901BFifo_in(ucTemp);//接收来自JY901B的数据并保存数据
		//WitSerialDataIn(ucTemp);
		g_event_JY901B_received = 1;
		USART_ClearITPendingBit(USART2, USART_IT_RXNE);
	}
}
