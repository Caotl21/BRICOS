#include "driver_power.h"
#include "bsp_adc.h"
#include "bsp_core.h"

/* =========================================
 * 内部全局变量
 * ========================================= */

// 存放底层 ADC+DMA 传输过来的原始数据缓存
// [0] 对应电压，[1] 对应电流
static uint16_t s_adc_raw_values[BSP_ADC_MAX];

/* =========================================
 * 驱动核心函数
 * ========================================= */

bool Power_Init(void)
{
    // 配置需要采集的通道枚举组 (顺序决定了 DMA 存入 s_adc_raw_values 的顺序)
    bsp_adc_ch_t ch_list[] = {
        BSP_ADC_VOLTAGE, 
        BSP_ADC_CURRENT
    };
    
    // 调用底层 BSP 接口启动 ADC 和 DMA
    // 参数2: 通道数量为 2
    return bsp_adc_init(ch_list, 2, s_adc_raw_values);
}

float Power_GetVoltage(void)
{
    // s_adc_raw_values[0] 对应 BSP_ADC_VOLTAGE
    // 满量程 4096 对应 3.3V 内部基准，分压系数为 11
    
    return ((float)s_adc_raw_values[0] / 4096.0f) * 3.3f * 11.0f;
}

float Power_GetCurrent(void)
{
    // s_adc_raw_values[1] 对应 BSP_ADC_CURRENT
    // 满量程 4096 对应 3.3V 内部基准
    
    float voltage_i = ((float)s_adc_raw_values[1] / 4096.0f) * 3.3f;
    
    // 电流解算公式：10 * (测量电压 - 2.5V 偏移)
    return 10.0f * (voltage_i);
}