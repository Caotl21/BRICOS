#ifndef __SERVO_DRIVER_H
#define __SERVO_DRIVER_H

#include "bsp_pwm.h"
#include <stdint.h>

#define SERVO_COUNT   2

// 舵机 PWM 物理参数定义
#define SERVO_PWM_STOP    1500
#define SERVO_PWM_MAX_LEFT 1000
#define SERVO_PWM_MAX_RIGHT 2000

// 函数声明
void Driver_Servo_SetAngle(bsp_pwm_ch_t ch, uint8_t angle);


#endif // __SERVO_DRIVER_H

