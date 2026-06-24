#ifndef __BSP_PWM_H
#define __BSP_PWM_H

#include "bsp_core.h"

// 时钟基准：
// - 系统主频 168MHz
// - APB1 定时器时钟 84MHz
// - APB2 定时器时钟 168MHz
// 为了让普通 PWM 的 CCR 直接按 us 理解，这里统一把计数频率配置到 1MHz：
// - APB1 定时器：PSC = 84 - 1
// - APB2 定时器：PSC = 168 - 1
#define PSC_APB1_1MHZ    (84 - 1)
#define PSC_APB2_1MHZ    (168 - 1)

// 用于 WS2812 这类波形型 PWM 输出：
// - APB1 定时器可直接以 84MHz 计数
// - APB2 上的 TIM1/TIM8 需先二分频到 84MHz
// - 84MHz / 800kHz = 105 count，对应 1.25us 一个 bit 周期
#define PSC_APB1_WS2812  (0)
#define PSC_APB2_WS2812  (2 - 1)

#define ARR_50HZ         (20000 - 1)  // 20ms 周期，用于推进器和舵机
#define ARR_1KHZ         (1000 - 1)   // 1ms 周期，用于探照灯
#define ARR_WS2812       (105 - 1)    // 84MHz / 800kHz = 105 counts => 1.25us

/**
 * @brief  初始化 PWM 模块。
 * @param  init_pulse_us - 普通 PWM 通道的默认脉宽，单位 us。
 * @return true 表示初始化成功，false 表示初始化失败。
 * @note   1. 每个通道的硬件资源都在 bsp_pwm.c 中的 pwm_hw_info 字典里配置。
 *         2. bsp_pwm_init() 会按字典内容完成所有 PWM 通道的 GPIO、定时器和输出配置。
 */
bool bsp_pwm_init(uint16_t init_pulse_us);

/**
 * @brief  设置指定 PWM 通道的高电平脉宽。
 * @param  ch - PWM 通道枚举（如 BSP_PWM_THRUSTER_1）。
 * @param  pulse_us - 目标脉宽。
 * @note   对于普通 1MHz 基准 PWM，该值可直接理解为 us；
 *         对于波形型 PWM，上层可直接把它当作要写入 CCR 的原始计数值。
 */
void bsp_pwm_set_pulse_us(bsp_pwm_ch_t ch, uint16_t pulse_us);

/**
 * @brief  获取指定 PWM 通道当前的 CCR 值。
 * @param  ch - PWM 通道枚举。
 * @return 当前 CCR 的数值。
 */
uint16_t bsp_pwm_get_pulse_us(bsp_pwm_ch_t ch);

/**
 * @brief  设置指定 PWM 通道的占空比（0% ~ 100%）。
 * @param  ch - PWM 通道枚举（如 BSP_PWM_SEARCHLIGHT_1）。
 * @param  duty - 占空比百分比，范围 0.0f ~ 100.0f。
 * @note   内部会结合该通道的 period 自动换算成 CCR，并调用 bsp_pwm_set_pulse_us()。
 */
void bsp_pwm_set_duty(bsp_pwm_ch_t ch, float duty);

/**
 * @brief  查询指定 PWM 通道是否具备 DMA 波形发送能力。
 * @param  ch - PWM 通道枚举。
 * @return true 表示支持，false 表示不支持。
 * @note   这里只表示 BSP 已经为该通道补齐了 PWM + DMA 波形发送所需的底层映射。
 */
bool bsp_pwm_supports_dma_waveform(bsp_pwm_ch_t ch);

/**
 * @brief  启动一次基于 DMA 的 PWM 波形发送。
 * @param  ch - PWM 通道枚举。
 * @param  ccr_buf - 波形缓冲区，每个元素对应一个 PWM 周期要装载到 CCR 的值。
 * @param  len - 缓冲区元素个数，单位为 uint16_t。
 * @return true 表示启动成功，false 表示启动失败。
 * @note   1. ccr_buf 的存储由上层负责，BSP 只借用，不持有。
 *         2. 在 DMA 完成之前，ccr_buf 必须保持有效。
 *         3. BSP 不关心缓冲区里的协议语义，只负责按时序把 CCR 波形送出去。
 */
bool bsp_pwm_start_dma_waveform(bsp_pwm_ch_t ch, const uint16_t *ccr_buf, uint16_t len);

/**
 * @brief  中止指定 PWM 通道当前的 DMA 波形发送，并强制恢复为空闲状态。
 * @param  ch - PWM 通道枚举。
 */
void bsp_pwm_abort_dma_waveform(bsp_pwm_ch_t ch);

/**
 * @brief  查询指定 PWM 通道当前是否处于 DMA 波形发送中。
 * @param  ch - PWM 通道枚举。
 * @return true 表示忙，false 表示空闲。
 */
bool bsp_pwm_is_dma_waveform_busy(bsp_pwm_ch_t ch);

/**
 * @brief  PWM 波形发送 DMA 中断收尾处理。
 * @param  ch - PWM 通道枚举。
 * @note   由对应的 DMAx_Streamy_IRQHandler 调用，用于处理发送完成或发送错误收尾。
 */
void bsp_pwm_dma_waveform_irq_handler(bsp_pwm_ch_t ch);

#endif // __BSP_PWM_H
