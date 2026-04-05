#include "driver_thruster.h"
#include "bsp_pwm.h"
#include "bsp_delay.h"
#include "sys_log.h"

// 电调ESC解锁：上电时必须发送 1500us 持续一段时间
void Driver_Thruster_Init(void){
    for(int i=0;i<THRUSTER_COUNT;i++){
        bsp_pwm_set_pulse_us((bsp_pwm_ch_t)(BSP_PWM_THRUSTER_1 + i), THRUSTER_PWM_STOP);
    }
    bsp_delay_ms(3000); // 等待 3000ms 确保电调解锁
}

// 设置单路推力
void Driver_Thruster_SetSpeed(bsp_pwm_ch_t ch, float force_percent) {
    if (ch < BSP_PWM_THRUSTER_1 || ch > BSP_PWM_THRUSTER_6) return;

    // 安全限幅
    if (force_percent > 100.0f) force_percent = 100.0f;
    if (force_percent < -100.0f) force_percent = -100.0f;

    // 根据正负计算对应的脉宽
    uint16_t pulse_us;
    if (force_percent >= 0.0f) {
        pulse_us = THRUSTER_PWM_STOP + (uint16_t)((THRUSTER_PWM_MAX_FWD - THRUSTER_PWM_STOP) * (force_percent / 100.0f));
    } else {
        pulse_us = THRUSTER_PWM_STOP - (uint16_t)((THRUSTER_PWM_MAX_REV - THRUSTER_PWM_STOP) * (force_percent / 100.0f));
    }

    // 设置 PWM 输出
    bsp_pwm_set_pulse_us(ch, pulse_us);
}

// 设置所有推进器为怠速（1500us）
void Driver_Thruster_Set_Idle(void) {
    for(int i=0;i<THRUSTER_COUNT;i++){
        bsp_pwm_set_pulse_us((bsp_pwm_ch_t)(BSP_PWM_THRUSTER_1 + i), THRUSTER_PWM_STOP);
    }
}
