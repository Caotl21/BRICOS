#include "bsp_i2c.h"
#include "stm32f4xx.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_rcc.h"
#include "bsp_delay.h" 

/* ==========================================
 * 1. 定义软件 I2C 硬件信息字典 (数据驱动核心)
 * ========================================== */
typedef struct {
    uint32_t      scl_rcc;
    GPIO_TypeDef* scl_port;
    uint16_t      scl_pin;
    
    uint32_t      sda_rcc;
    GPIO_TypeDef* sda_port;
    uint16_t      sda_pin;
} i2c_hw_info_t;

/* 实例化映射表，加 const 存入 Flash 不占 RAM，加 static 限制作用域 */
static const i2c_hw_info_t s_i2c_hw_info[BSP_I2C_MAX] = {
    [BSP_I2C_MS5837] = {
        .scl_rcc  = RCC_AHB1Periph_GPIOB,
        .scl_port = GPIOB,
        .scl_pin  = GPIO_Pin_6,
        .sda_rcc  = RCC_AHB1Periph_GPIOB,
        .sda_port = GPIOB,
        .sda_pin  = GPIO_Pin_7
    }
    // 如果未来有 BSP_I2C_2，直接在这里加一条配置即可，下面的逻辑代码一行都不用改！
};

/* ==========================================
 * 2. 内部静态内联函数，极致压缩函数调用开销
 * ========================================== */
static inline void i2c_scl_write(bsp_i2c_bus_t bus, uint8_t bit_val) {
    GPIO_WriteBit(s_i2c_hw_info[bus].scl_port, s_i2c_hw_info[bus].scl_pin, (BitAction)bit_val);
    bsp_delay_us(2);
}

static inline void i2c_sda_write(bsp_i2c_bus_t bus, uint8_t bit_val) {
    GPIO_WriteBit(s_i2c_hw_info[bus].sda_port, s_i2c_hw_info[bus].sda_pin, (BitAction)bit_val);
    bsp_delay_us(2);
}

static inline uint8_t i2c_sda_read(bsp_i2c_bus_t bus) {
    uint8_t bit_val = GPIO_ReadInputDataBit(s_i2c_hw_info[bus].sda_port, s_i2c_hw_info[bus].sda_pin);
    bsp_delay_us(2);
    return bit_val;
}

/* ==========================================
 * 3. 核心协议原语 (全部增加 bus 参数)
 * ========================================== */

bool bsp_i2c_init(bsp_i2c_bus_t *bus_list, uint8_t bus_num)
{
    // ================== 顶层失败情况排查 ==================
    if (bus_list == NULL || bus_num == 0) {
        return false;
    }

    GPIO_InitTypeDef GPIO_InitStructure;
    
    // 提取所有 I2C 引脚通用的 GPIO 配置参数
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    // 软件I2C必须配置为开漏输出 (Open-Drain)，配合外部上拉电阻
    GPIO_InitStructure.GPIO_OType = GPIO_OType_OD; 
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;

    // ================== 遍历上层传入的数组 ==================
    for (uint8_t i = 0; i < bus_num; i++) {
        bsp_i2c_bus_t bus = bus_list[i];
        
        // 单个总线枚举安全校验（防止越界导致查表时发生内存溢出）
        if (bus >= BSP_I2C_MAX) {
            return false;
        }

        // 从硬件字典中获取当前指定的总线配置
        const i2c_hw_info_t* hw = &s_i2c_hw_info[bus];
        
        // 开启时钟
        RCC_AHB1PeriphClockCmd(hw->scl_rcc | hw->sda_rcc, ENABLE);
        
        // 初始化 SCL
        GPIO_InitStructure.GPIO_Pin = hw->scl_pin;
        GPIO_Init(hw->scl_port, &GPIO_InitStructure);
        
        // 初始化 SDA
        GPIO_InitStructure.GPIO_Pin = hw->sda_pin;
        GPIO_Init(hw->sda_port, &GPIO_InitStructure);
        
        // 初始化释放总线 (拉高)，I2C 空闲时必须为高电平
        GPIO_SetBits(hw->scl_port, hw->scl_pin);
        GPIO_SetBits(hw->sda_port, hw->sda_pin);
    }
    
    return true; // 全部初始化完成
}


// 产生起始信号：SCL高电平时，SDA由高变低
void bsp_i2c_start(bsp_i2c_bus_t bus) {
    i2c_sda_write(bus, 1);
    i2c_scl_write(bus, 1);
    i2c_sda_write(bus, 0); 
    i2c_scl_write(bus, 0); // 钳住总线，准备收发
}

// 产生停止信号：SCL高电平时，SDA由低变高
void bsp_i2c_stop(bsp_i2c_bus_t bus) {
    i2c_sda_write(bus, 0);
    i2c_scl_write(bus, 1);
    i2c_sda_write(bus, 1); 
}

// 主机产生应答(ACK)：SDA拉低
void bsp_i2c_ack(bsp_i2c_bus_t bus) {
    i2c_sda_write(bus, 0);
    i2c_scl_write(bus, 1);
    i2c_scl_write(bus, 0);
}

// 主机产生非应答(NACK)：SDA拉高
void bsp_i2c_nack(bsp_i2c_bus_t bus) {
    i2c_sda_write(bus, 1);
    i2c_scl_write(bus, 1);
    i2c_scl_write(bus, 0);
}

// 等待从机应答
uint8_t bsp_i2c_wait_ack(bsp_i2c_bus_t bus) {
    uint8_t ack_bit;
    i2c_sda_write(bus, 1); // 主机释放SDA线
    i2c_scl_write(bus, 1);
    ack_bit = i2c_sda_read(bus); // 读回应答位 (0:成功, 1:失败)
    i2c_scl_write(bus, 0);
    return ack_bit; 
}

// 发送一个字节 (MSB 高位先发)
bool bsp_i2c_send_byte(bsp_i2c_bus_t bus, uint8_t byte) {
    for (uint8_t i = 0; i < 8; i++) {
        i2c_sda_write(bus, (byte & (0x80 >> i)) ? 1 : 0);
        i2c_scl_write(bus, 1);
        i2c_scl_write(bus, 0);
    }
    // 等待应答，0 代表收到 ACK (成功)，返回 true
    return (bsp_i2c_wait_ack(bus) == 0);
}

// 读取一个字节 (MSB 高位先收)
uint8_t bsp_i2c_read_byte(bsp_i2c_bus_t bus, uint8_t ack) {
    uint8_t byte = 0x00;
    i2c_sda_write(bus, 1); // 主机释放SDA线，交由从机控制
    
    for (uint8_t i = 0; i < 8; i++) {
        i2c_scl_write(bus, 1);
        if (i2c_sda_read(bus) == 1) {
            byte |= (0x80 >> i);
        }
        i2c_scl_write(bus, 0);
    }
    
    // 根据参数决定给从机回 ACK 还是 NACK
    if (ack) bsp_i2c_ack(bus);
    else     bsp_i2c_nack(bus);
    
    return byte;
}

/* ==========================================
 * 4. 高级读写封装 (暴露给上层的 API)
 * ========================================== */

// 连续写寄存器
bool bsp_i2c_mem_write(bsp_i2c_bus_t bus, uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len)
{
    if (bus >= BSP_I2C_MAX) return false;

    bsp_i2c_start(bus);   
    // 每发一个字节都检查 ACK，只要有一次 NACK 就终止并返回 false
    if (!bsp_i2c_send_byte(bus, dev_addr)) { bsp_i2c_stop(bus); return false; } // 发送设备地址 (默认最低位为0，即写操作)
    if (!bsp_i2c_send_byte(bus, reg_addr)) { bsp_i2c_stop(bus); return false; } // 发送目标寄存器地址
    for (uint16_t i = 0; i < len; i++) {
        if (!bsp_i2c_send_byte(bus, data[i])) { 
            bsp_i2c_stop(bus); 
            return false;
        }
    }
    bsp_i2c_stop(bus);
    return true;
}

// 连续读寄存器
bool bsp_i2c_mem_read(bsp_i2c_bus_t bus, uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len)
{
    if (bus >= BSP_I2C_MAX || len == 0) return false;
    
    bsp_i2c_start(bus);      
    
    if (!bsp_i2c_send_byte(bus, dev_addr)) { bsp_i2c_stop(bus); return false; } // 发送设备地址 (假写，用于定位寄存器)
    if (!bsp_i2c_send_byte(bus, reg_addr)) { bsp_i2c_stop(bus); return false; } // 发送要读取的寄存器地址

    bsp_i2c_start(bus);                      // 产生 Restart (重复起始) 信号
    if (!bsp_i2c_send_byte(bus, dev_addr | 0x01)) { bsp_i2c_stop(bus); return false; } // 发送设备地址，强制拉高最低位 (转为读操作)
    for (uint16_t i = 0; i < len - 1; i++) {
        data[i] = bsp_i2c_read_byte(bus, 1); // 还没读完时，每读一个字节回一个 ACK
    }
    data[len - 1] = bsp_i2c_read_byte(bus, 0); // 读最后一个字节时，必须回 NACK 通知从机停止发送
    
    bsp_i2c_stop(bus);
    return true;
}

