#ifndef __SEARCHLIGHT_DRIVER_H
#define __SEARCHLIGHT_DRIVER_H

#include "bsp_pwm.h"
#include <stdint.h>

#define SearchLight_COUNT   2

// 探照灯 PWM 物理参数定义
#define SEARCHLIGHT_PWM_STOP       1500
#define SEARCHLIGHT_PWM_MAX_LEFT   500
#define SEARCHLIGHT_PWM_MAX_RIGHT  2500

// 函数声明
void Driver_SearchLight_SetAngle(bsp_pwm_ch_t ch, uint8_t angle);


#endif // __SEARCHLIGHT_DRIVER_H

