#ifndef _BSP_ADC_H_
#define _BSP_ADC_H_

#include "bsp_core.h"
#include <stdbool.h>

typedef enum {
    BSP_ADC_SAMPLE_3CYC = 0,
    BSP_ADC_SAMPLE_15CYC,
    BSP_ADC_SAMPLE_28CYC,
    BSP_ADC_SAMPLE_56CYC,
    BSP_ADC_SAMPLE_84CYC,
    BSP_ADC_SAMPLE_112CYC,
    BSP_ADC_SAMPLE_144CYC,
    BSP_ADC_SAMPLE_480CYC
} bsp_adc_sample_t;

typedef struct {
    bsp_adc_sample_t sample_time;     // ADC_SampleTime_xCycles
    bool             scan_enable;         // 扫描模式
    bool             continuous_enable;   // 连续转换
    bool             dma_enable;          // 是否启用 DMA
} bsp_adc_config_t;

/**
 * @brief  ADC与DMA底层初始化 (支持多通道自由组合)
 * @param  ch_list   需要初始化的通道枚举数组
 * @param  ch_num    初始化的通道总数
 * @param  val_array 接收ADC数据的数组指针 (长度必须 >= ch_num)
 * @param  cfg       ADC配置参数 (采样时间、扫描模式、连续转换、DMA等)
 * @return 1:成功 0:失败
 */
bool bsp_adc_init(bsp_adc_ch_t *ch_list, uint8_t ch_num, uint16_t *val_array, const bsp_adc_config_t *cfg);


#endif /* _BSP_ADC_H_ */
