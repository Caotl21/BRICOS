/**
 ******************************************************************************
 * @file    task_sensor.h
 * @author  BRICOS Architecture Team
 * @brief   系统感知层任务大纲与参数配置
 * @note    此文件统一定义了所有底层传感器任务的优先级、栈大小及对外开放的句柄。
 ******************************************************************************
 */

#ifndef __TASK_SENSOR_H
#define __TASK_SENSOR_H

#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"

/* ============================================================================
 * 任务资源分配宏定义 (数字越大优先级越高)
 * ============================================================================ */

/* 1. IMU 姿态解析任务 (双核异构并线解析，含大量浮点矩阵运算) */
#define IMU_TASK_PRIO       4
#define IMU_STK_SIZE        512  

/* 2. MS5837 深度与水温任务 (高频 I2C 时序调度) */
#define MS5837_TASK_PRIO    3
#define MS5837_STK_SIZE     256  

/* 3. Power 电源监控任务 (低频 ADC 采样) */
#define POWER_TASK_PRIO     2
#define POWER_STK_SIZE      128  

/* 4. DHT11 舱内温湿度与漏水辅助监控任务 (单总线时序) */
#define DHT11_TASK_PRIO     2
#define DHT11_STK_SIZE      256  

/* ============================================================================
 * 全局任务句柄声明 (外部可通过这些句柄操控任务运行状态)
 * ============================================================================ */
extern TaskHandle_t IMU_Task_Handler;
extern TaskHandle_t MS5837_Task_Handler;
extern TaskHandle_t Power_Task_Handler;
extern TaskHandle_t DHT11_Task_Handler;

/* ============================================================================
 * 对外开放的 API 接口
 * ============================================================================ */
/**
 * @brief  初始化并创建所有的传感器感知任务
 * @note   在 main 函数启动 FreeRTOS 调度器 (vTaskStartScheduler) 之前调用。
 */
void Task_Sensor_Init(void);

#endif /* __TASK_SENSOR_H */
