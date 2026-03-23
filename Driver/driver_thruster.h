#ifndef __THRUSTER_DRIVER_H
#define __THRUSTER_DRIVER_H

#include "bsp_pwm.h"
#include <stdint.h>

#define THRUSTER_COUNT   6

// 推进器 PWM 物理参数定义
#define THRUSTER_PWM_STOP    1500
#define THRUSTER_PWM_MAX_FWD 1900
#define THRUSTER_PWM_MAX_REV 1100

// 函数声明
void Driver_Thruster_Init(void);
void Driver_Thruster_SetSpeed(bsp_pwm_ch_t ch, float force_percent);
void Driver_Thruster_Set_Idle(void); 

#endif // __THRUSTER_DRIVER_H

