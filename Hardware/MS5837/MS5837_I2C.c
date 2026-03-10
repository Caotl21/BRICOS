#include "stm32f4xx.h"
#include "Delay.h"

/*
	I2C1_SCL:PB6	I2C1_SDA:PB7
*/
/*
C1 ѹ�������� SENS|T1
C2  ѹ������  OFF|T1
C3	�¶�ѹ��������ϵ�� TCS
C4	�¶�ϵ����ѹ������ TCO
C5	�ο��¶� T|REF
C6 	�¶�ϵ�����¶� TEMPSENS
*/
uint32_t Cal_C[7]; //���ڴ��PROM�е�6������C1-C6

double OFF_;
float Aux;
/*
dT ʵ�ʺͲο��¶�֮��Ĳ���
Temperature ʵ���¶�	
*/
uint64_t dT, Temperature;
/*
OFF ʵ���¶Ȳ���
SENS ʵ���¶�������
*/
uint64_t SENS;
uint32_t D1_Pres, D2_Temp;		 // ����ѹ��ֵ,�����¶�ֵ
uint32_t TEMP2, T2, OFF2, SENS2; //�¶�У��ֵ

uint32_t Pressure;			  //��ѹ
uint32_t Depth;
float Atmdsphere_Pressure = 950;//985.0; //����ѹ

u8 ms5837_flag=0;

////���ʹ���������ŵĻ���Ҫ���ģ�ע�⣬������Ҫ��һ��ʱ�ӵĳ�ʼ���Լ�GPIO�ȣ�������GPIOB��
//#define MS5837_SCL_Pin GPIO_Pin_0
//#define MS5837_SDA_Pin GPIO_Pin_1
//дSCL��ƽֵ
void MS5837I2C_W_SCL(uint8_t BitValue)
{
	GPIO_WriteBit(GPIOB, GPIO_Pin_6, (BitAction)BitValue);
	Delay_us(2);
}
//дSDA��ƽ
void MS5837I2C_W_SDA(uint8_t BitValue)
{
	GPIO_WriteBit(GPIOB, GPIO_Pin_7, (BitAction)BitValue);
	//Delay_us(10);
	Delay_us(2);
}
//��ȡ����SDA�ĵ�ƽ
uint8_t MS5837I2C_R_SDA(void)
{
	uint8_t BitValue;
	BitValue = GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_7);
//	Delay_us(10);
	Delay_us(2);
	return BitValue;
}

//��ʼ������iic������
void MS5837I2C_Init(void)
{
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB,ENABLE);
	
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_OD;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6;
 	GPIO_Init(GPIOB, &GPIO_InitStructure);
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7;
 	GPIO_Init(GPIOB, &GPIO_InitStructure);
	
	GPIO_SetBits(GPIOB, GPIO_Pin_6 | GPIO_Pin_7);
}

//ͨ����SCL�ߵ�ƽ��ʱ��SDA����½�������ʾiic��ʼ
void MS5837I2C_Start(void)
{
	MS5837I2C_W_SDA(1);
	MS5837I2C_W_SCL(1);
	MS5837I2C_W_SDA(0);
	MS5837I2C_W_SCL(0);
}
//ͨ����SCL�ߵ�ƽʱSDA������������ʾiic����
void MS5837I2C_Stop(void)
{
	MS5837I2C_W_SDA(0);
	MS5837I2C_W_SCL(1);
	MS5837I2C_W_SDA(1);
}
//����Ӧ����Ϣ
void MS5837I2C_SendAck(uint8_t AckBit)
{
	MS5837I2C_W_SDA(AckBit);
	MS5837I2C_W_SCL(1);
	MS5837I2C_W_SCL(0);
}
//����Ӧ���ź�
uint8_t MS5837I2C_ReceiveAck(void)
{
	uint8_t AckBit;
	MS5837I2C_W_SDA(1);	//һ��Ҫ��©�����1
	MS5837I2C_W_SCL(1);
	AckBit = MS5837I2C_R_SDA();
	MS5837I2C_W_SCL(0);
	return AckBit;
}
//ʹ��iic����һ���ֽڣ�SDA��������SCL�ߵ�ƽʱ����Ч�����ҽ�����SCL���ͷ������ʹ��
void MS5837I2C_SendByte(uint8_t Byte)
{
	uint8_t i;
	for (i = 0; i < 8; i ++)
	{
		MS5837I2C_W_SDA(Byte & (0x80 >> i));
		MS5837I2C_W_SCL(1);
		MS5837I2C_W_SCL(0);
	}
	MS5837I2C_ReceiveAck();
}
//ʹ��iic����һ���ֽڣ���©���ʱ��SDA��1ʱ�����ѹ���ⲿ������
uint8_t MS5837I2C_ReceiveByte(u8 ack)
{
	uint8_t i, Byte = 0x00;
	MS5837I2C_W_SDA(1);				//��©�����ʱ��ѹ���ⲿ������������
	for (i = 0; i < 8; i ++)
	{
		MS5837I2C_W_SCL(1);
		if (MS5837I2C_R_SDA() == 1){Byte |= (0x80 >> i);}
		MS5837I2C_W_SCL(0);
	}
	MS5837I2C_SendAck(ack);
	return Byte;
}




/************ ���亯�� ****************/
u8 MS5837_write(u8 addr, u8 reg, u8 len, u8* buf)
{
	u8 i;
	MS5837I2C_Start();
	MS5837I2C_SendByte(addr);
	MS5837I2C_SendByte(reg);
	for(i=0;i<len;i++)
	{
		MS5837I2C_SendByte(*buf++);
	}
	MS5837I2C_Stop();
	return 0;
}

u8 Ms5837_read(u8 addr, u8 reg, u8 len, u8 *buf)
{
	u8 i;
	MS5837I2C_Start();
	MS5837I2C_SendByte(addr);
	MS5837I2C_SendByte(reg);
	
	MS5837I2C_Start();
	MS5837I2C_SendByte(addr+1);		
	for(i=0;i<len-1;i++)
	{
		*buf++ = MS5837I2C_ReceiveByte(0);
	}
	*buf = MS5837I2C_ReceiveByte(1);		//�����Ӧ��˳�򣿣���
	MS5837I2C_Stop();
	return 0;
}
/*******************************************************************************
  * @��������	MS583730BA_RESET
  * @����˵��   ��λMS5611
  * @�������   ��
  * @�������   ��
  * @���ز���   ��
*******************************************************************************/
void MS583703BA_RESET(void)
{
	MS5837I2C_Start();
	MS5837I2C_SendByte(0xEC);
	MS5837I2C_SendByte(0x1E);
	MS5837I2C_Stop();
	
}

void MS5837_init(void)
{
	u8 inth,intl;
	u8 i;
	MS5837I2C_Init();
	MS583703BA_RESET();
	Delay_ms(30);//��ʱ�б�Ҫ�����Ͻ��ܲ�����20ms
	for(i=0;i<=6;i++)
	{
		MS5837I2C_Start();
		MS5837I2C_SendByte(0xEC);
		MS5837I2C_SendByte(0xA0 + (i * 2));
		MS5837I2C_Stop();
		Delay_us(5);
		MS5837I2C_Start();
		MS5837I2C_SendByte(0xEC + 0x01);
		inth = MS5837I2C_ReceiveByte(1);
		intl = MS5837I2C_ReceiveByte(0);
		MS5837I2C_Stop();
		Cal_C[i] = (((uint16_t)inth<<8)|intl);
	}
	ms5837_flag=0;
}


///***********************************************
//  * @brief  ��ȡ�¶���Ϣ������1��������һ��Ҫ���¶�������2֮ǰ���У�ʱ��������10ms
//  * @param  None
//  * @retval None
//************************************************/
void MS5837_GetTemp_1(void)
{
	MS5837I2C_Start();
	MS5837I2C_SendByte(0xEC);
	MS5837I2C_SendByte(0x58);
	MS5837I2C_Stop();
}

///***********************************************
//  * @brief  ��ȡ�¶���Ϣ������2��������һ��Ҫ���¶�������1֮�����У�ʱ��������10ms
//  * @param  float * outTemp �¶� 
//  * @retval None
//************************************************/
void MS5837_GetTemp_2(float * outTemp)
{
	u8 temp[3] = {0};
	//��ʼ��ȡ
	Ms5837_read(0xEC,0,3,temp);
	D2_Temp = ((uint32_t)temp[0]<<16)|((uint32_t)temp[1]<<8)|temp[2];
	//��������
	if (D2_Temp > (((uint32_t)Cal_C[5]) * 256))
	{
		dT = D2_Temp - (((uint32_t)Cal_C[5]) * 256);
		Temperature = 2000 + dT * ((uint32_t)Cal_C[6]) / 8388608;
		OFF_ = (uint32_t)Cal_C[2] * 65536 + ((uint32_t)Cal_C[4] * dT) / 128;
		SENS = (uint32_t)Cal_C[1] * 32768 + ((uint32_t)Cal_C[3] * dT) / 256;
	}
	else
	{
		dT = (((uint32_t)Cal_C[5]) * 256) - D2_Temp;
		Temperature = 2000 - dT * ((uint32_t)Cal_C[6]) / 8388608;
		OFF_ = (uint32_t)Cal_C[2] * 65536 - ((uint32_t)Cal_C[4] * dT) / 128;
		SENS = (uint32_t)Cal_C[1] * 32768 - ((uint32_t)Cal_C[3] * dT) / 256;
	}
	if (Temperature < 2000) // low temp
	{
		Aux = (2000 - Temperature) * (2000 - Temperature);
		T2 = 3 * (dT * dT) / 8589934592;
		OFF2 = 3 * Aux / 2;
		SENS2 = 5 * Aux / 8;
	}
	else
	{
		Aux = (Temperature - 2000) * (Temperature - 2000);
		T2 = 2 * (dT * dT) / 137438953472;
		OFF2 = 1 * Aux / 16;
		SENS2 = 0;
	}
	OFF_ = OFF_ - OFF2;
	SENS = SENS - SENS2;
	*outTemp = (float)(Temperature - T2) / 100.0f;				//���϶�
}


///***********************************************
//  * @brief  ��ȡ�����Ϣ������1��������һ��Ҫ�����������2֮ǰ���У�ʱ��������10ms
//  * @param  None
//  * @retval None
//************************************************/
void MS5837_GetDepth_1(void)
{
	MS5837I2C_Start();
	MS5837I2C_SendByte(0xEC);
	MS5837I2C_SendByte(0x48);
	MS5837I2C_Stop();
}

///***********************************************
//  * @brief  ��ȡ�����Ϣ������2��������һ��Ҫ�����������1֮�����У�ʱ��������10ms
//  * @param  float * outPressѹǿ��Ϣ, float *outDepth�����Ϣ
//  * @retval None
//************************************************/
void MS5837_GetDepth_2(float * outPress, float *outDepth)
{
	//��ʼ��ȡ
	u8 temp[3] = {0};
	Ms5837_read(0xEC,0,3,temp);
	D1_Pres = ((uint32_t)temp[0]<<16)|((uint32_t)temp[1]<<8)|temp[2];
	*outPress =(float)((D1_Pres * SENS / 2097152 - OFF_) / 8192) / 10.0f;	//mbr
	if(ms5837_flag == 0)					//�˴���Ϊ�˳�ʼ��ʱ��¼��ǰ����ѹ��״̬���������ȷ������ѹ����Ҫ�Ķ��ˡ�
	{
		Atmdsphere_Pressure = *outPress;//*outPress;
		ms5837_flag = 1;
	}
	*outDepth = (*outPress - Atmdsphere_Pressure)/0.983615;		//cm
}
