#ifndef __BSP_DELAY_H
#define __BSP_DELAY_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief DWT (Data Watchpoint and Trace) 基于 CPU 时钟周期的精确延时函数
 * @note 这个延时函数的精度非常高，适合对时序要求极为严格的外设（如 DHT11、软件 I2C 等）。但它是阻塞式的，且依赖于 CPU 主频，因此在使用前必须调用 bsp_delay_init() 来初始化 DWT。
 * @note 由于它是基于 CPU 时钟周期的，所以在系统频率较高时，短时间的延时可能会因为函数调用和计算的开销而不够准确。对于长时间的延时，建议使用硬件定时器或 RTOS 的延时机制。
 */
bool bsp_delay_init(void);

/**
 * @brief 微秒级阻塞延时
 * @param us 需要延时的微秒数
 * @note 这个函数会根据当前系统主频计算需要的 CPU 时钟周期数，并通过 DWT 的周期计数器来实现精确的延时。请确保在调用这个函数之前已经调用了 bsp_delay_init() 来初始化 DWT。
 */
void bsp_delay_us(uint32_t us);

/**
 * @brief 毫秒级阻塞延时
 * @param ms 需要延时的毫秒数
 * @note 这个函数会调用 bsp_delay_us() 函数来实现毫秒级的延时。请确保在调用这个函数之前已经调用了 bsp_delay_init() 来初始化 DWT。
 */
void bsp_delay_ms(uint32_t ms);

#endif