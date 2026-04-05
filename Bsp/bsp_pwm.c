#include "bsp_pwm.h"
#include "stm32f4xx.h"
#include "stm32f4xx_tim.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_rcc.h"

#include "sys_log.h"

/* ---  只读硬件字典结构体 --- */
typedef struct {
    TIM_TypeDef* tim;        // 定时器基地址
    uint32_t tim_rcc;        // 定时器时钟 (如 RCC_APB1Periph_TIM3)
    void (*tim_rcc_cmd)(uint32_t, FunctionalState); // 时钟使能函数 (如 RCC_APB1PeriphClockCmd)
    
    uint8_t ch;              // 通道号 1-4
    GPIO_TypeDef* port;      // GPIO 端口基地址 
    uint16_t pin;            // GPIO 引脚号 (如 GPIO_Pin_0)
    uint8_t pin_src;         // GPIO 引脚源 (如 GPIO_PinSource0)
    uint8_t af;              // GPIO 复用功能号 (如 GPIO_AF_TIM2)  
    uint32_t gpio_rcc;       // GPIO 时钟 (如 RCC_AHB1Periph_GPIOA)

    void (*pwm_oc_init)(TIM_TypeDef*, TIM_OCInitTypeDef*); // PWM 输出比较初始化函数指针
    void (*tim_oc_preload_config)(TIM_TypeDef*, uint16_t); // 定时器输出比较预装载配置函数指针

    uint32_t prescaler;      // 预分频器 (PSC)
    uint32_t period;         // 自动重装载值 (ARR)

} pwm_ch_hw_t;

static const pwm_ch_hw_t pwm_hw_info[BSP_PWM_MAX] = {
    // --- 电机 1：TIM3 CH1 (PA6) [保持不变] ---
    [BSP_PWM_THRUSTER_1] = {
        .tim = TIM3,
        .tim_rcc = RCC_APB1Periph_TIM3, 
        .tim_rcc_cmd = RCC_APB1PeriphClockCmd,
        .ch = 1, 
        .port = GPIOA, 
        .pin = GPIO_Pin_6, 
        .pin_src = GPIO_PinSource6, 
        .af = GPIO_AF_TIM3, 
        .gpio_rcc = RCC_AHB1Periph_GPIOA,
        .pwm_oc_init = TIM_OC1Init,
        .tim_oc_preload_config = TIM_OC1PreloadConfig,
        .prescaler = PSC_APB1_1MHZ,
        .period = ARR_50HZ
    },
    // --- 电机 2：修改为 TIM3 CH4 (PB1) ---
    [BSP_PWM_THRUSTER_2] = {
        .tim = TIM3,
        .tim_rcc = RCC_APB1Periph_TIM3, 
        .tim_rcc_cmd = RCC_APB1PeriphClockCmd,
        .ch = 4, 
        .port = GPIOB, 
        .pin = GPIO_Pin_1, 
        .pin_src = GPIO_PinSource1, 
        .af = GPIO_AF_TIM3, 
        .gpio_rcc = RCC_AHB1Periph_GPIOB,
        .pwm_oc_init = TIM_OC4Init,
        .tim_oc_preload_config = TIM_OC4PreloadConfig,
        .prescaler = PSC_APB1_1MHZ,
        .period = ARR_50HZ
    },
    // --- 电机 3：修改为 TIM3 CH2 (PA7) ---
    [BSP_PWM_THRUSTER_3] = {
        .tim = TIM3, 
        .tim_rcc = RCC_APB1Periph_TIM3,
        .tim_rcc_cmd = RCC_APB1PeriphClockCmd,
        .ch = 2, 
        .port = GPIOA, 
        .pin = GPIO_Pin_7, 
        .pin_src = GPIO_PinSource7, 
        .af = GPIO_AF_TIM3, 
        .gpio_rcc = RCC_AHB1Periph_GPIOA,
        .pwm_oc_init = TIM_OC2Init,
        .tim_oc_preload_config = TIM_OC2PreloadConfig,
        .prescaler = PSC_APB1_1MHZ,
        .period = ARR_50HZ
    },
    // --- 电机 4：修改为 TIM3 CH3 (PB0) ---
    [BSP_PWM_THRUSTER_4] = {
        .tim = TIM3, 
        .tim_rcc = RCC_APB1Periph_TIM3,
        .tim_rcc_cmd = RCC_APB1PeriphClockCmd,
        .ch = 3, 
        .port = GPIOB, 
        .pin = GPIO_Pin_0, 
        .pin_src = GPIO_PinSource0, 
        .af = GPIO_AF_TIM3, 
        .gpio_rcc = RCC_AHB1Periph_GPIOB,
        .pwm_oc_init = TIM_OC3Init,
        .tim_oc_preload_config = TIM_OC3PreloadConfig,
        .prescaler = PSC_APB1_1MHZ,
        .period = ARR_50HZ
    },
    // --- 电机 5：修改为 TIM4 CH4 (PD15) ---
    [BSP_PWM_THRUSTER_5] = {
        .tim = TIM4, 
        .tim_rcc = RCC_APB1Periph_TIM4,
        .tim_rcc_cmd = RCC_APB1PeriphClockCmd,
        .ch = 4, 
        .port = GPIOD, 
        .pin = GPIO_Pin_15, 
        .pin_src = GPIO_PinSource15, 
        .af = GPIO_AF_TIM4, 
        .gpio_rcc = RCC_AHB1Periph_GPIOD,
        .pwm_oc_init = TIM_OC4Init,
        .tim_oc_preload_config = TIM_OC4PreloadConfig,
        .prescaler = PSC_APB1_1MHZ,
        .period = ARR_50HZ
    },
    // --- 电机 6：修改为 TIM4 CH1 (PD12) ---
    [BSP_PWM_THRUSTER_6] = {
        .tim = TIM4, 
        .tim_rcc = RCC_APB1Periph_TIM4,
        .tim_rcc_cmd = RCC_APB1PeriphClockCmd,
        .ch = 1, 
        .port = GPIOD, 
        .pin = GPIO_Pin_12,
        .pin_src = GPIO_PinSource12,
        .af = GPIO_AF_TIM4,
        .gpio_rcc = RCC_AHB1Periph_GPIOD,
        .pwm_oc_init = TIM_OC1Init,
        .tim_oc_preload_config = TIM_OC1PreloadConfig,
        .prescaler = PSC_APB1_1MHZ,
        .period = ARR_50HZ
    },    
    // --- 探照灯 1：TIM4 CH3 (PD14) [保持不变] ---
    [BSP_PWM_LIGHT_1]    = {
        .tim = TIM4, 
        .tim_rcc = RCC_APB1Periph_TIM4,
        .tim_rcc_cmd = RCC_APB1PeriphClockCmd,
        .ch = 3, 
        .port = GPIOD, 
        .pin = GPIO_Pin_14, 
        .pin_src = GPIO_PinSource14, 
        .af = GPIO_AF_TIM4, 
        .gpio_rcc = RCC_AHB1Periph_GPIOD,
        .pwm_oc_init = TIM_OC3Init,
        .tim_oc_preload_config = TIM_OC3PreloadConfig,
        .prescaler = PSC_APB1_1MHZ,
        .period = ARR_50HZ
    },
    // --- 探照灯 2：修改为 TIM4 CH2 (PD13) ---
    [BSP_PWM_LIGHT_2]    = {
        .tim = TIM4, 
        .tim_rcc = RCC_APB1Periph_TIM4,
        .tim_rcc_cmd = RCC_APB1PeriphClockCmd,
        .ch = 2, 
        .port = GPIOD, 
        .pin = GPIO_Pin_13, 
        .pin_src = GPIO_PinSource13, 
        .af = GPIO_AF_TIM4, 
        .gpio_rcc = RCC_AHB1Periph_GPIOD,
        .pwm_oc_init = TIM_OC2Init,
        .tim_oc_preload_config = TIM_OC2PreloadConfig,
        .prescaler = PSC_APB1_1MHZ,
        .period = ARR_50HZ
    },
    // --- 舵机 1：TIM9 CH1 (PE5) [保持不变] ---
    [BSP_PWM_SERVO_1]    = {
        .tim = TIM9, 
        .tim_rcc = RCC_APB2Periph_TIM9,
        .tim_rcc_cmd = RCC_APB2PeriphClockCmd,
        .ch = 1, 
        .port = GPIOE, 
        .pin = GPIO_Pin_5, 
        .pin_src = GPIO_PinSource5, 
        .af = GPIO_AF_TIM9, 
        .gpio_rcc = RCC_AHB1Periph_GPIOE,
        .pwm_oc_init = TIM_OC1Init,
        .tim_oc_preload_config = TIM_OC1PreloadConfig,
        .prescaler = PSC_APB2_1MHZ,
        .period = ARR_50HZ
    },
    // --- 舵机 2：TIM9 CH2 (PE6) [保持不变] ---
    [BSP_PWM_SERVO_2]    = {
        .tim = TIM9, 
        .tim_rcc = RCC_APB2Periph_TIM9,
        .tim_rcc_cmd = RCC_APB2PeriphClockCmd,
        .ch = 2, 
        .port = GPIOE, 
        .pin = GPIO_Pin_6, 
        .pin_src = GPIO_PinSource6, 
        .af = GPIO_AF_TIM9, 
        .gpio_rcc = RCC_AHB1Periph_GPIOE,
        .pwm_oc_init = TIM_OC2Init,
        .tim_oc_preload_config = TIM_OC2PreloadConfig,
        .prescaler = PSC_APB2_1MHZ,
        .period = ARR_50HZ
    }
};

/************************************************************
 * 统一的 PWM 驱动实现文件
 * 适配了推进器、舵机和探照灯的控制需求
 * 通过硬件字典实现了高度抽象和解耦
 ************************************************************/

bool bsp_pwm_init(uint16_t init_pulse_us) {
    GPIO_InitTypeDef GPIO_InitStructure;
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
    TIM_OCInitTypeDef TIM_OCInitStructure;

    for(int i=0;i<BSP_PWM_MAX;i++) {
        const pwm_ch_hw_t* hw = &pwm_hw_info[i];
        if(hw->tim == NULL) {
            return false;
        }
        
        // 使能时钟
        hw->tim_rcc_cmd(hw->tim_rcc, ENABLE);
        RCC_AHB1PeriphClockCmd(hw->gpio_rcc, ENABLE);
        
        // 配置 GPIO 引脚为复用功能
        GPIO_InitStructure.GPIO_Pin = hw->pin;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
        GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
        GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
        GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
        GPIO_Init(hw->port, &GPIO_InitStructure);
        
        // 配置 GPIO 复用映射
        GPIO_PinAFConfig(hw->port, hw->pin_src, hw->af);
        
        // 配置定时器基本参数
        TIM_TimeBaseInitStructure.TIM_Prescaler = hw->prescaler;
        TIM_TimeBaseInitStructure.TIM_Period = hw->period;
        TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
        TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
        TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;
        TIM_TimeBaseInit(hw->tim, &TIM_TimeBaseInitStructure);
        
        // 配置 PWM 模式
        TIM_OCStructInit(&TIM_OCInitStructure);
        TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
        TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
        TIM_OCInitStructure.TIM_Pulse = init_pulse_us; // 初始占空比/脉宽设为 0
        TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
        hw->pwm_oc_init(hw->tim, &TIM_OCInitStructure);

        hw->tim_oc_preload_config(hw->tim, TIM_OCPreload_Enable);

        TIM_Cmd(hw->tim, ENABLE);
    }

    return true;
}

/* -------------------------------------------------------------------------
 * 函数名：bsp_pwm_set_pulse_us
 * 功  能：设置指定 PWM 通道的高电平脉宽时间 (单位：微秒 us)
 * 参  数：ch - PWM 通道枚举 (如 BSP_PWM_THRUSTER_1)
 * pulse_us - 高电平微秒数 (如 1500 代表中位停止)
 * ------------------------------------------------------------------------- */
void bsp_pwm_set_pulse_us(bsp_pwm_ch_t ch, uint16_t pulse_us) 
{
    if (ch >= BSP_PWM_MAX) return;
    
    TIM_TypeDef* tim = pwm_hw_info[ch].tim;
    uint8_t ch_num = pwm_hw_info[ch].ch;

    // 直接操作 CCR 寄存器改变脉宽 (极速响应)
    switch (ch_num) {
        case 1: tim->CCR1 = pulse_us; break;
        case 2: tim->CCR2 = pulse_us; break;
        case 3: tim->CCR3 = pulse_us; break;
        case 4: tim->CCR4 = pulse_us; break;
    }
}


uint16_t bsp_pwm_get_pulse_us(bsp_pwm_ch_t ch)
{
    if (ch >= BSP_PWM_MAX) return 0;

    TIM_TypeDef* tim = pwm_hw_info[ch].tim;
    uint8_t ch_num = pwm_hw_info[ch].ch;

    switch (ch_num) {
        case 1: return (uint16_t)tim->CCR1;
        case 2: return (uint16_t)tim->CCR2;
        case 3: return (uint16_t)tim->CCR3;
        case 4: return (uint16_t)tim->CCR4;
        default: return 0;
    }
}

/* -------------------------------------------------------------------------
 * 函数名：bsp_pwm_set_duty
 * 功  能：设置指定 PWM 通道的占空比 (单位：百分比 %)
 * 参  数：ch - PWM 通道枚举 (如 BSP_PWM_LIGHT_1)
 * duty - 占空比 0.0f ~ 100.0f
 * ------------------------------------------------------------------------- */
void bsp_pwm_set_duty(bsp_pwm_ch_t ch, float duty) 
{
    if (ch >= BSP_PWM_MAX) return;

    // 1. 安全限幅
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 100.0f) duty = 100.0f;

    // 2. 获取该通道的周期总计数值 (ARR + 1)
    uint32_t period = pwm_hw_info[ch].period + 1;

    // 3. 计算对应的脉宽计数值
    uint16_t pulse = (uint16_t)((duty / 100.0f) * period);

    // 4. 复用上面的脉宽设置函数
    bsp_pwm_set_pulse_us(ch, pulse);
}

