#include "bsp_gpio.h"
#include "stm32f4xx.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_rcc.h"

/* --- 只读硬件字典结构体 --- */
typedef struct {
    GPIO_TypeDef* port;     // GPIO 端口基地址 (如 GPIOB)
    uint16_t      pin;      // GPIO 引脚号 (如 GPIO_Pin_12)
    uint32_t      rcc;      // GPIO 时钟宏 (如 RCC_AHB1Periph_GPIOB)
} gpio_hw_info_t;

/* --- 实例化 GPIO 硬件字典 (存入 Flash) --- */
static const gpio_hw_info_t s_gpio_hw_info[BSP_GPIO_MAX] = {
    [BSP_GPIO_DHT11] = {
        .port= GPIOB,
        .pin = GPIO_Pin_9,
        .rcc = RCC_AHB1Periph_GPIOB

    }
};

void bsp_gpio_init(void) {
    for (int i = 0; i < BSP_GPIO_MAX; i++) {
        if (s_gpio_hw_info[i].port != NULL) {
            RCC_AHB1PeriphClockCmd(s_gpio_hw_info[i].rcc, ENABLE);
        }
    }
}

void bsp_gpio_set_direction(bsp_gpio_pin_t pin, bool is_output) {
    if (pin >= BSP_GPIO_MAX) return; // 安全校验

    const gpio_hw_info_t* hw = &s_gpio_hw_info[pin];
    GPIO_InitTypeDef GPIO_InitStructure;

    GPIO_InitStructure.GPIO_Pin = hw->pin;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

    if(is_output){
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
        GPIO_InitStructure.GPIO_OType = GPIO_OType_PP; // 推挽输出
        GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
    } else {
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
        GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP; // 输入模式下启用上拉
    }

    GPIO_Init(hw->port, &GPIO_InitStructure);
}

void bsp_gpio_write(bsp_gpio_pin_t pin, bool state) {
    if (pin >= BSP_GPIO_MAX) return; // 安全校验

    const gpio_hw_info_t* hw = &s_gpio_hw_info[pin];

    if(state) {
        GPIO_SetBits(hw->port, hw->pin);
    } else {
        GPIO_ResetBits(hw->port, hw->pin);
    }

}

bool bsp_gpio_read(bsp_gpio_pin_t pin) {
    if (pin >= BSP_GPIO_MAX) return false; // 安全校验

    // 直接读取 IDR 寄存器并进行位与运算
    if ((s_gpio_hw_info[pin].port->IDR & s_gpio_hw_info[pin].pin) != 0) {
        return true;
    } else {
        return false;
    }
}
