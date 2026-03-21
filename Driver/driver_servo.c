#include "driver_servo.h"
#include "bsp_pwm.h"

// 设置单路舵机角度
void Driver_Servo_SetAngle(bsp_pwm_ch_t ch, uint8_t angle){
    if (ch < BSP_PWM_SERVO_1 || ch > BSP_PWM_SERVO_2) return;

    // 安全限幅
    if (angle > 180) angle = 180;
    if (angle < 0) angle = 0;

    // 根据正负计算对应的脉宽
    uint16_t pulse_us;
    if (angle >= 0) {
        pulse_us = SERVO_PWM_STOP + (uint16_t)((SERVO_PWM_MAX_LEFT - SERVO_PWM_STOP) * (angle / 180));
    } else {
        pulse_us = SERVO_PWM_STOP + (uint16_t)((SERVO_PWM_MAX_RIGHT - SERVO_PWM_STOP) * (angle / 180));
    }

    // 设置 PWM 输出
    bsp_pwm_set_pulse_us(ch, pulse_us);
}

