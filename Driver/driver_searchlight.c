#include "driver_searchlight.h"

// 设置单路探照灯亮度
void Driver_SearchLight_SetAngle(bsp_pwm_ch_t ch, uint8_t brightness)
{
    if (ch < BSP_PWM_SEARCHLIGHT_1 || ch > BSP_PWM_SEARCHLIGHT_2) {
        return;
    }

    // 安全限幅
    if (brightness > 100U) {
        brightness = 100U;
    }

    bsp_pwm_set_duty(ch, (float)brightness);
}
