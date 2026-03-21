#include "bsp_adc.h"
#include "bsp_delay.h"
#include "stm32f4xx.h"
#include "stm32f4xx_adc.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_dma.h"
#include "stm32f4xx_rcc.h"

/* ==========================================
 * 定义 ADC 通道信息字典
 * adc_hw_info_t 结构体包含了每个 ADC 通道对应的 ADC 外设、GPIO 端口、GPIO 引脚、ADC 通道号以及是否需要配置 GPIO 的标志。
 * adc_core_info_t 结构体包含了每个 ADC 外设对应的 ADC 时钟、DMA 时钟、DMA 流和 DMA 通道等核心配置信息。
 * ========================================== */
typedef struct {
    ADC_TypeDef* adc;
    GPIO_TypeDef* gpio_port;
    uint16_t gpio_pin;
    uint8_t adc_channel;
    uint8_t needs_gpio;
    uint8_t need_temp;
} adc_hw_info_t;

typedef struct {
    ADC_TypeDef* adc;
    uint32_t adc_rcc;
    uint32_t dma_rcc;
    DMA_Stream_TypeDef* dma_stream;
    uint32_t dma_channel;
} adc_core_info_t;

static const adc_hw_info_t s_adc_hw_info[BSP_ADC_MAX] = {
    [BSP_ADC_VOLTAGE] = {
        .adc = ADC3,
        .gpio_port = GPIOF,
        .gpio_pin = GPIO_Pin_9,
        .adc_channel = ADC_Channel_7,
        .needs_gpio = 1,
        .need_temp = 0
    },
    [BSP_ADC_CURRENT] = {
        .adc = ADC3,
        .gpio_port = GPIOF,
        .gpio_pin = GPIO_Pin_10,
        .adc_channel = ADC_Channel_8,
        .needs_gpio = 1,
        .need_temp = 0
    },
    [BSP_ADC_CHIPTEMP] = {
        .adc = ADC1,
        .gpio_port = 0,
        .gpio_pin = 0,
        .adc_channel = ADC_Channel_16,
        .needs_gpio = 0,
        .need_temp = 1
    }
};

static const adc_core_info_t s_adc_core_info[] = {
    { ADC1, RCC_APB2Periph_ADC1, RCC_AHB1Periph_DMA2, DMA2_Stream0, DMA_Channel_0 },
    { ADC3, RCC_APB2Periph_ADC3, RCC_AHB1Periph_DMA2, DMA2_Stream0, DMA_Channel_2 }
};

static const bsp_adc_config_t s_adc_default_cfg = {
    .sample_time = ADC_SampleTime_3Cycles,
    .scan_enable = true,
    .continuous_enable = true,
    .dma_enable = true
};

/* ==========================================
 * ADC 与 DMA 底层初始化
 * ========================================== */
static uint32_t bsp_adc_map_sample_time(bsp_adc_sample_t t)
{
    switch (t) {
        case BSP_ADC_SAMPLE_3CYC:   return ADC_SampleTime_3Cycles;
        case BSP_ADC_SAMPLE_15CYC:  return ADC_SampleTime_15Cycles;
        case BSP_ADC_SAMPLE_28CYC:  return ADC_SampleTime_28Cycles;
        case BSP_ADC_SAMPLE_56CYC:  return ADC_SampleTime_56Cycles;
        case BSP_ADC_SAMPLE_84CYC:  return ADC_SampleTime_84Cycles;
        case BSP_ADC_SAMPLE_112CYC: return ADC_SampleTime_112Cycles;
        case BSP_ADC_SAMPLE_144CYC: return ADC_SampleTime_144Cycles;
        case BSP_ADC_SAMPLE_480CYC: return ADC_SampleTime_480Cycles;
        default:                    return ADC_SampleTime_3Cycles;
    }
}

bool bsp_adc_init(bsp_adc_ch_t *ch_list, uint8_t ch_num, uint16_t *val_array, const bsp_adc_config_t *cfg)
{
    if (ch_list == NULL || ch_num == 0 || ch_num > BSP_ADC_MAX) return false;
    if (cfg->dma_enable && val_array == NULL) return false;

    const bsp_adc_config_t *use_cfg = (cfg != NULL) ? cfg : &s_adc_default_cfg;

    const adc_hw_info_t *hw0 = &s_adc_hw_info[ch_list[0]];
    ADC_TypeDef *adc = hw0->adc;

    // 确保所有通道属于同一个 ADC
    for (uint8_t i = 0; i < ch_num; i++) {
        if (s_adc_hw_info[ch_list[i]].adc != adc) {
            return false;
        }
    }

    // 找到对应 ADC 的 core 配置
    const adc_core_info_t *core = 0;
    for (uint32_t i = 0; i < (sizeof(s_adc_core_info)/sizeof(s_adc_core_info[0])); i++) {
        if (s_adc_core_info[i].adc == adc) {
            core = &s_adc_core_info[i];
            break;
        }
    }
    if (core == 0) return false;

    // 开时钟
    RCC_APB2PeriphClockCmd(core->adc_rcc, ENABLE);
    if (core->dma_stream != NULL) {
        RCC_AHB1PeriphClockCmd(core->dma_rcc, ENABLE);
    }

    if(hw0->need_temp)  ADC_TempSensorVrefintCmd(ENABLE);
    bsp_delay_us(10);



    // GPIO 只配 needs_gpio 的通道
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;

    for (uint8_t i = 0; i < ch_num; i++) {
        const adc_hw_info_t *hw = &s_adc_hw_info[ch_list[i]];
        if (hw->needs_gpio) {
            GPIO_InitStructure.GPIO_Pin = hw->gpio_pin;
            GPIO_Init(hw->gpio_port, &GPIO_InitStructure);
        }
        ADC_RegularChannelConfig(adc, hw->adc_channel, i + 1, bsp_adc_map_sample_time(use_cfg->sample_time));
    }

    // DMA（如果需要）
    if (core->dma_stream != NULL && use_cfg->dma_enable) {
        DMA_InitTypeDef DMA_InitStructure;
        DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&adc->DR;
        DMA_InitStructure.DMA_Memory0BaseAddr = (uint32_t)val_array;
        DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralToMemory;
        DMA_InitStructure.DMA_BufferSize = ch_num;
        DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
        DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
        DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
        DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
        DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;
        DMA_InitStructure.DMA_Priority = DMA_Priority_High;
        DMA_InitStructure.DMA_FIFOMode = DMA_FIFOMode_Disable;
        DMA_InitStructure.DMA_FIFOThreshold = DMA_FIFOThreshold_HalfFull;
        DMA_InitStructure.DMA_MemoryBurst = DMA_MemoryBurst_Single;
        DMA_InitStructure.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;
        DMA_InitStructure.DMA_Channel = core->dma_channel;

        DMA_Init(core->dma_stream, &DMA_InitStructure);
        DMA_Cmd(core->dma_stream, ENABLE);
    }

    // ADC init
    ADC_CommonInitTypeDef ADC_CommonInitStructure;
    ADC_InitTypeDef ADC_InitStructure;
    ADC_CommonInitStructure.ADC_Mode = ADC_Mode_Independent;
    ADC_CommonInitStructure.ADC_Prescaler = ADC_Prescaler_Div4;
    ADC_CommonInitStructure.ADC_DMAAccessMode = ADC_DMAAccessMode_Disabled;
    ADC_CommonInitStructure.ADC_TwoSamplingDelay = ADC_TwoSamplingDelay_20Cycles;
    ADC_CommonInit(&ADC_CommonInitStructure);

    ADC_StructInit(&ADC_InitStructure);
    ADC_InitStructure.ADC_Resolution = ADC_Resolution_12b;
    ADC_InitStructure.ADC_ScanConvMode = use_cfg->scan_enable ? ENABLE : DISABLE;
    ADC_InitStructure.ADC_ContinuousConvMode = use_cfg->continuous_enable ? ENABLE : DISABLE;
    ADC_InitStructure.ADC_ExternalTrigConvEdge = ADC_ExternalTrigConvEdge_None;
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfConversion = ch_num;
    ADC_Init(adc, &ADC_InitStructure);

    if (core->dma_stream != NULL && use_cfg->dma_enable) {
        ADC_DMARequestAfterLastTransferCmd(adc, ENABLE);
        ADC_DMACmd(adc, ENABLE);
    }

    ADC_Cmd(adc, ENABLE);
    ADC_SoftwareStartConv(adc);

    return true;
}

uint16_t bsp_adc_read_raw(bsp_adc_ch_t ch)
{
    if (ch >= BSP_ADC_MAX) {
        return 0;
    }

    const adc_hw_info_t *hw = &s_adc_hw_info[ch];
    ADC_TypeDef *adc = hw->adc;

    ADC_RegularChannelConfig(adc, hw->adc_channel, 1, bsp_adc_map_sample_time(BSP_ADC_SAMPLE_480CYC));

    ADC_SoftwareStartConv(adc);
    while (ADC_GetFlagStatus(adc, ADC_FLAG_EOC) == RESET) {
    }

    return ADC_GetConversionValue(adc);
}

