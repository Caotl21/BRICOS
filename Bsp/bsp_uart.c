#include "bsp_uart.h"
#include "stm32f4xx.h"
#include "stm32f4xx_usart.h"
#include "stm32f4xx_dma.h"
#include "stm32f4xx_rcc.h"
#include "stm32f4xx_gpio.h"
#include "misc.h"

#include <stdbool.h>

/* 内部管理的私有硬件上下文 */
typedef struct {
    USART_TypeDef* uart_base;

    // 时钟信息
    uint32_t gpio_rcc;
    uint32_t uart_rcc;
    uint32_t dma_rcc;
    void (*uart_rcc_cmd)(uint32_t, FunctionalState);

    // GPIO TX 引脚配置
    GPIO_TypeDef* gpio_tx_port;
    uint16_t gpio_pin_tx;
    uint16_t gpio_pin_tx_src;

    // GPIO RX 引脚配置
    GPIO_TypeDef* gpio_rx_port;
    uint16_t gpio_pin_rx;
    uint16_t gpio_pin_rx_src;

    // 复用映射功能号
    uint8_t       gpio_af;
    
    // 中断号 (可选，用于需要开启 NVIC 的串口)
    uint8_t       irqn;
    uint32_t      nvic_priority;

    // DMA 配置 (仅用于需要 DMA 的串口)
    DMA_Stream_TypeDef* dma_rx_stream;  
    uint32_t            dma_channel;
    uint32_t            dma_priority;
    uint32_t            dma_rx_clear_flags;
} uart_hw_info_t;

typedef struct {
    uint8_t*            rx_buf_ptr;  
    uint16_t            rx_buf_size;
    bsp_uart_rx_cb_t    rx_cb;
} uart_ctx_t;

/* --- 实例化硬件字典 --- */
// const优化 存入只读存储区 不占用RAM
static const uart_hw_info_t s_uart_hw_info[BSP_UART_MAX] = {
    [BSP_UART_IMU1] = {
        .uart_base = USART1,
        .gpio_rcc = RCC_AHB1Periph_GPIOA,
        .uart_rcc = RCC_APB2Periph_USART1,
        .dma_rcc = RCC_AHB1Periph_DMA2,
        .uart_rcc_cmd = RCC_APB2PeriphClockCmd,
        .gpio_tx_port = GPIOA,
        .gpio_pin_tx = GPIO_Pin_9,
        .gpio_pin_tx_src = GPIO_PinSource9,
        .gpio_rx_port = GPIOA,
        .gpio_pin_rx = GPIO_Pin_10,
        .gpio_pin_rx_src = GPIO_PinSource10,
        .gpio_af = GPIO_AF_USART1,
        .irqn = 0,  // 零中断架构 不需要NVIC配置
        .nvic_priority = 0,
        .dma_rx_stream = DMA2_Stream2,
        .dma_channel = DMA_Channel_4,
        .dma_priority = DMA_Priority_High,
        .dma_rx_clear_flags = DMA_FLAG_TCIF2 | DMA_FLAG_HTIF2 | DMA_FLAG_TEIF2 | DMA_FLAG_DMEIF2 | DMA_FLAG_FEIF2
    },

    [BSP_UART_IMU2] = {
        .uart_base = USART2,
        .gpio_rcc = RCC_AHB1Periph_GPIOA,
        .uart_rcc = RCC_APB1Periph_USART2,
        .dma_rcc = RCC_AHB1Periph_DMA1,
        .uart_rcc_cmd = RCC_APB1PeriphClockCmd,
        .gpio_tx_port = GPIOA,
        .gpio_pin_tx = GPIO_Pin_2,
        .gpio_pin_tx_src = GPIO_PinSource2,
        .gpio_rx_port = GPIOA,
        .gpio_pin_rx = GPIO_Pin_3,
        .gpio_pin_rx_src = GPIO_PinSource3,
        .gpio_af = GPIO_AF_USART2,
        .irqn = 0,
        .nvic_priority = 0,
        .dma_rx_stream = DMA1_Stream5,
        .dma_channel = DMA_Channel_4,
        .dma_priority = DMA_Priority_High,
        .dma_rx_clear_flags = DMA_FLAG_TCIF5 | DMA_FLAG_HTIF5 | DMA_FLAG_TEIF5 | DMA_FLAG_DMEIF5 | DMA_FLAG_FEIF5
    },

    [BSP_UART_OPI_RT] = {
        .uart_base = USART3,
        .gpio_rcc = RCC_AHB1Periph_GPIOB,
        .uart_rcc = RCC_APB1Periph_USART3,
        .dma_rcc = RCC_AHB1Periph_DMA1,
        .uart_rcc_cmd = RCC_APB1PeriphClockCmd,
        .gpio_tx_port = GPIOB,
        .gpio_pin_tx = GPIO_Pin_10,
        .gpio_pin_tx_src = GPIO_PinSource10,
        .gpio_rx_port = GPIOB,
        .gpio_pin_rx = GPIO_Pin_11,
        .gpio_pin_rx_src = GPIO_PinSource11,
        .gpio_af = GPIO_AF_USART3,
        .irqn = USART3_IRQn,   // 需要串口DMA空闲中断
        .nvic_priority = 5,
        .dma_rx_stream = DMA1_Stream1,
        .dma_channel = DMA_Channel_4,
        .dma_priority = DMA_Priority_VeryHigh, // 实时通信优先级更高
        .dma_rx_clear_flags = DMA_FLAG_TCIF1 | DMA_FLAG_HTIF1 | DMA_FLAG_TEIF1 | DMA_FLAG_DMEIF1 | DMA_FLAG_FEIF1
    },

    [BSP_UART_OPI_NRT] = {
        .uart_base = UART4,
        .gpio_rcc = RCC_AHB1Periph_GPIOA,
        .uart_rcc = RCC_APB1Periph_UART4,
        .dma_rcc = RCC_AHB1Periph_DMA1,  
        .uart_rcc_cmd = RCC_APB1PeriphClockCmd,
        .gpio_tx_port = GPIOA,
        .gpio_pin_tx = GPIO_Pin_0,
        .gpio_pin_tx_src = GPIO_PinSource0,
        .gpio_rx_port = GPIOA,
        .gpio_pin_rx = GPIO_Pin_1,
        .gpio_pin_rx_src = GPIO_PinSource1,
        .gpio_af = GPIO_AF_UART4,
        .irqn = UART4_IRQn,  // 需要串口接收中断
        .nvic_priority = 7,
        .dma_rx_stream = DMA1_Stream2,
        .dma_channel = DMA_Channel_4,
        .dma_priority = DMA_Priority_Medium, // 非实时通信优先级较低
        .dma_rx_clear_flags = DMA_FLAG_TCIF2 | DMA_FLAG_HTIF2 | DMA_FLAG_TEIF2 | DMA_FLAG_DMEIF2 | DMA_FLAG_FEIF2
    }

};

/* 实例化映射表 */
static uart_ctx_t s_uart_ctx[BSP_UART_MAX] = {0};

/* --- 注册回调函数 --- */
void bsp_uart_register_rx_cb(bsp_uart_port_t port, bsp_uart_rx_cb_t cb) {
    if (port < BSP_UART_MAX) {
        s_uart_ctx[port].rx_cb = cb;
    }
}

/* --- 中断处理核心 (供 stm32f4xx_it.c 调用) --- */
void bsp_uart_isr_handler(bsp_uart_port_t port) {
    if (port >= BSP_UART_MAX) return;
    
    USART_TypeDef* uart = s_uart_hw_info[port].uart_base;
    const uart_hw_info_t* hw = &s_uart_hw_info[port];
    DMA_Stream_TypeDef* dma_rx = hw->dma_rx_stream;

    uart_ctx_t* ctx = &s_uart_ctx[port];

    if (USART_GetITStatus(uart, USART_IT_RXNE) != RESET) 
    {
        uint8_t rx_byte = USART_ReceiveData(uart); // 读 DR 自动清除 RXNE 标志位
        
        // 直接通过回调扔给上层
        if (ctx->rx_cb != NULL) {
            ctx->rx_cb(&rx_byte, 1);
        }
    }

    // =========================================================
    // 场景 2：处理 IDLE 空闲中断 + DMA (比如 USART3 香橙派通信)
    // =========================================================
    if (USART_GetITStatus(uart, USART_IT_IDLE) != RESET) 
    {
        if (dma_rx != NULL && ctx->rx_buf_ptr != NULL && ctx->rx_buf_size > 0) {
            // 1. 清除 IDLE 标志 (读 SR 后读 DR)
            volatile uint32_t temp = uart->SR;
            temp = uart->DR; 
            (void)temp;
                 
            if (dma_rx != NULL) 
            {
                // 2. 停止 DMA，等待彻底关闭
                DMA_Cmd(dma_rx, DISABLE);
                while((dma_rx->CR & DMA_SxCR_EN) != 0);
                
                DMA_ClearFlag(dma_rx, hw->dma_rx_clear_flags);

                // 3. 计算实际接收的长度
                uint16_t rx_len = ctx->rx_buf_size - DMA_GetCurrDataCounter(dma_rx);

                // 4. 调用回调交出数据
                if (ctx->rx_cb != NULL && rx_len > 0) {
                    ctx->rx_cb(ctx->rx_buf_ptr, rx_len);
                }

                // 5. 重置 DMA (注意地址已经在一开始的初始化里绑好了，不需要再填 M0AR)
                DMA_SetCurrDataCounter(dma_rx, ctx->rx_buf_size);
                DMA_Cmd(dma_rx, ENABLE);
            }
        }
        
    }
}

/* -------------------------------------------------------------------------
 * 函数名：bsp_uart_init
 * 功  能：统一的串口底层初始化 (包含时钟、GPIO引脚复用、串口参数及NVIC配置)
 * 参  数：port - 串口端口枚举 (IMU1, IMU2, OPI, OTA)
 * config - 硬件无关的串口配置结构体指针
 * ------------------------------------------------------------------------- */
bool bsp_uart_init(bsp_uart_port_t port, const bsp_uart_config_t *config) {
    if (port >= BSP_UART_MAX || config == NULL) return false;

    // 获取当前串口的硬件字典指针
    const uart_hw_info_t* hw = &s_uart_hw_info[port];

    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    // =========================================================
    // 第一阶段：时钟开启与 GPIO 引脚复用映射 (完全根据你的真实硬件)
    // =========================================================

    RCC_AHB1PeriphClockCmd(hw->gpio_rcc, ENABLE);
    hw->uart_rcc_cmd(hw->uart_rcc, ENABLE);

    if (hw->dma_rcc != 0) {
        RCC_AHB1PeriphClockCmd(hw->dma_rcc, ENABLE);
    }

    // ==========================================
    // 2. 自动配置引脚复用映射
    // ==========================================
    GPIO_PinAFConfig(hw->gpio_tx_port, hw->gpio_pin_tx_src, hw->gpio_af);
    GPIO_PinAFConfig(hw->gpio_rx_port, hw->gpio_pin_rx_src, hw->gpio_af);

    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;

    // 配置 TX
    GPIO_InitStructure.GPIO_Pin = hw->gpio_pin_tx;
    GPIO_Init(hw->gpio_tx_port, &GPIO_InitStructure);

    // 配置 RX 
    GPIO_InitStructure.GPIO_Pin = hw->gpio_pin_rx;
    GPIO_Init(hw->gpio_rx_port, &GPIO_InitStructure);

    // ===========================================================
    // 第二阶段：将通用 config 结构体翻译为标准库的 USART_InitTypeDef
    // ===========================================================
    USART_InitStructure.USART_BaudRate = config->baudrate;

    // 翻译数据位
    if (config->data_bits == BSP_UART_DATA_9B) {
        USART_InitStructure.USART_WordLength = USART_WordLength_9b;
    } else {
        USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    }

    // 翻译停止位
    if (config->stop_bits == BSP_UART_STOP_2B) {
        USART_InitStructure.USART_StopBits = USART_StopBits_2;
    } else {
        USART_InitStructure.USART_StopBits = USART_StopBits_1;
    }

    // 翻译校验位
    if (config->parity == BSP_UART_PARITY_EVEN) {
        USART_InitStructure.USART_Parity = USART_Parity_Even;
    } else if (config->parity == BSP_UART_PARITY_ODD) {
        USART_InitStructure.USART_Parity = USART_Parity_Odd;
    } else {
        USART_InitStructure.USART_Parity = USART_Parity_No;
    }

    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    
    // 写入底层寄存器
    USART_Init(hw->uart_base, &USART_InitStructure);

    // ==========================================
    // 4. 按需配置 NVIC (查表判断是否需要中断)
    // ==========================================
    if (hw->irqn != 0) 
    {
        NVIC_InitStructure.NVIC_IRQChannel = hw->irqn;
        NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = hw->nvic_priority;
        NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
        NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
        NVIC_Init(&NVIC_InitStructure);
        
        // 开启特定中断标志位 (香橙派开 IDLE，OTA开 RXNE)
        if (port == BSP_UART_OPI_RT) {
            USART_ITConfig(hw->uart_base, USART_IT_IDLE, ENABLE);
        } else if (port == BSP_UART_OPI_NRT) {
            USART_ITConfig(hw->uart_base, USART_IT_IDLE, ENABLE);
        }
    }

    // ==========================================
    // 5. 使能外设
    // ==========================================
    USART_Cmd(hw->uart_base, ENABLE);

    return true;
}


/* -------------------------------------------------------------------------
 * 函数名：bsp_uart_start_dma_rx_circular
 * 功  能：启动指定串口的 DMA 循环接收模式，接收数据直接写入上层提供的缓冲区，实现零拷贝
 * 参  数：port - 串口端口枚举 (IMU1, IMU2, OPI, OTA) buf_addr - 缓冲区地址  buf_size - 缓冲区大小 (单位：字节)
 * ------------------------------------------------------------------------- */
void bsp_uart_start_dma_rx_circular(bsp_uart_port_t port, uint8_t *buf_addr, uint16_t buf_size) {
    if (port >= BSP_UART_MAX || buf_addr == NULL || s_uart_hw_info[port].dma_rx_stream == NULL) return;

    DMA_Stream_TypeDef* dma_rx = s_uart_hw_info[port].dma_rx_stream;

    uart_ctx_t* ctx = &s_uart_ctx[port];
    ctx->rx_buf_ptr = buf_addr;
    ctx->rx_buf_size = buf_size;

    // 先停掉 DMA，确保配置安全
    DMA_DeInit(dma_rx);

    DMA_InitTypeDef DMA_InitStructure;
    DMA_InitStructure.DMA_Channel = s_uart_hw_info[port].dma_channel;
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&s_uart_hw_info[port].uart_base->DR;

    // 【关键】：这里使用了上层传进来的缓冲区指针与大小，实现了零拷贝解耦！
    DMA_InitStructure.DMA_Memory0BaseAddr = (uint32_t)buf_addr; 
    DMA_InitStructure.DMA_BufferSize = buf_size;
    
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralToMemory;                     // 方向：外设 -> 内存
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;            // 外设地址不增
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;                     // 内存地址自增
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;     // 字节传输
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    
    DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;             // 【关键】循环模式
    DMA_InitStructure.DMA_Priority = s_uart_hw_info[port].dma_priority;     // 优先级很高
    DMA_InitStructure.DMA_FIFOMode = DMA_FIFOMode_Disable;      // 禁用 FIFO 模式
    
    DMA_InitStructure.DMA_FIFOThreshold = DMA_FIFOThreshold_Full;
    DMA_InitStructure.DMA_MemoryBurst = DMA_MemoryBurst_Single;
    DMA_InitStructure.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;

    DMA_Init(dma_rx, &DMA_InitStructure);
    
    // 开启串口的 DMA 请求，并启动 DMA 流
    USART_DMACmd(s_uart_hw_info[port].uart_base, USART_DMAReq_Rx, ENABLE);
    DMA_Cmd(dma_rx, ENABLE);
}

/* -------------------------------------------------------------------------
 * bsp_uart_start_dma_rx_normal
 * 功  能：启动指定串口的 DMA 正常接收模式，接收数据直接写入上层提供的缓冲区，实现零拷贝
 * 参  数：port - 串口端口枚举 (IMU1, IMU2, OPI, OTA) buf_addr - 缓冲区地址  buf_size - 缓冲区大小 (单位：字节)
 * ------------------------------------------------------------------------- */
void bsp_uart_start_dma_rx_normal(bsp_uart_port_t port, uint8_t *buf_addr, uint16_t buf_size) {
    if (port >= BSP_UART_MAX || buf_addr == NULL || s_uart_hw_info[port].dma_rx_stream == NULL) return;

    DMA_Stream_TypeDef* dma_rx = s_uart_hw_info[port].dma_rx_stream;

    uart_ctx_t* ctx = &s_uart_ctx[port];
    ctx->rx_buf_ptr = buf_addr;
    ctx->rx_buf_size = buf_size;

    // 先停掉 DMA，确保配置安全
    DMA_DeInit(dma_rx);

    DMA_InitTypeDef DMA_InitStructure;
    DMA_InitStructure.DMA_Channel = s_uart_hw_info[port].dma_channel;
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&s_uart_hw_info[port].uart_base->DR;

    // 【关键】：这里使用了上层传进来的缓冲区指针与大小，实现了零拷贝解耦！
    DMA_InitStructure.DMA_Memory0BaseAddr = (uint32_t)buf_addr; 
    DMA_InitStructure.DMA_BufferSize = buf_size;
    
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralToMemory;                     // 方向：外设 -> 内存
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;            // 外设地址不增
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;                     // 内存地址自增
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;     // 字节传输
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;             // 【关键】普通模式
    DMA_InitStructure.DMA_Priority = s_uart_hw_info[port].dma_priority;     // 优先级很高
    DMA_InitStructure.DMA_FIFOMode = DMA_FIFOMode_Disable;      // 禁用 FIFO 模式
    
    DMA_InitStructure.DMA_FIFOThreshold = DMA_FIFOThreshold_Full;
    DMA_InitStructure.DMA_MemoryBurst = DMA_MemoryBurst_Single;
    DMA_InitStructure.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;

    DMA_Init(dma_rx, &DMA_InitStructure);
    
    // 开启串口的 DMA 请求，并启动 DMA 流
    USART_DMACmd(s_uart_hw_info[port].uart_base, USART_DMAReq_Rx, ENABLE);
    DMA_Cmd(dma_rx, ENABLE);
}

/* --- 获取 DMA 当前剩余接收长度 --- */
uint16_t bsp_uart_get_dma_rx_remaining(bsp_uart_port_t port) {
    if (port >= BSP_UART_MAX || s_uart_hw_info[port].dma_rx_stream == NULL) return 0;
    
    // 直接调用标准库函数获取当前 DMA 数据计数器的值，这就是剩余未接收的数据长度
    return DMA_GetCurrDataCounter(s_uart_hw_info[port].dma_rx_stream);
}

void bsp_uart_send_buffer(bsp_uart_port_t port, const uint8_t *data, uint16_t len) {
    if(port >= BSP_UART_MAX || data == NULL || len == 0) return;

    USART_TypeDef* uart = s_uart_hw_info[port].uart_base;

    // =======================================================
    // [进阶预留] 
    // 如果你在 FreeRTOS 中多个任务会同时调用此函数发数据，
    // 请在此处获取互斥锁 (Mutex)，防止数据交错乱码：
    // xSemaphoreTake(s_uart_tx_mutex[port], portMAX_DELAY);
    // =======================================================

    for (uint16_t i = 0; i < len; i++) {
        // 等待发送缓冲区空
        while (USART_GetFlagStatus(uart, USART_FLAG_TXE) == RESET);
        uart->DR = data[i]; // 直接写数据寄存器触发发送
    }

    while(USART_GetFlagStatus(uart, USART_FLAG_TC) == RESET); // 等待最后一个字节发送完成

    // =======================================================
    // [进阶预留]
    // 在此处释放互斥锁：
    // xSemaphoreGive(s_uart_tx_mutex[port]);
    // =======================================================
}

bool bsp_uart_send_dma(bsp_uart_port_t port, uint8_t *data, uint16_t len) {
    return false; // 目前不实现 DMA 发送，后续可根据需要添加
}