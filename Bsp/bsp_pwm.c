#include "bsp_pwm.h"

#include "misc.h"
#include "stm32f4xx.h"
#include "stm32f4xx_dma.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_rcc.h"
#include "stm32f4xx_tim.h"

/* --- 只读硬件字典结构体 --- */
typedef struct {
    TIM_TypeDef *tim;        // 定时器基地址
    uint32_t tim_rcc;        // 定时器时钟
    void (*tim_rcc_cmd)(uint32_t, FunctionalState); // 时钟使能函数

    uint8_t ch;              // 通道号 1-4
    GPIO_TypeDef *port;      // GPIO 端口
    uint16_t pin;            // GPIO 引脚号
    uint8_t pin_src;         // GPIO 引脚源
    uint8_t af;              // GPIO 复用功能号
    uint32_t gpio_rcc;       // GPIO 时钟

    void (*pwm_oc_init)(TIM_TypeDef *, TIM_OCInitTypeDef *); // PWM 输出比较初始化函数
    void (*tim_oc_preload_config)(TIM_TypeDef *, uint16_t);  // PWM 预装载配置函数

    uint32_t prescaler;      // 预分频器（PSC）
    uint32_t period;         // 自动重装载值（ARR）

    // 该组字段仅用于“通用 PWM + DMA 波形发送”能力
    bool dma_waveform_capable;
    DMA_Stream_TypeDef *dma_stream;
    uint32_t dma_rcc;
    uint32_t dma_channel;
    uint32_t dma_priority;
    uint32_t dma_clear_flags;
    uint32_t dma_tc_it;
    uint32_t dma_te_it;
    uint16_t tim_dma_src;
    IRQn_Type dma_irqn;
    uint32_t dma_irq_preemption_prio;
} pwm_ch_hw_t;

/* --- 运行时上下文：记录 DMA 波形发送状态 --- */
typedef struct {
    const uint16_t *waveform_buf;
    uint16_t waveform_len;
    bool dma_busy;
} pwm_ch_ctx_t;

static pwm_ch_ctx_t s_pwm_ctx[BSP_PWM_MAX] = {0};

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
    // --- 电机 2：TIM3 CH4 (PB1) ---
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
    // --- 电机 3：TIM3 CH2 (PA7) ---
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
    // --- 电机 4：TIM3 CH3 (PB0) ---
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
    // --- 电机 5：TIM4 CH4 (PD15) ---
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
    // --- 电机 6：TIM4 CH1 (PD12) ---
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
    // --- 电机 7：TIM4 CH3 (PD14) [预留] ---
    [BSP_PWM_THRUSTER_7] = {
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
    // --- 电机 8：TIM4 CH2 (PD13) [预留] ---
    [BSP_PWM_THRUSTER_8] = {
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
    [BSP_PWM_SERVO_1] = {
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
    [BSP_PWM_SERVO_2] = {
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
    },
    // --- 灯带 1：TIM1 CH1 (PE9) ---
    // --- 灯带 1：TIM1 CH1 (PE9)，DMA2 Stream1 / Channel6 ---
    [BSP_PWM_LED_1] = {
        .tim = TIM1,
        .tim_rcc = RCC_APB2Periph_TIM1,
        .tim_rcc_cmd = RCC_APB2PeriphClockCmd,
        .ch = 1,
        .port = GPIOE,
        .pin = GPIO_Pin_9,
        .pin_src = GPIO_PinSource9,
        .af = GPIO_AF_TIM1,
        .gpio_rcc = RCC_AHB1Periph_GPIOE,
        .pwm_oc_init = TIM_OC1Init,
        .tim_oc_preload_config = TIM_OC1PreloadConfig,
        .prescaler = PSC_APB2_WS2812,
        .period = ARR_WS2812,
        .dma_waveform_capable = true,
        .dma_stream = DMA2_Stream3,
        .dma_rcc = RCC_AHB1Periph_DMA2,
        .dma_channel = DMA_Channel_6,
        .dma_priority = DMA_Priority_High,
        .dma_clear_flags = DMA_FLAG_TCIF1 | DMA_FLAG_HTIF1 | DMA_FLAG_TEIF1 | DMA_FLAG_DMEIF1 | DMA_FLAG_FEIF1,
        .dma_tc_it = DMA_IT_TCIF1,
        .dma_te_it = DMA_IT_TEIF1,
        .tim_dma_src = TIM_DMA_CC1,
        .dma_irqn = DMA2_Stream1_IRQn,
        .dma_irq_preemption_prio = 6U
    },
    // --- 灯带 2：TIM1 CH2 (PE11) ---
    // --- 灯带 2：TIM1 CH2 (PE11)，DMA2 Stream2 / Channel6 ---
    [BSP_PWM_LED_2] = {
        .tim = TIM1,
        .tim_rcc = RCC_APB2Periph_TIM1,
        .tim_rcc_cmd = RCC_APB2PeriphClockCmd,
        .ch = 2,
        .port = GPIOE,
        .pin = GPIO_Pin_11,
        .pin_src = GPIO_PinSource11,
        .af = GPIO_AF_TIM1,
        .gpio_rcc = RCC_AHB1Periph_GPIOE,
        .pwm_oc_init = TIM_OC2Init,
        .tim_oc_preload_config = TIM_OC2PreloadConfig,
        .prescaler = PSC_APB2_WS2812,
        .period = ARR_WS2812,
        .dma_waveform_capable = true,
        .dma_stream = DMA2_Stream2,
        .dma_rcc = RCC_AHB1Periph_DMA2,
        .dma_channel = DMA_Channel_6,
        .dma_priority = DMA_Priority_High,
        .dma_clear_flags = DMA_FLAG_TCIF2 | DMA_FLAG_HTIF2 | DMA_FLAG_TEIF2 | DMA_FLAG_DMEIF2 | DMA_FLAG_FEIF2,
        .dma_tc_it = DMA_IT_TCIF2,
        .dma_te_it = DMA_IT_TEIF2,
        .tim_dma_src = TIM_DMA_CC2,
        .dma_irqn = DMA2_Stream2_IRQn,
        .dma_irq_preemption_prio = 6U
    },
    // --- 探照灯 1：TIM12 CH1 (PB14) [保持不变] ---
    [BSP_PWM_SEARCHLIGHT_1] = {
        .tim = TIM12,
        .tim_rcc = RCC_APB1Periph_TIM12,
        .tim_rcc_cmd = RCC_APB1PeriphClockCmd,
        .ch = 1,
        .port = GPIOB,
        .pin = GPIO_Pin_14,
        .pin_src = GPIO_PinSource14,
        .af = GPIO_AF_TIM12,
        .gpio_rcc = RCC_AHB1Periph_GPIOB,
        .pwm_oc_init = TIM_OC1Init,
        .tim_oc_preload_config = TIM_OC1PreloadConfig,
        .prescaler = PSC_APB1_1MHZ,
        .period = ARR_1KHZ
    },
    // --- 探照灯 2：TIM12 CH2 (PB15) [保持不变] ---
    [BSP_PWM_SEARCHLIGHT_2] = {
        .tim = TIM12,
        .tim_rcc = RCC_APB1Periph_TIM12,
        .tim_rcc_cmd = RCC_APB1PeriphClockCmd,
        .ch = 2,
        .port = GPIOB,
        .pin = GPIO_Pin_15,
        .pin_src = GPIO_PinSource15,
        .af = GPIO_AF_TIM12,
        .gpio_rcc = RCC_AHB1Periph_GPIOB,
        .pwm_oc_init = TIM_OC2Init,
        .tim_oc_preload_config = TIM_OC2PreloadConfig,
        .prescaler = PSC_APB1_1MHZ,
        .period = ARR_1KHZ
    }
};

// 判断该通道是否已经在硬件字典中配置了 DMA 波形发送能力
static bool prv_pwm_has_dma_waveform_support(const pwm_ch_hw_t *hw)
{
    return (hw != NULL) && hw->dma_waveform_capable && (hw->dma_stream != NULL);
}

// 根据通道号获取对应的 CCR 寄存器地址
static volatile uint32_t *prv_pwm_get_ccr_reg(const pwm_ch_hw_t *hw)
{
    if (hw == NULL) {
        return NULL;
    }

    switch (hw->ch) {
        case 1:
            return &hw->tim->CCR1;
        case 2:
            return &hw->tim->CCR2;
        case 3:
            return &hw->tim->CCR3;
        case 4:
            return &hw->tim->CCR4;
        default:
            return NULL;
    }
}

// 统一封装 CCR 写入，便于普通 PWM 与 DMA 波形逻辑复用
// TIM1/TIM8 属于高级定时器，PWM 输出前需要额外打开主输出使能。
static bool prv_pwm_is_advanced_timer(const pwm_ch_hw_t *hw)
{
    return (hw != NULL) && ((hw->tim == TIM1) || (hw->tim == TIM8));
}

static void prv_pwm_write_ccr(const pwm_ch_hw_t *hw, uint16_t value)
{
    volatile uint32_t *ccr_reg = prv_pwm_get_ccr_reg(hw);

    if (ccr_reg != NULL) {
        *ccr_reg = value;
    }
}

// 为支持 DMA 波形发送的 PWM 通道配置 NVIC
static void prv_pwm_configure_dma_irq(const pwm_ch_hw_t *hw)
{
    NVIC_InitTypeDef nvic_init;

    if (!prv_pwm_has_dma_waveform_support(hw)) {
        return;
    }

    nvic_init.NVIC_IRQChannel = hw->dma_irqn;
    nvic_init.NVIC_IRQChannelPreemptionPriority = hw->dma_irq_preemption_prio;
    nvic_init.NVIC_IRQChannelSubPriority = 0U;
    nvic_init.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic_init);
}

// 统一的 DMA 波形发送收尾逻辑
static void prv_pwm_finish_dma_waveform(bsp_pwm_ch_t ch)
{
    const pwm_ch_hw_t *hw;
    pwm_ch_ctx_t *ctx;

    if (ch >= BSP_PWM_MAX) {
        return;
    }

    hw = &pwm_hw_info[ch];
    ctx = &s_pwm_ctx[ch];

    if (!prv_pwm_has_dma_waveform_support(hw)) {
        return;
    }

    TIM_DMACmd(hw->tim, hw->tim_dma_src, DISABLE);
    DMA_ITConfig(hw->dma_stream, DMA_IT_TC | DMA_IT_TE, DISABLE);
    DMA_Cmd(hw->dma_stream, DISABLE);
    while ((hw->dma_stream->CR & DMA_SxCR_EN) != 0U) {
        ;
    }

    DMA_ClearFlag(hw->dma_stream, hw->dma_clear_flags);
    prv_pwm_write_ccr(hw, 0U);

    ctx->waveform_buf = NULL;
    ctx->waveform_len = 0U;
    ctx->dma_busy = false;
}

/************************************************************
 * 统一的 PWM 驱动实现文件
 * 适配了推进器、舵机、探照灯，以及“通用 PWM + DMA 波形发送”能力
 * 通过硬件字典实现硬件资源与上层接口的解耦
 ************************************************************/
bool bsp_pwm_init(uint16_t init_pulse_us)
{
    GPIO_InitTypeDef gpio_init;
    TIM_TimeBaseInitTypeDef tim_base_init;
    TIM_OCInitTypeDef tim_oc_init;
    int i;

    for (i = 0; i < BSP_PWM_MAX; i++) {
        const pwm_ch_hw_t *hw = &pwm_hw_info[i];

        if (hw->tim == NULL) {
            return false;
        }

        // 使能时钟
        hw->tim_rcc_cmd(hw->tim_rcc, ENABLE);
        RCC_AHB1PeriphClockCmd(hw->gpio_rcc, ENABLE);

        // 如果该通道具备 DMA 波形发送能力，则提前完成 DMA 资源初始化
        if (prv_pwm_has_dma_waveform_support(hw) && (hw->dma_rcc != 0U)) {
            RCC_AHB1PeriphClockCmd(hw->dma_rcc, ENABLE);
            DMA_DeInit(hw->dma_stream);
            DMA_ClearFlag(hw->dma_stream, hw->dma_clear_flags);
            prv_pwm_configure_dma_irq(hw);
        }

        // 配置 GPIO 为定时器复用输出
        gpio_init.GPIO_Pin = hw->pin;
        gpio_init.GPIO_Mode = GPIO_Mode_AF;
        gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
        gpio_init.GPIO_OType = GPIO_OType_PP;
        gpio_init.GPIO_PuPd = GPIO_PuPd_UP;
        GPIO_Init(hw->port, &gpio_init);
        GPIO_PinAFConfig(hw->port, hw->pin_src, hw->af);

        // 配置定时器基础参数
        tim_base_init.TIM_Prescaler = hw->prescaler;
        tim_base_init.TIM_Period = hw->period;
        tim_base_init.TIM_CounterMode = TIM_CounterMode_Up;
        tim_base_init.TIM_ClockDivision = TIM_CKD_DIV1;
        tim_base_init.TIM_RepetitionCounter = 0U;
        TIM_TimeBaseInit(hw->tim, &tim_base_init);

        // 配置 PWM 输出模式
        TIM_OCStructInit(&tim_oc_init);
        tim_oc_init.TIM_OCMode = TIM_OCMode_PWM1;
        tim_oc_init.TIM_OutputState = TIM_OutputState_Enable;
        tim_oc_init.TIM_Pulse = init_pulse_us;
        tim_oc_init.TIM_OCPolarity = TIM_OCPolarity_High;
        hw->pwm_oc_init(hw->tim, &tim_oc_init);

        hw->tim_oc_preload_config(hw->tim, TIM_OCPreload_Enable);
        TIM_Cmd(hw->tim, ENABLE);
        if (prv_pwm_is_advanced_timer(hw)) {
            TIM_CtrlPWMOutputs(hw->tim, ENABLE);
        }

        // 缺省不打开 DMA 请求，由上层在真正发波形时临时使能
        if (prv_pwm_has_dma_waveform_support(hw)) {
            TIM_DMACmd(hw->tim, hw->tim_dma_src, DISABLE);
        }
    }

    return true;
}

// 设置指定 PWM 通道的高电平脉宽时间
// 对于普通 1MHz 基准 PWM，该值可直接理解为微秒；
// 对于特殊时基通道（如后续波形型输出），它本质上就是 CCR 计数值。
void bsp_pwm_set_pulse_us(bsp_pwm_ch_t ch, uint16_t pulse_us)
{
    if (ch >= BSP_PWM_MAX) {
        return;
    }

    prv_pwm_write_ccr(&pwm_hw_info[ch], pulse_us);
}

// 读取指定 PWM 通道当前的 CCR 值
uint16_t bsp_pwm_get_pulse_us(bsp_pwm_ch_t ch)
{
    const pwm_ch_hw_t *hw;
    volatile uint32_t *ccr_reg;

    if (ch >= BSP_PWM_MAX) {
        return 0U;
    }

    hw = &pwm_hw_info[ch];
    ccr_reg = prv_pwm_get_ccr_reg(hw);

    if (ccr_reg == NULL) {
        return 0U;
    }

    return (uint16_t)(*ccr_reg);
}

// 设置指定 PWM 通道的占空比（单位：百分比 %）
void bsp_pwm_set_duty(bsp_pwm_ch_t ch, float duty)
{
    uint32_t period;
    uint16_t pulse;

    if (ch >= BSP_PWM_MAX) {
        return;
    }

    if (duty < 0.0f) {
        duty = 0.0f;
    }
    if (duty > 100.0f) {
        duty = 100.0f;
    }

    period = pwm_hw_info[ch].period + 1U;
    pulse = (uint16_t)((duty / 100.0f) * (float)period);
    bsp_pwm_set_pulse_us(ch, pulse);
}

// 查询指定 PWM 通道是否支持 DMA 波形发送
bool bsp_pwm_supports_dma_waveform(bsp_pwm_ch_t ch)
{
    if (ch >= BSP_PWM_MAX) {
        return false;
    }

    return prv_pwm_has_dma_waveform_support(&pwm_hw_info[ch]);
}

// 启动一次基于 DMA 的 PWM 波形发送
// 这里的 ccr_buf 每个元素都对应一个 PWM 周期要装载到 CCR 的值
// BSP 不关心这串数据的协议语义，只负责按时序发出
bool bsp_pwm_start_dma_waveform(bsp_pwm_ch_t ch, const uint16_t *ccr_buf, uint16_t len)
{
    const pwm_ch_hw_t *hw;
    pwm_ch_ctx_t *ctx;
    DMA_InitTypeDef dma_init;
    volatile uint32_t *ccr_reg;

    if ((ch >= BSP_PWM_MAX) || (ccr_buf == NULL) || (len < 2U)) {
        return false;
    }

    hw = &pwm_hw_info[ch];
    ctx = &s_pwm_ctx[ch];
    ccr_reg = prv_pwm_get_ccr_reg(hw);

    if (!prv_pwm_has_dma_waveform_support(hw) || (ccr_reg == NULL) || ctx->dma_busy) {
        return false;
    }

    // 记录本次发送上下文，供 busy 查询和中断收尾使用
    ctx->waveform_buf = ccr_buf;
    ctx->waveform_len = len;
    ctx->dma_busy = true;

    // 先确保定时器 DMA 请求与 DMA Stream 都处于关闭状态
    TIM_DMACmd(hw->tim, hw->tim_dma_src, DISABLE);
    DMA_Cmd(hw->dma_stream, DISABLE);
    while ((hw->dma_stream->CR & DMA_SxCR_EN) != 0U) {
        ;
    }

    DMA_ClearFlag(hw->dma_stream, hw->dma_clear_flags);
    DMA_DeInit(hw->dma_stream);

    // 配置 DMA：后续 len-1 个元素由 DMA 自动搬运到 CCR
    dma_init.DMA_Channel = hw->dma_channel;
    dma_init.DMA_PeripheralBaseAddr = (uint32_t)ccr_reg;
    dma_init.DMA_Memory0BaseAddr = (uint32_t)&ccr_buf[1];
    dma_init.DMA_DIR = DMA_DIR_MemoryToPeripheral;
    dma_init.DMA_BufferSize = (uint32_t)(len - 1U);
    dma_init.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma_init.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma_init.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    dma_init.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    dma_init.DMA_Mode = DMA_Mode_Normal;
    dma_init.DMA_Priority = hw->dma_priority;
    dma_init.DMA_FIFOMode = DMA_FIFOMode_Disable;
    dma_init.DMA_FIFOThreshold = DMA_FIFOThreshold_HalfFull;
    dma_init.DMA_MemoryBurst = DMA_MemoryBurst_Single;
    dma_init.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;
    DMA_Init(hw->dma_stream, &dma_init);
    DMA_ITConfig(hw->dma_stream, DMA_IT_TC | DMA_IT_TE, ENABLE);

    // 第一个点位先手动写入 CCR，后续更新由 DMA 接管
    TIM_Cmd(hw->tim, DISABLE);
    prv_pwm_write_ccr(hw, ccr_buf[0]);
    TIM_SetCounter(hw->tim, 0U);
    TIM_GenerateEvent(hw->tim, TIM_EventSource_Update);

    TIM_DMACmd(hw->tim, hw->tim_dma_src, ENABLE);
    DMA_Cmd(hw->dma_stream, ENABLE);
    TIM_Cmd(hw->tim, ENABLE);

    return true;
}

void bsp_pwm_abort_dma_waveform(bsp_pwm_ch_t ch)
{
    prv_pwm_finish_dma_waveform(ch);
}

void bsp_pwm_poll_dma_waveform(bsp_pwm_ch_t ch)
{
    bsp_pwm_dma_waveform_irq_handler(ch);
}

// 查询指定 PWM 通道当前是否处于 DMA 波形发送中
bool bsp_pwm_is_dma_waveform_busy(bsp_pwm_ch_t ch)
{
    if (ch >= BSP_PWM_MAX) {
        return false;
    }

    return s_pwm_ctx[ch].dma_busy;
}

// PWM 波形 DMA 中断统一处理入口
// 由具体的 DMAx_Streamy_IRQHandler 调用，用于处理发送完成或错误收尾。
void bsp_pwm_dma_waveform_irq_handler(bsp_pwm_ch_t ch)
{
    const pwm_ch_hw_t *hw;

    if (ch >= BSP_PWM_MAX) {
        return;
    }

    hw = &pwm_hw_info[ch];
    if (!prv_pwm_has_dma_waveform_support(hw)) {
        return;
    }

    if (DMA_GetITStatus(hw->dma_stream, hw->dma_te_it) != RESET) {
        DMA_ClearITPendingBit(hw->dma_stream, hw->dma_te_it);
        prv_pwm_finish_dma_waveform(ch);
        return;
    }

    if (DMA_GetITStatus(hw->dma_stream, hw->dma_tc_it) != RESET) {
        DMA_ClearITPendingBit(hw->dma_stream, hw->dma_tc_it);
        prv_pwm_finish_dma_waveform(ch);
    }
}
