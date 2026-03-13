#include "bsp_pwm.h"
#include "stm32f4xx.h"
#include "stm32f4xx_tim.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_rcc.h"

/* --- 1. 只读硬件字典结构体 --- */
typedef struct {
    TIM_TypeDef* tim;        // 定时器基地址
    uint8_t ch;              // 通道号 1-4
    GPIO_TypeDef* port;      // GPIO 端口基地址 
    uint16_t pin;            // GPIO 引脚号 (如 GPIO_Pin_0)
    uint8_t pin_src;         // GPIO 引脚源 (如 GPIO_PinSource0)
    uint8_t af;              // GPIO 复用功能号 (如 GPIO_AF_TIM2)  
    uint32_t gpio_rcc;       // GPIO 时钟 (如 RCC_AHB1Periph_GPIOA)
} pwm_ch_hw_t;

/* * 假设：系统时钟 168MHz，APB1 定时器频率 84MHz，APB2 定时器频率 168MHz。
 * 为了得到 1MHz 的计数频率 (1us 分辨率)：
 * APB1 定时器 PSC = 84 - 1
 * APB2 定时器 PSC = 168 - 1
 */
#define PSC_APB1_1MHZ  (84 - 1)
#define PSC_APB2_1MHZ  (168 - 1)

#define ARR_50HZ       (20000 - 1)  // 20ms 周期，用于推进器和舵机
#define ARR_1KHZ       (1000 - 1)   // 1ms 周期，用于探照灯

static const pwm_ch_hw_t pwm_hw_info[BSP_PWM_MAX] = {
    [BSP_PWM_THRUSTER_1] = {
        .tim = TIM3, 
        .ch = 1, 
        .port = GPIOA, 
        .pin = GPIO_Pin_6, 
        .pin_src = GPIO_PinSource6, 
        .af = GPIO_AF_TIM3, 
        .gpio_rcc = RCC_AHB1Periph_GPIOA
    },
    [BSP_PWM_THRUSTER_2] = {
        .tim = TIM3, 
        .ch = 2, 
        .port = GPIOA, 
        .pin = GPIO_Pin_7, 
        .pin_src = GPIO_PinSource7, 
        .af = GPIO_AF_TIM3, 
        .gpio_rcc = RCC_AHB1Periph_GPIOA
    },
    [BSP_PWM_THRUSTER_3] = {
        .tim = TIM3, 
        .ch = 3, 
        .port = GPIOB, 
        .pin = GPIO_Pin_0, 
        .pin_src = GPIO_PinSource0, 
        .af = GPIO_AF_TIM3, 
        .gpio_rcc = RCC_AHB1Periph_GPIOB
    },
    [BSP_PWM_THRUSTER_4] = {
        .tim = TIM3, 
        .ch = 4, 
        .port = GPIOB, 
        .pin = GPIO_Pin_1, 
        .pin_src = GPIO_PinSource1, 
        .af = GPIO_AF_TIM3, 
        .gpio_rcc = RCC_AHB1Periph_GPIOB
    },
    [BSP_PWM_THRUSTER_5] = {
        .tim = TIM4, 
        .ch = 1, 
        .port = GPIOD, 
        .pin = GPIO_Pin_12, 
        .pin_src = GPIO_PinSource12, 
        .af = GPIO_AF_TIM4, 
        .gpio_rcc = RCC_AHB1Periph_GPIOD
    },
    [BSP_PWM_THRUSTER_6] = {
        .tim = TIM4, 
        .ch = 2, 
        .port = GPIOD, 
        .pin =GPIO_Pin_13,
        .pin_src = GPIO_PinSource13,
        .af = GPIO_AF_TIM4,
        .gpio_rcc = RCC_AHB1Periph_GPIOD
    },    
    [BSP_PWM_LIGHT_1]    = {
        .tim = TIM4, 
        .ch = 3, 
        .port = GPIOD, 
        .pin = GPIO_Pin_14, 
        .pin_src = GPIO_PinSource14, 
        .af = GPIO_AF_TIM4, 
        .gpio_rcc = RCC_AHB1Periph_GPIOD
    },
    [BSP_PWM_LIGHT_2]    = {
        .tim = TIM4, 
        .ch = 4, 
        .port = GPIOD, 
        .pin = GPIO_Pin_15, 
        .pin_src = GPIO_PinSource15, 
        .af = GPIO_AF_TIM4, 
        .gpio_rcc = RCC_AHB1Periph_GPIOD
    },
    [BSP_PWM_SERVO_1]    = {
        .tim = TIM9, 
        .ch = 1, 
        .port = GPIOE, 
        .pin = GPIO_Pin_5, 
        .pin_src = GPIO_PinSource5, 
        .af = GPIO_AF_TIM9, 
        .gpio_rcc = RCC_AHB1Periph_GPIOE
    },
    [BSP_PWM_SERVO_2]    = {
        .tim = TIM9, 
        .ch = 2, 
        .port = GPIOE, 
        .pin = GPIO_Pin_6, 
        .pin_src = GPIO_PinSource6, 
        .af = GPIO_AF_TIM9, 
        .gpio_rcc = RCC_AHB1Periph_GPIOE
    }
};