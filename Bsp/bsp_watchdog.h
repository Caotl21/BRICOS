#ifndef __BSP_WDG_H
#define __BSP_WDG_H

#include <stdint.h>

/**
 * @brief 看门狗模块底层驱动初始化
 * @param timeout_ms - 超时复位时间 (单位：毫秒 ms)
 * @note 1. 该函数会根据传入的超时时间参数配置独立看门狗 (IWDG)，并启动看门狗计数器。
 *       2. IWDG 的时钟源为内部低速时钟 LSI，频率约为 32kHz。通过设置预分频器和重载值来实现不同的超时时间。
 *       3. 例如，预分频器设置为 64 时，计数器时钟为 32kHz / 64 = 500Hz，即每 2ms 计一次数。要实现 1秒超时，重载值应设置为 500 (1000ms / 2ms)。函数内部会自动计算并限幅重载值，确保在 IWDG 的有效范围内。
 *       4. 调用此函数后，必须定期调用 bsp_wdg_feed() 来重载计数器，否则在超时时间到达后单片机会被复位。
 */
void bsp_wdg_init(uint16_t timeout_ms);

/**
 * @brief 喂狗 (重载计数器)
 * @note 该函数用于重载独立看门狗的计数器，防止单片机因超时而复位。
 */
void bsp_wdg_feed(void);

#endif // __BSP_WDG_H

