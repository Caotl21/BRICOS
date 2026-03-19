#ifndef __DRIVER_MS5837_H
#define __DRIVER_MS5837_H

#include "bsp_core.h"

/**
 * @brief  初始化 MS5837 深度传感器
 * @note   该函数会发送 I2C 复位指令，并读取出厂 PROM 校准系数。
 * 内部包含了 20ms 的复位等待延时。
 * @retval true  初始化并读取校验数据成功
 * @retval false 传感器无响应或总线错误
 */
bool Driver_Ms5837_Init(void);

/* =========================================================================
 * 状态机非阻塞接口：
 * 因为 MS5837 内部 ADC 执行转换需要时间，为了不阻塞主循环，调用顺序必须为：
 * * 1. Ms5837_Start_Temp_Conversion()       // 发起温度转换
 * 2. 延时/等待 (>= 2ms ~ 10ms，取决于 OSR 精度)
 * 3. Ms5837_Read_Temp(&temp)              // 读取并计算温度
 * 4. Ms5837_Start_Pressure_Conversion()   // 发起压力转换
 * 5. 延时/等待 (>= 2ms ~ 10ms)
 * 6. Ms5837_Read_Pressure_Depth(&p, &d)   // 读取并解算压力与深度
 * * 注意：压力解算严格依赖紧挨着它上一步读取出的温度校准偏差变量！
 * ========================================================================= */

/**
 * @brief  发送温度转换指令 (D2)
 * @note   发送后需要等待至少 2~10ms (取决于内部 OSR 宏定义配置) 才能读取
 */
void Driver_Ms5837_Start_Temp_Conversion(void);

/**
 * @brief  读取温度数据并进行一阶/二阶温度补偿
 * @param  out_temp 指向存放温度结果(摄氏度 °C)的指针
 * @retval true  读取并计算成功
 * @retval false 读取失败 (总线错误)
 */
bool Driver_Ms5837_Read_Temp(float *out_temp);

/**
 * @brief  发送压力转换指令 (D1)
 * @note   发送后需要等待至少 2~10ms 才能读取
 */
void Driver_Ms5837_Start_Pressure_Conversion(void);

/**
 * @brief  读取压力与深度数据 
 * @note   必须在 Driver_Ms5837_Read_Temp() 成功执行之后调用，因为深度计算强依赖温度补偿系数
 * @param  out_press 指向存放气压/水压结果(mbar)的指针 (可传 NULL)
 * @param  out_depth 指向存放水深结果(cm)的指针 (可传 NULL)
 * @retval true  读取并解算成功
 * @retval false 读取失败
 */
bool Driver_Ms5837_Read_Pressure_Depth(float *out_press, float *out_depth);

#endif // __DRIVER_MS5837_H
