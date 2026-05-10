#ifndef __SEARCHLIGHT_DRIVER_H
#define __SEARCHLIGHT_DRIVER_H

#include "bsp_pwm.h"

#include <stdint.h>

#define SearchLight_COUNT 2

/**
 * @brief 设置单路探照灯亮度
 * @param ch - 探照灯对应的 PWM 通道枚举
 * @param brightness - 亮度百分比，范围 0~100
 */
void Driver_SearchLight_SetAngle(bsp_pwm_ch_t ch, uint8_t brightness);

#endif // __SEARCHLIGHT_DRIVER_H
