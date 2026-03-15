#ifndef __BSP_PWM_H
#define __BSP_PWM_H

#include "bsp_core.h" // 包含所有的枚举定义

/* * 假设：系统时钟 168MHz，APB1 定时器频率 84MHz，APB2 定时器频率 168MHz。
 * 为了得到 1MHz 的计数频率 (1us 分辨率)：
 * APB1 定时器 PSC = 84 - 1
 * APB2 定时器 PSC = 168 - 1
 */
#define PSC_APB1_1MHZ  (84 - 1)
#define PSC_APB2_1MHZ  (168 - 1)

#define ARR_50HZ       (20000 - 1)  // 20ms 周期，用于推进器和舵机
#define ARR_1KHZ       (1000 - 1)   // 1ms 周期，用于探照灯

/* 
 * 定义 PWM 配置结构体，包含预分频器和周期等参数
 * 该结构体可以根据需要扩展，例如添加死区时间、PWM 模式等参数
 */
typedef struct {
    uint16_t init_pulse_us;  // 初始脉宽 (单位：微秒 us)，默认为 0
    uint16_t max_pulse_us;   // 最大脉宽 (单位：微秒 us)，用于占空比计算的上限
    uint16_t min_pulse_us;   // 最小脉宽 (单位：微秒 us)，用于占空比计算的下限
} bsp_pwm_config_t;

/**
 * @brief  PWM 模块底层驱动
 * @param  ch - PWM 通道枚举 (如 BSP_PWM_THRUSTER_1)
 * @param  config - PWM 配置结构体指针，包含初始脉宽、最大脉宽、最小脉宽等参数
 * @return true 初始化成功，false 初始化失败（如通道无效）
 * @note 1. 该模块使用数据驱动设计，所有硬件相关配置都集中在 bsp_pwm.c 的 pwm_hw_info 数组中，方便维护和扩展。
 *       2. bsp_pwm_init() 会根据 pwm_hw_info 数组自动初始化所有配置的 PWM 通道，无需修改初始化代码。
 */
bool bsp_pwm_init(bsp_pwm_ch_t ch, bsp_pwm_config_t *config);

/**
 * @brief 设置指定 PWM 通道的高电平脉宽时间 (单位：微秒 us)
 * @param ch - PWM 通道枚举 (如 BSP_PWM_THRUSTER_1)
 * @param pulse_us - 高电平微秒数 (如 1500 代表中位停止)
 * @note 该函数直接操作定时器的 CCR 寄存器，具有极低的调用开销，适合频繁更新 PWM 输出的场景。
 */
void bsp_pwm_set_pulse_us(bsp_pwm_ch_t ch, uint16_t pulse_us, bsp_pwm_config_t *config); 

/**
 * @brief 设置指定 PWM 通道的占空比 (单位：百分比 %)
 * @param ch - PWM 通道枚举 (如 BSP_PWM_LIGHT_1)
 * @param duty - 占空比 0.0f ~ 100.0f
 * @note 该函数会将占空比转换为对应的脉宽微秒数，并调用 bsp_pwm_set_pulse_us() 来设置输出。适合以占空比为控制目标的场景。
 */
void bsp_pwm_set_duty(bsp_pwm_ch_t ch, float duty, bsp_pwm_config_t *config);

#endif // __BSP_PWM_H