#include "bsp_adc.h"
#include "stm32f4xx.h"
#include "stm32f4xx_adc.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_dma.h"
#include "stm32f4xx_rcc.h"

/* ==========================================
 * 1. 定义 ADC 硬件信息字典 (数据驱动核心)
 * ========================================== */
typedef struct {
    GPIO_TypeDef* gpio_port;     // GPIO 端口
    uint16_t      gpio_pin;      // GPIO 引脚
    uint8_t       adc_channel;   // ADC 通道号
} adc_hw_info_t;

/* 实例化映射表，加 const 存入只读存储区，不占 RAM */
static const adc_hw_info_t s_adc_hw_info[BSP_ADC_MAX] = {
    [BSP_ADC_VOLTAGE] = {
        .gpio_port   = GPIOF,
        .gpio_pin    = GPIO_Pin_9,
        .adc_channel = ADC_Channel_7
    },
    [BSP_ADC_CURRENT] = {
        .gpio_port   = GPIOF,
        .gpio_pin    = GPIO_Pin_10,
        .adc_channel = ADC_Channel_8
    }
};

/* ==========================================
 * 2. ADC 与 DMA 底层初始化
 * ========================================== */
bool bsp_adc_init(bsp_adc_ch_t *ch_list, uint8_t ch_num, uint16_t *val_array)
{
    // 参数校验
    if (ch_list == NULL || val_array == NULL || ch_num == 0 || ch_num > BSP_ADC_MAX) {
        return false; 
    }

    GPIO_InitTypeDef GPIO_InitStructure;
    DMA_InitTypeDef DMA_InitStructure;
    ADC_CommonInitTypeDef ADC_CommonInitStructure;
    ADC_InitTypeDef ADC_InitStructure;

    /* 开启时钟 (如果以后通道跨越了不同ADC或GPIO，这里也可做到表中动态开启) */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC3, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2 | RCC_AHB1Periph_GPIOF, ENABLE);
    
    /*----------------GPIO 与 ADC 通道动态解析配置----------------*/
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;

    for (uint8_t i = 0; i < ch_num; i++) {
        bsp_adc_ch_t ch = ch_list[i];
        
        // 安全校验：防止传入未知的枚举导致查表越界
        if (ch >= BSP_ADC_MAX) {
            return false;
        }

        // 获取当前通道的硬件配置指针
        const adc_hw_info_t *hw = &s_adc_hw_info[ch];

        // 1. 动态配置具体的 GPIO 引脚
        GPIO_InitStructure.GPIO_Pin = hw->gpio_pin;
        GPIO_Init(hw->gpio_port, &GPIO_InitStructure);
        
        // 2. 动态配置 ADC 转换序列 (rank为 i+1)
        ADC_RegularChannelConfig(ADC3, hw->adc_channel, i + 1, ADC_SampleTime_3Cycles);
    }
    
    /*----------------DMA初始化---------------*/
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&ADC3->DR;
    // 直接绑定到上层传入的数组地址
    DMA_InitStructure.DMA_Memory0BaseAddr = (uint32_t)val_array; 
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralToMemory;
    DMA_InitStructure.DMA_BufferSize = ch_num; // 缓冲大小动态适配通道数量
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable; // 内存地址递增开启，存入数组
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;
    DMA_InitStructure.DMA_Priority = DMA_Priority_High;
    DMA_InitStructure.DMA_FIFOMode = DMA_FIFOMode_Disable;
    DMA_InitStructure.DMA_FIFOThreshold = DMA_FIFOThreshold_HalfFull;
    DMA_InitStructure.DMA_MemoryBurst = DMA_MemoryBurst_Single;
    DMA_InitStructure.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;
    DMA_InitStructure.DMA_Channel = DMA_Channel_2;
    
    DMA_Init(DMA2_Stream0, &DMA_InitStructure);
    DMA_Cmd(DMA2_Stream0, ENABLE);
    
    /*-----------ADC Common 及 ADC 初始化----------*/
    ADC_CommonInitStructure.ADC_Mode = ADC_Mode_Independent;
    ADC_CommonInitStructure.ADC_Prescaler = ADC_Prescaler_Div4;
    ADC_CommonInitStructure.ADC_DMAAccessMode = ADC_DMAAccessMode_Disabled;
    ADC_CommonInitStructure.ADC_TwoSamplingDelay = ADC_TwoSamplingDelay_20Cycles;
    ADC_CommonInit(&ADC_CommonInitStructure);
    
    ADC_StructInit(&ADC_InitStructure);
    ADC_InitStructure.ADC_Resolution = ADC_Resolution_12b;
    ADC_InitStructure.ADC_ScanConvMode = ENABLE; // 开启扫描模式
    ADC_InitStructure.ADC_ContinuousConvMode = ENABLE; // 连续转换
    ADC_InitStructure.ADC_ExternalTrigConvEdge = ADC_ExternalTrigConvEdge_None;
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_T1_CC1;
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfConversion = ch_num; // 转换总数动态适配
    ADC_Init(ADC3, &ADC_InitStructure);

    ADC_DMARequestAfterLastTransferCmd(ADC3, ENABLE);                       
    ADC_DMACmd(ADC3, ENABLE);
    ADC_Cmd(ADC3, ENABLE);
    
    /* ADC软件触发 */
    ADC_SoftwareStartConv(ADC3);
    
    return true; // 返回布尔值 true
}

