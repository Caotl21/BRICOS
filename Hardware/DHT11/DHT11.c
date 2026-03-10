#include "stm32f4xx.h"
#include "DHT11.h"
#include "Delay.h"

//ʹ��PB9��ΪDHT11���źſ�

u8 dht11_delay_flag = 0;

void DHT11_IO_OUT(void)
{
	GPIO_InitTypeDef  GPIO_InitStructure;
 	
 	RCC_AHB1PeriphClockCmd(DHT11_GPIO_CLK, ENABLE);	 
	
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_InitStructure.GPIO_Pin = DHT11_GPIO_PIN;
	GPIO_Init(DHT11_GPIO_PORT,&GPIO_InitStructure);					
}

void DHT11_IO_IN(void)
{
	GPIO_InitTypeDef  GPIO_InitStructure;
 	
 	RCC_AHB1PeriphClockCmd(DHT11_GPIO_CLK, ENABLE);	 
	
 	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_InitStructure.GPIO_Pin = DHT11_GPIO_PIN; 		 
 	GPIO_Init(DHT11_GPIO_PORT, &GPIO_InitStructure);				 
}

//��λDHT11
void DHT11_Rst(void)	   
{  
	DHT11_IO_OUT(); 	//SET OUTPUT
	DHT11_DQ_OUT_0; 	//����DQ
	Delay_ms(20);    	//��������18ms
	DHT11_DQ_OUT_1; 	//DQ=1 
	Delay_us(30);     	//��������20~40us
}
//�ȴ�DHT11�Ļ�Ӧ
//����1:δ��⵽DHT11�Ĵ���
//����0:����
u8 DHT11_Check(void) 	   
{   
	u8 retry=0;
	DHT11_IO_IN();//SET INPUT	 
    while (DHT11_DQ_IN&&retry<100)//DHT11������40~80us
	{
		retry++;
		Delay_us(1);
	};	 
	if(retry>=100)return 1;
	else retry=0;
    while (!DHT11_DQ_IN&&retry<100)//DHT11���ͺ���ٴ�����40~80us
	{
		retry++;
		Delay_us(1);
	};
	if(retry>=100)return 1;	    
	return 0;
}
//��DHT11��ȡһ��λ
//����ֵ��1/0
u8 DHT11_Read_Bit(void) 			 
{
 	u8 retry=0;
	while(DHT11_DQ_IN&&retry<100)//�ȴ���Ϊ�͵�ƽ
	{
		retry++;
		Delay_us(1);
	}
	retry=0;
	while(!DHT11_DQ_IN&&retry<100)//�ȴ���ߵ�ƽ
	{
		retry++;
		Delay_us(1);
	}
	Delay_us(40);//�ȴ�40us
	if(DHT11_DQ_IN)return 1;
	else return 0;		   
}
//��DHT11��ȡһ���ֽ�
//����ֵ������������
u8 DHT11_Read_Byte(void)    
{        
	u8 i,dat;
	dat=0;
	for (i=0;i<8;i++) 
	{
		dat<<=1; 
		dat|=DHT11_Read_Bit();
  }						    
  return dat;
}
//��DHT11��ȡһ������
//temp:�¶�ֵ(��Χ:0~50��)
//humi:ʪ��ֵ(��Χ:20%~90%)
//����ֵ��0,����;1,��ȡʧ��
u8 DHT11_Read_Data(u8 *temp,u8 *humi)    
{        
 	u8 buf[5];
	u8 i;
	//DHT11_Rst();
	//ִ��Rst
	switch(dht11_delay_flag)
	{
		case 0:
			DHT11_IO_OUT(); 	//SET OUTPUT
			DHT11_DQ_OUT_0; 	//����DQ
			dht11_delay_flag = 1;
			return 0;
		case 1:
			DHT11_DQ_OUT_1; 	//DQ=1 
			Delay_us(30);     	//��������20~40us
			dht11_delay_flag = 0;
		break;
	}
	
	if(DHT11_Check()==0)
	{
		for(i=0;i<5;i++)//��ȡ40λ����
		{
			buf[i]=DHT11_Read_Byte();
		}
		if((buf[0]+buf[1]+buf[2]+buf[3])==buf[4])
		{
			*humi=buf[0];
			*temp=buf[2];
		}
	}else return 1;

	
	return 0;	    
}
//��ʼ��DHT11��IO�� DQ ͬʱ���DHT11�Ĵ���
//����1:������
//����0:����    	 
u8 DHT11_Init(void)
{	 		    
	DHT11_Rst();  //��λDHT11
	return DHT11_Check();//�ȴ�DHT11�Ļ�Ӧ
} 
