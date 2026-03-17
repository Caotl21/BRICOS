#ifndef __DRIVER_POWER_H
#define __DRIVER_POWER_H

#include "bsp_core.h"

/**
 * @brief  电源检测模块初始化
 * @note   内部会调用 BSP 层初始化 ADC 和 DMA，并将缓冲区绑定。
 * @retval true  初始化成功
 * @retval false 初始化失败
 */
bool Power_Init(void);

/**
 * @brief  获取系统总电压
 * @note   硬件分压比为 11
 * @retval 当前电压值 (单位: V)
 */
float Power_GetVoltage(void);

/**
 * @brief  获取系统总电流
 * @note   (大洋科技提供计算公式) 偏移中值电压为 2.5V
 * @retval 当前电流值 (单位: A)
 */
float Power_GetCurrent(void);

#endif // __DRIVER_POWER_H