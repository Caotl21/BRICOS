#ifndef __DRIVER_DHT11_H
#define __DRIVER_DHT11_H

#include "bsp_core.h"

/**
 * @brief  初始化 DHT11 温湿度传感器
 * @note   该函数内部包含约 20ms 的阻塞延时以复位传感器。
 * 通常只在系统上电初始化阶段调用一次即可。
 * @retval 0: 初始化成功，检测到 DHT11
 * @retval 1: 初始化失败，未检测到 DHT11 响应
 */
uint8_t DHT11_Init(void);

/**
 * @brief  读取 DHT11 温湿度数据 (非阻塞状态机版)
 * * @warning 【重要调用说明】
 * 由于 DHT11 每次读取前需要主机拉低总线至少 18ms，为避免阻塞主程序，
 * 本函数采用了状态机设计，必须分为**两阶段调用**：
 * * - 第 1 次调用：函数会拉低引脚并立即返回 `1`。
 * - 等待阶段：   外部业务逻辑必须等待至少 20ms（可利用 RTOS 延时或定时器）。
 * - 第 2 次调用：函数会拉高引脚，与传感器握手并阻塞读取数据（耗时约 4~5ms）。
 * 读取完成并校验通过后，返回 `0`。
 * * @param  temp 指向存储温度值的变量指针 (范围: 0~50°C)
 * @param  humi 指向存储湿度值的变量指针 (范围: 20%~90% RH)
 * * @retval 0: 读取并校验成功，数据已更新
 * @retval 1: 正在等待复位延时(第1次调用后)，或者设备未响应、数据校验失败
 */
uint8_t DHT11_Read_Data(uint8_t *temp, uint8_t *humi);

#endif // __DRIVER_DHT11_H