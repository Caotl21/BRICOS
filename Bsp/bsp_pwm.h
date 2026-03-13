#ifndef __BSP_PWM_H
#define __BSP_PWM_H

#include "bsp_core.h" // 包含所有的枚举定义

void bsp_pwm_init(void);

void bsp_pwm_set_pulse_us(bsp_pwm_ch_t ch, uint16_t pulse_us);

void bsp_pwm_set_duty(bsp_pwm_ch_t ch, float duty);

#endif // __BSP_PWM_H