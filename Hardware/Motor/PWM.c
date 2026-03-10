#include "stm32f4xx.h"                  // Device header

/*
motor:
	TIM3_CH1:PA6 TIM3_CH2:PA7 TIM3_CH3:PB0
	TIM3_CH4:PB1 TIM4_CH1:PD12 TIM4_CH2:PD13
servo:
	TIM9_CH1:PE5 TIM9_CH2:PE6
LED:
	TIM4_CH3:PD14 TIM4_CH4:PD15
*/


/**
  * 函    数：PWM初始化
  * 参    数：无
  * 返 回 值：无
  */
void PWM_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	/*开启时钟*/
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);			//开启TIM3的时钟
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);			//开启TIM4的时钟
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM9, ENABLE);
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA,ENABLE);			
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB,ENABLE);
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOD,ENABLE);			
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE,ENABLE);

	/*引脚复用*/
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource6, GPIO_AF_TIM3);
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource7, GPIO_AF_TIM3);
	GPIO_PinAFConfig(GPIOB, GPIO_PinSource0, GPIO_AF_TIM3);
	GPIO_PinAFConfig(GPIOB, GPIO_PinSource1, GPIO_AF_TIM3);

	GPIO_PinAFConfig(GPIOD, GPIO_PinSource12, GPIO_AF_TIM4);
	GPIO_PinAFConfig(GPIOD, GPIO_PinSource13, GPIO_AF_TIM4);
	GPIO_PinAFConfig(GPIOD, GPIO_PinSource14, GPIO_AF_TIM4);
	GPIO_PinAFConfig(GPIOD, GPIO_PinSource15, GPIO_AF_TIM4);
	
	GPIO_PinAFConfig(GPIOE, GPIO_PinSource5, GPIO_AF_TIM9);
	GPIO_PinAFConfig(GPIOE, GPIO_PinSource6, GPIO_AF_TIM9);

	
	 /* GPIO初始化 */
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_Init(GPIOA, &GPIO_InitStructure);	
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1;
    GPIO_Init(GPIOB, &GPIO_InitStructure);	
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15;
    GPIO_Init(GPIOD, &GPIO_InitStructure);	
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_6;
    GPIO_Init(GPIOE, &GPIO_InitStructure);
	
	/*配置时钟源*/
	TIM_InternalClockConfig(TIM3);		//选择TIM3为内部时钟，若不调用此函数，TIM默认也为内部时钟
	TIM_InternalClockConfig(TIM4);		//选择TIM3为内部时钟，若不调用此函数，TIM默认也为内部时钟
	TIM_InternalClockConfig(TIM9);		//选择TIM3为内部时钟，若不调用此函数，TIM默认也为内部时钟
	
	/*时基单元初始化*/
	TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;				//定义结构体变量
	TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;     //时钟分频，选择不分频，此参数用于配置滤波器时钟，不影响时基单元功能
	TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up; //计数器模式，选择向上计数
	TIM_TimeBaseInitStructure.TIM_Period = 20000 - 1;				//计数周期，即ARR的值
	TIM_TimeBaseInitStructure.TIM_Prescaler = 84 - 1;				//预分频器，即PSC的值
	TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;            //重复计数器，高级定时器才会用到
	TIM_TimeBaseInit(TIM3, &TIM_TimeBaseInitStructure);             //将结构体变量交给TIM_TimeBaseInit，配置TIM3的时基单元
	TIM_TimeBaseInit(TIM4, &TIM_TimeBaseInitStructure);
	TIM_TimeBaseInitStructure.TIM_Prescaler = 168 - 1;
	TIM_TimeBaseInit(TIM9, &TIM_TimeBaseInitStructure);
	
	/*输出比较初始化*/ 
	TIM_OCInitTypeDef TIM_OCInitStructure;							//定义结构体变量
	TIM_OCStructInit(&TIM_OCInitStructure);                         //结构体初始化，若结构体没有完整赋值
	                                                                
	
	TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;               //输出比较模式，选择PWM模式1
	TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;       //输出极性，选择为高，若选择极性为低，则输出高低电平取反
	TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;   //输出使能
	TIM_OCInitStructure.TIM_Pulse = 0;								//初始的CCR值
	TIM_OC1Init(TIM3, &TIM_OCInitStructure);                        //将结构体变量交给TIM_OC2Init，配置TIM3的输出比较通道2
	TIM_OC2Init(TIM3, &TIM_OCInitStructure);						//将结构体变量交给TIM_OC3Init，配置TIM3的输出比较通道3
	TIM_OC3Init(TIM3, &TIM_OCInitStructure);
	TIM_OC4Init(TIM3, &TIM_OCInitStructure);
	
	TIM_OC1Init(TIM4, &TIM_OCInitStructure);                        
	TIM_OC2Init(TIM4, &TIM_OCInitStructure);						
	TIM_OC3Init(TIM4, &TIM_OCInitStructure);
	TIM_OC4Init(TIM4, &TIM_OCInitStructure);
	
	TIM_OC1Init(TIM9, &TIM_OCInitStructure);                        
	TIM_OC2Init(TIM9, &TIM_OCInitStructure);
	
	//使能预装寄存器
	TIM_OC1PreloadConfig(TIM3 ,TIM_OCPreload_Enable);
	TIM_OC2PreloadConfig(TIM3 ,TIM_OCPreload_Enable);
	TIM_OC3PreloadConfig(TIM3 ,TIM_OCPreload_Enable);
	TIM_OC4PreloadConfig(TIM3 ,TIM_OCPreload_Enable);
	TIM_OC1PreloadConfig(TIM4 ,TIM_OCPreload_Enable);
	TIM_OC2PreloadConfig(TIM4 ,TIM_OCPreload_Enable);
	TIM_OC3PreloadConfig(TIM4 ,TIM_OCPreload_Enable);
	TIM_OC4PreloadConfig(TIM4 ,TIM_OCPreload_Enable);
	TIM_OC1PreloadConfig(TIM9 ,TIM_OCPreload_Enable);
	TIM_OC2PreloadConfig(TIM9 ,TIM_OCPreload_Enable);
	
	/*TIM使能*/
	TIM_Cmd(TIM3, ENABLE);			//使能TIM3，定时器开始运行
	TIM_Cmd(TIM4, ENABLE);			//使能TIM4，定时器开始运行
	TIM_Cmd(TIM9, ENABLE);			//使能TIM9，定时器开始运行
}

