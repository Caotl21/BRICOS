#ifndef _BSP_I2C_H_
#define _BSP_I2C_H_

#include "bsp_core.h"

/**
 * @brief  选择性批量初始化软件 I2C 总线
 * @param  bus_list 需要初始化的 I2C 总线枚举数组
 * @param  bus_num  数组中的元素个数
 * @retval true     全部初始化成功
 * @retval false    初始化失败（参数为空或枚举越界）
 */
bool bsp_i2c_init(bsp_i2c_bus_t *bus_list, uint8_t bus_num);

/**
 * @brief  高级抽象接口：向指定 I2C 总线的设备寄存器连续写入数据
 * @param  bus       I2C总线枚举 (如 BSP_I2C_1)
 * @param  dev_addr  I2C设备地址 (例如 MS5837 的 0xEC)
 * @param  reg_addr  寄存器地址
 * @param  data      要写入的数据指针
 * @param  len       数据长度
 * @retval true      写入成功
 * @retval false     写入失败 (未收到ACK)
 */
bool bsp_i2c_mem_write(bsp_i2c_bus_t bus, uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len);

/**
 * @brief  高级抽象接口：从指定 I2C 总线的设备寄存器连续读取数据
 * @param  bus       I2C总线枚举
 * @param  dev_addr  I2C设备地址
 * @param  reg_addr  寄存器地址
 * @param  data      数据存放指针
 * @param  len       要读取的长度
 * @retval true      读取成功
 * @retval false     读取失败
 */
bool bsp_i2c_mem_read(bsp_i2c_bus_t bus, uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len);

#endif /* _BSP_I2C_H_ */
