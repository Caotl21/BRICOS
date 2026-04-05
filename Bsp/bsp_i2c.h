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
 * @brief  基础协议原语：产生 I2C 起始信号 (Start)
 * @note   SCL 高电平期间，SDA 发生由高到低的跳变
 * @param  bus I2C总线枚举 (如 BSP_I2C_MS5837)
 */
void bsp_i2c_start(bsp_i2c_bus_t bus);

/**
 * @brief  基础协议原语：产生 I2C 停止信号 (Stop)
 * @note   SCL 高电平期间，SDA 发生由低到高的跳变
 * @param  bus I2C总线枚举
 */
void bsp_i2c_stop(bsp_i2c_bus_t bus);

/**
 * @brief  基础协议原语：主机产生应答信号 (ACK)
 * @note   在第 9 个时钟周期，主机拉低 SDA 告知从机继续发送
 * @param  bus I2C总线枚举
 */
void bsp_i2c_ack(bsp_i2c_bus_t bus);

/**
 * @brief  基础协议原语：主机产生非应答信号 (NACK)
 * @note   在第 9 个时钟周期，主机释放 SDA (拉高) 告知从机停止发送
 * @param  bus I2C总线枚举
 */
void bsp_i2c_nack(bsp_i2c_bus_t bus);

/**
 * @brief  基础协议原语：等待从机应答
 * @param  bus I2C总线枚举
 * @retval 0   成功接收到从机应答 (ACK)
 * @retval 1   未接收到从机应答或超时 (NACK)
 */
uint8_t bsp_i2c_wait_ack(bsp_i2c_bus_t bus);

/**
 * @brief  基础协议原语：向总线发送一个字节数据
 * @note   高位 (MSB) 优先发送，发送完毕后会自动等待从机应答
 * @param  bus  I2C总线枚举
 * @param  byte 要发送的 8 位数据
 * @retval true 发送成功并收到从机 ACK
 * @retval false 发送失败 (未收到从机 ACK)
 */
bool bsp_i2c_send_byte(bsp_i2c_bus_t bus, uint8_t byte);

/**
 * @brief  基础协议原语：从总线读取一个字节数据
 * @note   高位 (MSB) 优先接收
 * @param  bus I2C总线枚举
 * @param  ack 决定接收完毕后主机的动作 (1: 发送 ACK 继续读, 0: 发送 NACK 停止读)
 * @retval uint8_t 读取到的 8 位数据
 */
uint8_t bsp_i2c_read_byte(bsp_i2c_bus_t bus, uint8_t ack);
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
