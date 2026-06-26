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
    BSP_UART_IMU1 = 0,  // USART1: IMU1 data receive, IM948
    BSP_UART_IMU2,      // USART2: IMU2 data receive, JY901
    BSP_UART_IMU3,      // USART6: IMU3 data receive, IM948_2
    BSP_UART_OPI_RT,    // USART3: realtime communication
    BSP_UART_OPI_NRT,   // UART4: non-realtime communication
    BSP_UART_DEBUG,     // UART5: Shell/Log debug port
    BSP_UART_MAX
} bsp_uart_port_t;

/* --- PWM 通道枚举 --- */
typedef enum {
    BSP_PWM_THRUSTER_1 = 0, // 推进器 1-6
    BSP_PWM_THRUSTER_2,
    BSP_PWM_THRUSTER_3,
    BSP_PWM_THRUSTER_4,
    BSP_PWM_THRUSTER_5,
    BSP_PWM_THRUSTER_6,
    BSP_PWM_THRUSTER_7,
    BSP_PWM_THRUSTER_8,
    BSP_PWM_SERVO_1,        // 舵机 1-2
    BSP_PWM_SERVO_2,
    BSP_PWM_LED_1,          // ws2812 RGB LED 1-2
    BSP_PWM_LED_2,
    BSP_PWM_SEARCHLIGHT_1,        // 探照灯 1-2
    BSP_PWM_SEARCHLIGHT_2,
    BSP_PWM_MAX
} bsp_pwm_ch_t;

/* --- ADC 通道枚举 --- */
typedef enum {
    BSP_ADC_VOLTAGE = 0,    // 电池总电压
    BSP_ADC_CURRENT,        // 系统总电流
    BSP_ADC_CHIPTEMP,       // 芯片温度
    BSP_ADC_MAX
} bsp_adc_ch_t;

/* --- 软件 I2C 总线枚举 --- */
typedef enum {
    BSP_I2C_MS5837 = 0,  // 对应 PB6/PB7 (如 MS5837 水压计)
    BSP_I2C_MAX
} bsp_i2c_bus_t;

/* --- GPIO 引脚枚举 --- */
typedef enum {
    BSP_GPIO_DHT11 = 0,
    BSP_GPIO_PSWITCH = 1,
    BSP_GPIO_USERLED = 2,
    // BSP_GPIO_LEAK_DET, // 预留：漏水检测传感器
    // BSP_GPIO_RELAY_1,  // 预留：外部设备供电继电器
    BSP_GPIO_MAX
}bsp_gpio_pin_t;

/* --- TIM 通道枚举 --- */
typedef enum {
    BSP_TIM_SYSCOUNT = 0,
    BSP_TIM_MAX
} bsp_timer_t;



#endif // __BSP_CORE_H
