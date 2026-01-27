#include "stm32f4xx.h"
#include "Delay.h"
#include "OLED.h"

/*
	I2C1_SCL:PB6	I2C1_SDA:PB7
*/
/*
C1 压力灵敏度 SENS|T1
C2  压力补偿  OFF|T1
C3	温度压力灵敏度系数 TCS
C4	温度系数的压力补偿 TCO
C5	参考温度 T|REF
C6 	温度系数的温度 TEMPSENS
*/
uint32_t Cal_C[7]; //用于存放PROM中的6组数据C1-C6

double OFF_;
float Aux;
/*
dT 实际和参考温度之间的差异
Temperature 实际温度	
*/
uint64_t dT, Temperature;
/*
OFF 实际温度补偿
SENS 实际温度灵敏度
*/
uint64_t SENS;
uint32_t D1_Pres, D2_Temp;		 // 数字压力值,数字温度值
uint32_t TEMP2, T2, OFF2, SENS2; //温度校验值

uint32_t Pressure;			  //气压
uint32_t Depth;
float Atmdsphere_Pressure = 950;//985.0; //大气压

u8 ms5837_flag=0;

////如果使用其它引脚的话需要更改（注意，还有需要改一下时钟的初始化以及GPIO等，现在是GPIOB）
//#define MS5837_SCL_Pin GPIO_Pin_0
//#define MS5837_SDA_Pin GPIO_Pin_1
//写SCL电平值
void MS5837I2C_W_SCL(uint8_t BitValue)
{
	GPIO_WriteBit(GPIOB, GPIO_Pin_6, (BitAction)BitValue);
	Delay_us(2);
}
//写SDA电平
void MS5837I2C_W_SDA(uint8_t BitValue)
{
	GPIO_WriteBit(GPIOB, GPIO_Pin_7, (BitAction)BitValue);
	//Delay_us(10);
	Delay_us(2);
}
//读取引脚SDA的电平
uint8_t MS5837I2C_R_SDA(void)
{
	uint8_t BitValue;
	BitValue = GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_7);
//	Delay_us(10);
	Delay_us(2);
	return BitValue;
}

//初始化软件iic的引脚
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

//通过在SCL高电平的时候SDA输出下降沿来表示iic开始
void MS5837I2C_Start(void)
{
	MS5837I2C_W_SDA(1);
	MS5837I2C_W_SCL(1);
	MS5837I2C_W_SDA(0);
	MS5837I2C_W_SCL(0);
}
//通过在SCL高电平时SDA线上升沿来表示iic结束
void MS5837I2C_Stop(void)
{
	MS5837I2C_W_SDA(0);
	MS5837I2C_W_SCL(1);
	MS5837I2C_W_SDA(1);
}
//发送应答信息
void MS5837I2C_SendAck(uint8_t AckBit)
{
	MS5837I2C_W_SDA(AckBit);
	MS5837I2C_W_SCL(1);
	MS5837I2C_W_SCL(0);
}
//接受应答信号
uint8_t MS5837I2C_ReceiveAck(void)
{
	uint8_t AckBit;
	MS5837I2C_W_SDA(1);	//一定要开漏输出置1
	MS5837I2C_W_SCL(1);
	AckBit = MS5837I2C_R_SDA();
	MS5837I2C_W_SCL(0);
	return AckBit;
}
//使用iic发送一个字节，SDA的数据在SCL高电平时才有效，并且结束后将SCL拉低方便后续使用
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
//使用iic接受一个字节，开漏输出时，SDA置1时输出电压由外部决定。
uint8_t MS5837I2C_ReceiveByte(u8 ack)
{
	uint8_t i, Byte = 0x00;
	MS5837I2C_W_SDA(1);				//开漏输出此时电压由外部决定！！！！
	for (i = 0; i < 8; i ++)
	{
		MS5837I2C_W_SCL(1);
		if (MS5837I2C_R_SDA() == 1){Byte |= (0x80 >> i);}
		MS5837I2C_W_SCL(0);
	}
	MS5837I2C_SendAck(ack);
	return Byte;
}




/************ 补充函数 ****************/
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
	*buf = MS5837I2C_ReceiveByte(1);		//这里的应答顺序？？？
	MS5837I2C_Stop();
	return 0;
}
/*******************************************************************************
  * @函数名称	MS583730BA_RESET
  * @函数说明   复位MS5611
  * @输入参数   无
  * @输出参数   无
  * @返回参数   无
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
	Delay_ms(30);//延时有必要，网上介绍不少于20ms
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

/**************************实现函数********************************************
*函数原型:unsigned long MS561101BA_getConversion(void)
*功　　能:    读取 MS5837 的转换结果 
*******************************************************************************/
uint32_t MS583703BA_getConversion(uint8_t command)
{
	uint32_t conversion = 0;
	u8 temp[3] = {0};
	MS5837I2C_Start();
	MS5837I2C_SendByte(0xEC);
	MS5837I2C_SendByte(command);
	MS5837I2C_Stop();
	Delay_ms(10);	//等待AD转换结束
	//开始读取
	Ms5837_read(0xEC,0,3,temp);
	conversion = ((uint32_t)temp[0]<<16)|((uint32_t)temp[1]<<8)|temp[2];
	return conversion;
}
///***********************************************
//  * @brief  读取数据
//  * @param  None
//  * @retval None
//************************************************/
void MS5837_Getdata(float * outTemp, float * outPress, float *outDepth)
{
	//通过设置相应的指令获取对应的数据
	D1_Pres = MS583703BA_getConversion(0x48);
	D2_Temp = MS583703BA_getConversion(0x58);
	

	//处理数据
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
	*outTemp = (float)(Temperature - T2) / 100.0f;				//摄氏度
	*outPress =(float)((D1_Pres * SENS / 2097152 - OFF_) / 8192) / 10.0f;	//mbr
	if(ms5837_flag == 0)					//此处是为了初始化时记录当前大气压的状态，后续如果确定大气压则不需要改动了。
	{
		Atmdsphere_Pressure = *outPress;
		ms5837_flag = 1;
	}
	*outDepth = (*outPress - Atmdsphere_Pressure)/0.983615;		//cm
}

///***********************************************
//  * @brief  读取温度信息子任务1，此任务一定要在温度子任务2之前运行，时间间隔大于10ms
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
//  * @brief  读取温度信息子任务2，此任务一定要在温度子任务1之后运行，时间间隔大于10ms
//  * @param  float * outTemp 温度 
//  * @retval None
//************************************************/
void MS5837_GetTemp_2(float * outTemp)
{
	u8 temp[3] = {0};
	//开始读取
	Ms5837_read(0xEC,0,3,temp);
	D2_Temp = ((uint32_t)temp[0]<<16)|((uint32_t)temp[1]<<8)|temp[2];
	//处理数据
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
	*outTemp = (float)(Temperature - T2) / 100.0f;				//摄氏度
}


///***********************************************
//  * @brief  读取深度信息子任务1，此任务一定要在深度子任务2之前运行，时间间隔大于10ms
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
//  * @brief  读取深度信息子任务2，此任务一定要在深度子任务1之后运行，时间间隔大于10ms
//  * @param  float * outPress压强信息, float *outDepth深度信息
//  * @retval None
//************************************************/
void MS5837_GetDepth_2(float * outPress, float *outDepth)
{
	//开始读取
	u8 temp[3] = {0};
	Ms5837_read(0xEC,0,3,temp);
	D1_Pres = ((uint32_t)temp[0]<<16)|((uint32_t)temp[1]<<8)|temp[2];
	*outPress =(float)((D1_Pres * SENS / 2097152 - OFF_) / 8192) / 10.0f;	//mbr
	if(ms5837_flag == 0)					//此处是为了初始化时记录当前大气压的状态，后续如果确定大气压则不需要改动了。
	{
		Atmdsphere_Pressure = *outPress;//*outPress;
		ms5837_flag = 1;
	}
	*outDepth = (*outPress - Atmdsphere_Pressure)/0.983615;		//cm
}
