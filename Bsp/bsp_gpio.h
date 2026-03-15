#ifndef __BSP_GPIO_H
#define __BSP_GPIO_H

#include "bsp_core.h" 
#include <stdbool.h>

/**
 * @brief GPIO 模块底层驱动初始化
 * @note 该函数会根据 bsp_core.h 中定义的 bsp_gpio_pin_t 枚举，自动启用对应 GPIO 引脚的时钟，为后续的 GPIO 配置做好准备。
 */
bool bsp_gpio_init(void);

/**
 * @brief 设置 GPIO 引脚的输入输出方向
 * @param pin - GPIO 引脚枚举 (如 BSP_GPIO_DHT11)
 * @param is_output - true 设置为输出模式，false 设置为输入模式
 * @note 该函数会根据传入的引脚枚举参数，在 bsp_gpio.c 中查找对应的硬件信息，并配置 GPIO 的模式、输出类型和上拉/下拉电阻。输入模式默认启用上拉电阻，输出模式默认推挽输出。
 */
void bsp_gpio_set_direction(bsp_gpio_pin_t pin, bool is_output);

/**
 * @brief 写入 GPIO 引脚电平
 * @param pin - GPIO 引脚枚举 (如 BSP_GPIO_DHT11)
 * @param state - true 设置为高电平，false 设置为低电平
 * @note 该函数会直接操作 GPIO 的 BSRR 寄存器来设置引脚电平，具有极低的调用开销，适合频繁更新输出状态的场景。
 */
void bsp_gpio_write(bsp_gpio_pin_t pin, bool state);

/**
 * @brief 读取 GPIO 引脚电平
 * @param pin - GPIO 引脚枚举 (如 BSP_GPIO_DHT11)
 * @return true 表示高电平，false 表示低电平
 * @note 该函数会直接读取 GPIO 的 IDR 寄存器来获取引脚电平状态。
 */
bool bsp_gpio_read(bsp_gpio_pin_t pin);

#endif