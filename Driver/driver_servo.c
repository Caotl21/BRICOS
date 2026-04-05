#include "driver_servo.h"
#include "bsp_pwm.h"
#include "sys_log.h"

// 设置单路舵机角度
void Driver_Servo_SetAngle(bsp_pwm_ch_t ch, uint8_t angle) {
    if (ch < BSP_PWM_SERVO_1 || ch > BSP_PWM_SERVO_2) return;

    // 安全限幅
    if (angle > 180) angle = 180;

    uint16_t pulse_us;
    
    pulse_us = SERVO_PWM_MAX_LEFT + (uint16_t)(((uint32_t)(SERVO_PWM_MAX_RIGHT - SERVO_PWM_MAX_LEFT) * angle) / 180);

    bsp_pwm_set_pulse_us(ch, pulse_us);
}

