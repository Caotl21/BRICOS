#ifndef _BSP_ADC_H_
#define _BSP_ADC_H_

#include "bsp_core.h"
/**
 * @brief  ADC与DMA底层初始化 (支持多通道自由组合)
 * @param  ch_list   需要初始化的通道枚举数组
 * @param  ch_num    初始化的通道总数
 * @param  val_array 接收ADC数据的数组指针 (长度必须 >= ch_num)
 * @return 1:成功 0:失败
 */
bool bsp_adc_init(bsp_adc_ch_t *ch_list, uint8_t ch_num, uint16_t *val_array);


#endif /* _BSP_ADC_H_ */
