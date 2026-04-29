#include "driver_searchlight.h"
#include "bsp_pwm.h"
#include "sys_log.h"

// 设置单路探照灯亮度
void Driver_SearchLight_SetAngle(bsp_pwm_ch_t ch, uint8_t brightness) {
    if (ch < BSP_PWM_SERVO_1 || ch > BSP_PWM_SERVO_2) return;

    // 安全限幅
    if (brightness > 100) brightness = 100;

    uint16_t pulse_us;
    
    pulse_us = SEARCHLIGHT_PWM_MAX_LEFT + (uint16_t)(((uint32_t)(SEARCHLIGHT_PWM_MAX_RIGHT - SEARCHLIGHT_PWM_MAX_LEFT) * brightness) / 100);

    bsp_pwm_set_pulse_us(ch, pulse_us);
}