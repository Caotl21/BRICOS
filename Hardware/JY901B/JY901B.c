#include <stdio.h>
#include "stm32f4xx.h"
#include "stm32f4xx_conf.h"
#include "misc.h"
#include "wit_c_sdk.h"
#include "Delay.h"
#include "JY901B.h"
#include "TaskScheduler.h"
#include "Serial.h"

#define ACC_UPDATE		0x01
#define GYRO_UPDATE		0x02
#define QUATER_UPDATE	0x04
#define MAG_UPDATE		0x08
#define READ_UPDATE		0x80

volatile char s_cDataUpdate = 0, s_cCmd = 0xff;
const uint32_t c_uiBaud[10] = {0, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};
struct_JY901BFifo JY901BFifo;
/**
  * ��    �����Ӵ����л�ȡ��JY901B������ָ��
  * ��    ����ucDataΪ�Ӵ��ڻ�ȡ�����ݣ����ڵĸ�ʽ��ָ����ĸ�ӻ���,���磺
	"a
	"
  * �� �� ֵ����
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
  * ��    �����Ӵ��������Ϣ
  * ��    ������
  * �� �� ֵ����
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
  * ��    �������ݻ�ȡ����������ж�Ӧ�Ĳ���
  * ��    ������
  * �� �� ֵ����
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
			break;
		case 'b':	
			if(WitSetUartBaud(WIT_BAUD_9600) != WIT_HAL_OK)
				printf("\r\nSet Baud Error\r\n");												
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
  * ��    ����������JY901B��������
  * ��    ����p_dataΪҪд������ݵ�ָ�룬uiSizeΪ���ݴ�С
  * �� �� ֵ����
  */
void SensorUartSend(uint8_t *p_data, uint32_t uiSize)
{
	USART2_UartSend(p_data, uiSize);
}
/**
  * ��    ������ʱ����
  * ��    ����ucMsΪ������
  * �� �� ֵ����
  */
void Delayms(uint16_t ucMs)
{
	Delay_ms(ucMs);
}
/**
  * ��    ����JY901B���ݸ���
  * ��    ����uiReg�������ݵ����ͣ�uiRegNum���µ�����
  * �� �� ֵ����
  */
void SensorDataUpdata(uint32_t uiReg, uint32_t uiRegNum)
{
	int i;
    for(i = 0; i < uiRegNum; i++)
    {
        switch(uiReg)
        {

            case AZ:
				s_cDataUpdate |= ACC_UPDATE;
            break;

            case GZ:
				s_cDataUpdate |= GYRO_UPDATE;
            break;

            case HZ:
				s_cDataUpdate |= MAG_UPDATE;
            break;

            case Yaw:
				s_cDataUpdate |= QUATER_UPDATE;
            break;
            default:
				s_cDataUpdate |= READ_UPDATE;
			break;
        }
		uiReg++;
    }
}

/**
  * ��    ����JY901B��ʼ������
  * ��    ������
  * �� �� ֵ����
  */
void JY901B_Init(void)
{
	WitInit(WIT_PROTOCOL_NORMAL, 0x50);	//��ʼ����׼Э�飬�˴�ΪnormalЭ�飬��Ч
	WitSerialWriteRegister(SensorUartSend);//ע��д�ص�����
	WitRegisterCallBack(SensorDataUpdata);//ע���ȡ���������ݻص�����
	WitDelayMsRegister(Delayms);

	JY901BFifo.Cnt = 0;
	JY901BFifo.In = 0;
	JY901BFifo.Out = 0;
}
/**
  * ��    ��������JY901B����������ȡ����
  * ��    ����fAcc[]���ٶȼ���Ϣ��fGyro[]��������Ϣ��fQuater[]��Ԫ����Ϣ
  * �� �� ֵ����
  */
void JY901B_GetData(float fAcc[],float fGyro[],float fQuater[])
{
	if(s_cDataUpdate)
	{

		uint8_t i;
		for(i = 0; i < 3; i++)
		{
			fAcc[i] = sReg[AX+i] / 32768.0f * 16.0f;
			fGyro[i] = sReg[GX+i] / 32768.0f * 2000.0f;
		}
		for(i = 0; i < 4; i++)
		{
			fQuater[i] = sReg[q0+i] / 32768.0f;
		}
		if(s_cDataUpdate & ACC_UPDATE)
		{
			s_cDataUpdate &= ~ACC_UPDATE;
		}
		if(s_cDataUpdate & GYRO_UPDATE)
		{
			s_cDataUpdate &= ~GYRO_UPDATE;
		}
		if(s_cDataUpdate & QUATER_UPDATE)
		{
			s_cDataUpdate &= ~QUATER_UPDATE;
		}
		if(s_cDataUpdate & MAG_UPDATE)
		{
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
  * ��    ��������JY901Bͨ�����ڷ���������
  * ��    ������
  * �� �� ֵ����
  */
void JY901B_process(void)
{
	uint16_t usData[4];
	uint8_t ucSum;
	uint8_t data_temp[11] = {0};
	uint16_t cnt_temp = 0;	//������ֵ�����Ҹպü�¼����Outֵ
	//�����ж������ݰ�����˲���ֻ����һ�����ݰ�����
	if (JY901BFifo.In == JY901BFifo.Out){
		printf("empty!");
		return;
	}
	else if(JY901BFifo.In > JY901BFifo.Out) 
	{JY901BFifo.Cnt = JY901BFifo.In - JY901BFifo.Out;}
	else
	{JY901BFifo.Cnt = JY901BFifoSize + JY901BFifo.In - JY901BFifo.Out;}
	
	
	while(JY901BFifo.Cnt >= 11)
        {
			//1.����ͷ
            while(JY901BFifo.JY901BRxBuf[JY901BFifo.Out] != 0x55 && JY901BFifo.Cnt>0)
            {
               __disable_irq();
                JY901BFifo.Cnt--;
				__enable_irq();
				if(++JY901BFifo.Out>=JY901BFifoSize) JY901BFifo.Out=0;
			}
			//������������һ�����ĳ��ȣ����д���
            if(JY901BFifo.Cnt >= 11)
            {
				for(int i=0;i<11;i++)
				{
					//�ȼ�¼��һ�����ݵ����
					cnt_temp = JY901BFifo.Out + i;
					//����������жϷ�Χ
					if(cnt_temp >= JY901BFifoSize) 
						cnt_temp = cnt_temp - JY901BFifoSize;
					data_temp[i] = JY901BFifo.JY901BRxBuf[cnt_temp];
				}
				//У��
                ucSum = __CaliSum(data_temp, 10);
                if(ucSum != data_temp[10])
                {
                    __disable_irq();
					JY901BFifo.Cnt--;
					__enable_irq();
					if(++JY901BFifo.Out>=JY901BFifoSize) JY901BFifo.Out=0;
                    //�����˳�����Ϊ��Ҫ�����жϰ�ͷ
                }
				else//û���⿪ʼ��ȡ����
				{
					usData[0] = ((uint16_t)data_temp[3] << 8) | (uint16_t)data_temp[2];
					usData[1] = ((uint16_t)data_temp[5] << 8) | (uint16_t)data_temp[4];
					usData[2] = ((uint16_t)data_temp[7] << 8) | (uint16_t)data_temp[6];
					usData[3] = ((uint16_t)data_temp[9] << 8) | (uint16_t)data_temp[8];
					CopeWitData_user(data_temp[1], usData, 4);
					
					JY901BFifo.Cnt = JY901BFifo.Cnt - 11;
					JY901BFifo.Out = cnt_temp + 1;
					if(JY901BFifo.Out>=JY901BFifoSize) JY901BFifo.Out=0;
					//������ݲ���11���ֽ��ˣ��ǾͲ������˵��´δ���
					if(JY901BFifo.Cnt < 11) 
					{
						return;
					}
					//JY901BFifo.Cnt = 0;
				}
				//�����ݴ洢
            }
        }
		
}
