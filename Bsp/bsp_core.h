#ifndef __BSP_CORE_H
#define __BSP_CORE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* =========================================
 * 硬件资源全局枚举字典 (资源更新后需要同步修改)
 * ========================================= */

/* --- 串口端口枚举 --- */
typedef enum {
    BSP_UART_IMU1 = 0,  // 对应 USART1: IMU1 数据接收
    BSP_UART_IMU2,      // 对应 USART2: IMU2 数据接收
    BSP_UART_OPI,       // 对应 USART3: 香橙派指令与数据交互
    BSP_UART_OTA,       // 对应 UART4:  OTA 固件升级
    BSP_UART_MAX        // 端口数量上限，用于数组越界保护
} bsp_uart_port_t;

/* --- PWM 通道枚举 --- */
typedef enum {
    BSP_PWM_THRUSTER_1 = 0, // 推进器 1-6
    BSP_PWM_THRUSTER_2,
    BSP_PWM_THRUSTER_3,
    BSP_PWM_THRUSTER_4,
    BSP_PWM_THRUSTER_5,
    BSP_PWM_THRUSTER_6,
    BSP_PWM_SERVO_1,        // 舵机 1-2
    BSP_PWM_SERVO_2,
    BSP_PWM_LIGHT_1,        // 探照灯 1-2
    BSP_PWM_LIGHT_2,
    BSP_PWM_MAX
} bsp_pwm_ch_t;

/* --- ADC 通道枚举 --- */
typedef enum {
    BSP_ADC_VOLTAGE = 0,    // 电池总电压
    BSP_ADC_CURRENT,        // 系统总电流
    BSP_ADC_MAX
} bsp_adc_ch_t;

/* --- 软件 I2C 总线枚举 --- */
typedef enum {
    BSP_I2C_MS5837 = 0,  // 对应 PB6/PB7 (如 MS5837 水压计)
    BSP_I2C_MAX
} bsp_i2c_bus_t;

typedef enum {
    BSP_GPIO_DHT11 = 0,
    // BSP_GPIO_LEAK_DET, // 预留：漏水检测传感器
    // BSP_GPIO_RELAY_1,  // 预留：外部设备供电继电器
    BSP_GPIO_MAX
}bsp_gpio_pin_t;



#endif // __BSP_CORE_H
