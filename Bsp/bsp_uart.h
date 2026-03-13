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
// 初始化串口 只配置时钟、引脚、波特率
void bsp_uart_init(bsp_uart_port_t port, const bsp_uart_config_t *config);

// 启动 DMA 循环接收模式 零拷贝处理接收数据
// 驱动层调用此函数 将自己的缓冲区地址传给底层
void bsp_uart_start_dma_rx_circular(bsp_uart_port_t port, uint8_t *buf_addr, uint16_t buf_size);

// 启动 DMA 正常接收模式 零拷贝处理接收数据
void bsp_uart_start_dma_rx_normal(bsp_uart_port_t port, uint8_t *buf_addr, uint16_t buf_size);

// 获取DMA当前剩余未接收的数据长度（单位：字节），用于上层计算实际收到的数据长度
uint16_t bsp_uart_get_dma_rx_remaining(bsp_uart_port_t port);

// 注册串口接收完成（空闲中断）的回调函数
void bsp_uart_register_rx_cb(bsp_uart_port_t port, bsp_uart_rx_cb_t cb);

// 触发阻塞式发送数据（仅适用于小数据量）
void bsp_uart_send_buffer(bsp_uart_port_t port, const uint8_t *data, uint16_t len);

// 触发 DMA 非阻塞发送数据
bool bsp_uart_send_dma(bsp_uart_port_t port, uint8_t *data, uint16_t len);

// 供 stm32f4xx_it.c 中的硬件中断函数调用
void bsp_uart_isr_handler(bsp_uart_port_t port);

#endif // __BSP_UART_H