#ifndef __BSP_PWM_H
#define __BSP_PWM_H

#include "bsp_core.h" // 包含所有的枚举定义

/**
 * @brief  PWM 模块底层驱动
 * @note 1. 该模块使用数据驱动设计，所有硬件相关配置都集中在 bsp_pwm.c 的 pwm_hw_info 数组中，方便维护和扩展。
 *       2. bsp_pwm_init() 会根据 pwm_hw_info 数组自动初始化所有配置的 PWM 通道，无需修改初始化代码。
 */
void bsp_pwm_init(void);

/**
 * @brief 设置指定 PWM 通道的高电平脉宽时间 (单位：微秒 us)
 * @param ch - PWM 通道枚举 (如 BSP_PWM_THRUSTER_1)
 * @param pulse_us - 高电平微秒数 (如 1500 代表中位停止)
 * @note 该函数直接操作定时器的 CCR 寄存器，具有极低的调用开销，适合频繁更新 PWM 输出的场景。
 */
void bsp_pwm_set_pulse_us(bsp_pwm_ch_t ch, uint16_t pulse_us);

/**
 * @brief 设置指定 PWM 通道的占空比 (单位：百分比 %)
 * @param ch - PWM 通道枚举 (如 BSP_PWM_LIGHT_1)
 * @param duty - 占空比 0.0f ~ 100.0f
 * @note 该函数会将占空比转换为对应的脉宽微秒数，并调用 bsp_pwm_set_pulse_us() 来设置输出。适合以占空比为控制目标的场景。
 */
void bsp_pwm_set_duty(bsp_pwm_ch_t ch, float duty);

#endif // __BSP_PWM_H