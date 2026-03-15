#ifndef __THRUSTER_DRIVER_H
#define __THRUSTER_DRIVER_H

#include "bsp_pwm.h"
#include <stdint.h>

#define THRUSTER_COUNT   6

// 推进器 PWM 物理参数定义
#define THRUSTER_PWM_STOP    1500
#define THRUSTER_PWM_MAX_FWD 2000
#define THRUSTER_PWM_MAX_REV 1000

// 函数声明
void Thruster_Init(void);
void Thruster_SetSpeed(bsp_pwm_ch_t ch, float force_percent);

#endif // __THRUSTER_DRIVER_H