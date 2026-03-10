#include "stm32f4xx.h"                  // Device header
#include "stm32f4xx_conf.h"

/*
	ADI:PF9 	ADC3->ADC_Channel_7
	ADU:PF10 	ADC3->ADC_Channel_8
*/

uint16_t AD_Value[4];					//定义用于存放AD转换结果的全局数组

#define ADC_LEN 2
uint8_t R_change=10;

/**
  * 函    数：AD初始化
  * 参    数：无
  * 返 回 值：无
  */
void AD_Init(void)
{
	/*开启时钟*/
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC3, ENABLE);	//开启ADC3的时钟
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2|RCC_AHB1Periph_GPIOF, ENABLE);		//开启DMA2的时钟
	
	/*----------------GPIO初始化----------------*/
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    // 配置为模拟输入
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    // 不上拉不下拉
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL ;
	GPIO_Init(GPIOF, &GPIO_InitStructure);
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
	GPIO_Init(GPIOF, &GPIO_InitStructure);
	
	/*----------------DMA初始化---------------*/
	DMA_InitTypeDef DMA_InitStructure;											//定义结构体变量
	// 外设基址为：ADC 数据寄存器地址
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&ADC3->DR;
    // 存储器地址，实际上就是一个内部SRAM的变量
    DMA_InitStructure.DMA_Memory0BaseAddr = (uint32_t)AD_Value;
    // 数据传输方向为外设到存储器
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralToMemory;
    // 缓冲区大小为，指一次传输的数据量
    DMA_InitStructure.DMA_BufferSize = ADC_LEN;
    // 外设寄存器只有一个，地址不用递增
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    // 存储器地址固定
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    // // 外设数据大小为半字，即两个字节
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    //  存储器数据大小也为半字，跟外设数据大小相同
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    // 循环传输模式
    DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;
    // DMA 传输通道优先级为高，当使用一个DMA通道时，优先级设置不影响
    DMA_InitStructure.DMA_Priority = DMA_Priority_High;
    // 禁止DMA FIFO ，使用直连模式
    DMA_InitStructure.DMA_FIFOMode = DMA_FIFOMode_Disable;
    // FIFO 大小，FIFO模式禁止时，这个不用配置
    DMA_InitStructure.DMA_FIFOThreshold = DMA_FIFOThreshold_HalfFull;
    DMA_InitStructure.DMA_MemoryBurst = DMA_MemoryBurst_Single;
    DMA_InitStructure.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;
    // 选择 DMA 通道，通道存在于流中
    DMA_InitStructure.DMA_Channel = DMA_Channel_2;
    //初始化DMA流，流相当于一个大的管道，管道里面有很多通道
    DMA_Init(DMA2_Stream0, &DMA_InitStructure);
    // 使能DMA流
    DMA_Cmd(DMA2_Stream0, ENABLE);
	
	
	/*-----------ADC Common 结构体参数初始化----------*/
	ADC_CommonInitTypeDef ADC_CommonInitStructure;
	// 独立ADC模式
    ADC_CommonInitStructure.ADC_Mode = ADC_Mode_Independent;
    // 时钟为fpclk x分频
    ADC_CommonInitStructure.ADC_Prescaler = ADC_Prescaler_Div4;
    // 禁止DMA直接访问模式
    ADC_CommonInitStructure.ADC_DMAAccessMode = ADC_DMAAccessMode_Disabled;
    // 采样时间间隔，仅双重三重有效
    ADC_CommonInitStructure.ADC_TwoSamplingDelay = ADC_TwoSamplingDelay_20Cycles;
    ADC_CommonInit(&ADC_CommonInitStructure);
	
	/*ADC初始化*/
	ADC_InitTypeDef ADC_InitStructure;											//定义结构体变量
	ADC_StructInit(&ADC_InitStructure);
    // ADC 分辨率
    ADC_InitStructure.ADC_Resolution = ADC_Resolution_12b;
    // 扫描模式，多通道采集需要
    ADC_InitStructure.ADC_ScanConvMode = ENABLE;
    // 连续转换
    ADC_InitStructure.ADC_ContinuousConvMode = ENABLE;
    //禁止外部边沿触发
    ADC_InitStructure.ADC_ExternalTrigConvEdge = ADC_ExternalTrigConvEdge_None;
    //外部触发通道，本例子使用软件触发，此值随便赋值即可
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_T1_CC1;
    //数据右对齐
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
    //转换通道 1个
    ADC_InitStructure.ADC_NbrOfConversion = ADC_LEN;
    ADC_Init(ADC3, &ADC_InitStructure);										//将结构体变量交给ADC_Init，配置ADC3
	// 配置 ADC 通道转换顺序和采样时间周期
    ADC_RegularChannelConfig(ADC3, ADC_Channel_7, 1,
                            ADC_SampleTime_3Cycles);
    ADC_RegularChannelConfig(ADC3, ADC_Channel_8, 2,
                            ADC_SampleTime_3Cycles);
	ADC_DMARequestAfterLastTransferCmd(ADC3, ENABLE);						
	ADC_DMACmd(ADC3, ENABLE);								//ADC3触发DMA2的信号使能
	ADC_Cmd(ADC3, ENABLE);									//ADC3使能
	
	/*ADC触发*/
	ADC_SoftwareStartConv(ADC3);	//软件触发ADC开始工作，由于ADC处于连续转换模式，故触发一次后ADC就可以一直连续不断地工作
}

//大洋科技提供，对应的电流和电压计算
float current_Getdata(void)
{
	float voltage_I=0;
	voltage_I = (AD_Value[1] / 4096.0) * 3.3 ;
	return (10 * (voltage_I - 2.5));
}

float voltage_Getdata(void)
{
	int R_voltage = 11;
	return  (AD_Value[0] / 4096.0) * 3.3 * R_voltage;
}
