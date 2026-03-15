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

/* =======================================================
 * API：串口初始化
 * 参数：port - 串口端口枚举 (如 BSP_UART_IMU1)
 *     config - 串口配置结构体指针，包含波特率、数据位、停止位、校验位等参数
 * ======================================================= */
void bsp_uart_init(bsp_uart_port_t port, const bsp_uart_config_t *config);

/* =======================================================
 * API：启动 DMA 循环接收模式
 * 参数：port - 串口端口枚举 (如 BSP_UART_OPI)
 *     buf_addr - 上层提供的接收缓冲区地址，DMA 将直接写入此地址
 *     buf_size - 接收缓冲区大小，单位字节
 * 说明：此模式下 DMA 会在缓冲区满后自动回绕继续接收，适合持续数据流场景
 * ======================================================= */
void bsp_uart_start_dma_rx_circular(bsp_uart_port_t port, uint8_t *buf_addr, uint16_t buf_size);

/* =======================================================
 * API：启动 DMA 正常接收模式
 * 参数：port - 串口端口枚举 (如 BSP_UART_OPI)
 *     buf_addr - 上层提供的接收缓冲区地址，DMA 将直接写入此地址
 *     buf_size - 接收缓冲区大小，单位字节
 * 说明：此模式下 DMA 会按照顺序接收数据，适合小数据量传输场景
 * ======================================================= */
void bsp_uart_start_dma_rx_normal(bsp_uart_port_t port, uint8_t *buf_addr, uint16_t buf_size);

/* =======================================================
 * API：获取 DMA 当前剩余未接收的数据长度
 * 参数：port - 串口端口枚举 (如 BSP_UART_OPI)
 * 返回值：剩余未接收的数据长度（单位：字节）
 * ======================================================= */
uint16_t bsp_uart_get_dma_rx_remaining(bsp_uart_port_t port);

/* =======================================================
 * API：注册串口接收回调函数
 * 参数：port - 串口端口枚举 (如 BSP_UART_IMU1)
 *     cb - 上层提供的回调函数指针，BSP 层会在接收到数据时调用此函数
 * 说明：回调函数原型为 void cb(uint8_t *data, uint16_t len)，
 *     data 是接收到的数据指针，len 是数据长度
 * ======================================================= */
void bsp_uart_register_rx_cb(bsp_uart_port_t port, bsp_uart_rx_cb_t cb);

/* =======================================================
 * API：发送数据（阻塞模式）
 * 参数：port - 串口端口枚举 (如 BSP_UART_IMU1)
 *     data - 待发送的数据缓冲区指针
 *     len - 待发送的数据长度，单位字节
 * 说明：此函数会阻塞直到所有数据发送完成，适合小数据量传输场景
 * ======================================================= */
void bsp_uart_send_buffer(bsp_uart_port_t port, const uint8_t *data, uint16_t len);

/* =======================================================
 * API：发送数据（DMA 模式）
 * 参数：port - 串口端口枚举 (如 BSP_UART_OPI)
 *     data - 待发送的数据缓冲区指针
 *     len - 待发送的数据长度，单位字节
 * 返回值：true 表示成功启动 DMA 发送，false 表示当前无法启动 DMA 发送
 * 说明：此函数会尝试通过 DMA 发送数据，如果当前 DMA 正忙或不支持 DMA 发送，则返回 false
 * ======================================================= */
bool bsp_uart_send_dma(bsp_uart_port_t port, uint8_t *data, uint16_t len);

/* =======================================================
 * API：串口中断服务程序 (ISR) 处理函数
 * 参数：port - 串口端口枚举 (如 BSP_UART_IMU1)
 * 说明：此函数需要在对应串口的中断服务程序中被调用，以处理接收和空闲中断
 * ======================================================= */
void bsp_uart_isr_handler(bsp_uart_port_t port);

#endif // __BSP_UART_H