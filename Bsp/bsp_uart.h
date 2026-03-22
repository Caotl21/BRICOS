#ifndef __BSP_UART_H
#define __BSP_UART_H

#include "bsp_core.h"

/* --- 1. 定义硬件无关的串口参数枚举 --- */
typedef enum {
    BSP_UART_DATA_8B = 0, // 8位数据
    BSP_UART_DATA_9B      // 9位数据
} bsp_uart_data_bits_t;

typedef enum {
    BSP_UART_STOP_1B = 0, // 1位停止位
    BSP_UART_STOP_2B      // 2位停止位
} bsp_uart_stop_bits_t;

typedef enum {
    BSP_UART_PARITY_NONE = 0, // 无校验
    BSP_UART_PARITY_EVEN,     // 偶校验
    BSP_UART_PARITY_ODD       // 奇校验
} bsp_uart_parity_t;

/* --- 2. 组合成配置结构体 --- */
typedef struct {
    uint32_t             baudrate;   // 波特率 (如 115200)
    bsp_uart_data_bits_t data_bits;  // 数据位
    bsp_uart_stop_bits_t stop_bits;  // 停止位
    bsp_uart_parity_t    parity;     // 校验位
} bsp_uart_config_t;

/* 
 * 定义接收回调函数指针类型 
 * BSP 层会在空闲中断(IDLE)触发时调用此函数，将接收到的一段数据抛给上层
 */
typedef void (*bsp_uart_rx_cb_t)(uint8_t *data, uint16_t len);

/* --- UART API --- */

/**
 * @brief 初始化指定的串口
 * @param port - 串口端口枚举 (如 BSP_UART_IMU1)
 * @param config - 串口配置结构体指针，包含波特率、数据位、停止位、校验位等参数
 * @note 该函数会根据配置结构体中的参数初始化对应的串口硬件。
 */
bool bsp_uart_init(bsp_uart_port_t port, const bsp_uart_config_t *config);

/**
 * @brief  初始化系统中所有的串口设备
 * @note   统一配置为: 115200bps, 8位数据, 1位停止, 无校验
 * 需要在 main 函数的硬件初始化阶段被调用
 */
void bsp_uart_init_default(void);

/**
 * @brief 启动 DMA 循环接收模式
 * @param port - 串口端口枚举 (如 BSP_UART_OPI)
 * @param buf_addr - 上层提供的接收缓冲区地址，DMA 将直接写入此地址
 * @param buf_size - 接收缓冲区大小，单位字节
 * @note 此模式下 DMA 会在接收缓冲区满后自动回绕到起始地址继续接收，适合持续数据流的场景，如 IMU 数据接收
 */
void bsp_uart_start_dma_rx_circular(bsp_uart_port_t port, uint8_t *buf_addr, uint16_t buf_size);

/**
 * @brief 启动 DMA 普通接收模式
 * @param port - 串口端口枚举 (如 BSP_UART_OPI)
 * @param buf_addr - 上层提供的接收缓冲区地址，DMA 将直接写入此地址
 * @param buf_size - 接收缓冲区大小，单位字节
 * @note 此模式下 DMA 在接收完指定长度的数据后会停止，适合分包数据接收的场景，如指令交互
 */
void bsp_uart_start_dma_rx_normal(bsp_uart_port_t port, uint8_t *buf_addr, uint16_t buf_size);

/**
 * @brief 获取 DMA 接收剩余空间长度
 * @param port - 串口端口枚举 (如 BSP_UART_OPI)
 * @return 当前 DMA 接收缓冲区剩余的可用空间长度，单位字节
 * @note 该函数通过读取 DMA 的数据计数器寄存器来计算剩余空间长度，适用于需要动态监控接收状态的场景。
 */
uint16_t bsp_uart_get_dma_rx_remaining(bsp_uart_port_t port);

/**
 * @brief 注册串口接收回调函数
 * @param port - 串口端口枚举 (如 BSP_UART_IMU1)
 * @param cb - 上层提供的回调函数指针，当串口接收到数据时会调用此函数将数据抛给上层
 * @note 该函数会将回调函数指针保存在 BSP 层的上下文结构体中，在串口的空闲中断服务程序中被调用，以实现数据的异步通知。
 */
void bsp_uart_register_rx_cb(bsp_uart_port_t port, bsp_uart_rx_cb_t cb);

/**
 * @brief 发送数据（CPU模式）
 * @param port - 串口端口枚举 (如 BSP_UART_OPI)
 * @param data - 待发送的数据缓冲区指针
 * @param len - 待发送的数据长度，单位字节
 * @note 此函数会通过轮询方式等待发送缓冲区空闲，并逐字节写入数据寄存器触发发送，适合偶尔发送少量数据的场景。
 */
void bsp_uart_send_buffer(bsp_uart_port_t port, const uint8_t *data, uint16_t len);

/**
 * @brief 发送数据（DMA模式）
 * @param port - 串口端口枚举 (如 BSP_UART_OPI)
 * @param data - 待发送的数据缓冲区指针
 * @param len - 待发送的数据长度，单位字节
 * @note 该函数会启动 DMA 并阻塞等待发送完成，适合 UART3 / UART4 这类管理通道。
 */
bool bsp_uart_send_dma(bsp_uart_port_t port, uint8_t *data, uint16_t len);

/**
 * @brief UART DMA TX 中断服务处理函数
 * @param port - 串口端口枚举
 * @note 由对应的 DMAx_Streamy_IRQHandler 调用，用于唤醒正在等待 DMA 完成的任务。
 */
void bsp_uart_dma_tx_isr_handler(bsp_uart_port_t port);

/**
 * @brief 串口中断服务程序
 * @param port - 串口端口枚举 (如 BSP_UART_IMU1)
 * @note 该函数会在串口接收中断（如 RXNE）或空闲中断（IDLE）触发时被调用，负责处理接收数据并通过回调函数通知上层。此函数需要在对应的 USARTx_IRQHandler 中被调用。
 */
void bsp_uart_isr_handler(bsp_uart_port_t port);

#endif

