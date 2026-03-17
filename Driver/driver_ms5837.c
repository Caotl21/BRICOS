#include "driver_ms5837.h"
#include "bsp_core.h"
#include "bsp_i2c.h"
#include "bsp_delay.h"

// MS5837 I2C 设备写地址 (8-bit)
#define MS5837_ADDR 0xEC

// MS5837 命令宏
#define MS5837_CMD_RESET      0x1E
#define MS5837_CMD_ADC_READ   0x00
#define MS5837_CMD_PROM_READ  0xA0
#define MS5837_CMD_CONV_D1    0x48 // D1 压力转换，OSR=256
#define MS5837_CMD_CONV_D2    0x58 // D2 温度转换，OSR=256

/* =========================================
 * 内部全局变量
 * ========================================= */
static uint16_t cal_c[7];               // PROM 出厂校准数据 C0-C6
static float atmosphere_pressure = 985.0f; // 大气压初始基准，上电第一次读取时修正
static bool is_first_read = true;       // 首次读取标志

// MS5837 内部计算变量（因为压力解算强依赖温度转化后的偏差，必须存为全局）
static int64_t ms5837_dT   = 0;
static int64_t ms5837_OFF  = 0;
static int64_t ms5837_SENS = 0;


/* =========================================
 * 驱动核心函数
 * ========================================= */
static bool readFromMs5837(uint8_t reg_addr, uint16_t *data)
{
    uint8_t inth, intl;

    // ============ 第一阶段：发送读 PROM 命令 ============
    bsp_i2c_start(BSP_I2C_MS5837);
    
    // 发送设备写地址，若 NACK 则报错退出
    if (!bsp_i2c_send_byte(BSP_I2C_MS5837, MS5837_ADDR)) {
        bsp_i2c_stop(BSP_I2C_MS5837);
        return false;
    }
    // 发送目标寄存器地址
    if (!bsp_i2c_send_byte(BSP_I2C_MS5837, reg_addr)) {
        bsp_i2c_stop(BSP_I2C_MS5837);
        return false;
    }
    
    // 强制发送 Stop，释放总线
    bsp_i2c_stop(BSP_I2C_MS5837);

    // 核心等待：给传感器留出时间把数据从 PROM 搬到输出移位寄存器
    bsp_delay_us(5);

    // ============ 第二阶段：读取数据 ============
    bsp_i2c_start(BSP_I2C_MS5837);
    
    // 发送设备读地址 (0xEC | 0x01 = 0xED)
    if (!bsp_i2c_send_byte(BSP_I2C_MS5837, MS5837_ADDR | 0x01)) { 
        bsp_i2c_stop(BSP_I2C_MS5837);
        return false;
    }

    // 读高字节，回 ACK (参数 1) 继续读
    inth = bsp_i2c_read_byte(BSP_I2C_MS5837, 1); 
    
    // 读低字节，回 NACK (参数 0) 结束读
    intl = bsp_i2c_read_byte(BSP_I2C_MS5837, 0); 
    
    // 结束通信
    bsp_i2c_stop(BSP_I2C_MS5837);

    // 拼接成 16 位并返回
    if (data != NULL) {
        *data = ((uint16_t)inth << 8) | intl;
    }

    return true;
}

bool Ms5837_Init(void)
{
    // 0. 初始化底层 I2C 硬件管脚和时钟
    bsp_i2c_bus_t i2c_bus_list[] = {BSP_I2C_MS5837};
    if (!bsp_i2c_init(i2c_bus_list, 1)) {
        return false; // I2C 底层初始化失败
    }   
    
    // 1. 发送复位指令 0x1E (无数据传输，通过 reg_addr 传命令)
    if (!bsp_i2c_mem_write(BSP_I2C_MS5837, MS5837_ADDR, MS5837_CMD_RESET, NULL, 0)) {
        return false;
    }
    
    // 传感器内部重载 PROM 需要一定时间，手册建议 3ms，这里给 20ms 保证稳定
    bsp_delay_ms(20); 

    // 2. 依次读取 7 个字（14字节）的 PROM 数据 (0xA0 ~ 0xAE)
     for (uint8_t i = 0; i <= 6; i++) 
    {
        // 调用刚刚封装好的专用函数！
        if (!readFromMs5837(0xA0 + (i * 2), &cal_c[i])) {
            return false; // 如果任何一次读取失败，直接返回初始化失败
        }
    }
    
    is_first_read = true; // 复位基准压力标志
    return true;
}

void Ms5837_Start_Temp_Conversion(void)
{
    bsp_i2c_mem_write(BSP_I2C_MS5837, MS5837_ADDR, MS5837_CMD_CONV_D2, NULL, 0);
}

bool Ms5837_Read_Temp(float *out_temp)
{
    uint8_t buf[3];
    int32_t TEMP;
    
    // 读取 ADC 转换结果
    if (!bsp_i2c_mem_read(BSP_I2C_MS5837, MS5837_ADDR, MS5837_CMD_ADC_READ, buf, 3)) {
        return false;
    }
    
    uint32_t D2 = ((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 8) | buf[2];

    /* --- 一阶温度补偿运算 (使用 int64_t 自然处理负数，拒绝繁琐的分支) --- */
    ms5837_dT = (int64_t)D2 - ((int64_t)cal_c[5] << 8); 
    TEMP = 2000 + (int32_t)((ms5837_dT * (int64_t)cal_c[6]) / 8388608LL);

    ms5837_OFF  = ((int64_t)cal_c[2] << 16) + (((int64_t)cal_c[4] * ms5837_dT) / 128LL);
    ms5837_SENS = ((int64_t)cal_c[1] << 15) + (((int64_t)cal_c[3] * ms5837_dT) / 256LL);

    /* --- 二阶温度补偿运算 (针对极低温和高温的修正) --- */
    int64_t Ti = 0, OFFi = 0, SENSi = 0;
    
    if (TEMP < 2000) // 低于 20°C
    {
        Ti = (3LL * ms5837_dT * ms5837_dT) / 8589934592LL;
        int64_t aux = ((int64_t)TEMP - 2000) * ((int64_t)TEMP - 2000);
        OFFi = (3LL * aux) / 2LL;
        SENSi = (5LL * aux) / 8LL;
    }
    else // 高于或等于 20°C
    {
        Ti = (2LL * ms5837_dT * ms5837_dT) / 137438953472LL;
        int64_t aux = ((int64_t)TEMP - 2000) * ((int64_t)TEMP - 2000);
        OFFi = aux / 16LL;
        SENSi = 0;
    }

    // 应用二阶补偿值
    TEMP -= Ti;
    ms5837_OFF -= OFFi;
    ms5837_SENS -= SENSi;

    // 输出最终温度
    if (out_temp) {
        *out_temp = (float)TEMP / 100.0f;
    }
    return true;
}

void Ms5837_Start_Pressure_Conversion(void)
{
    bsp_i2c_mem_write(BSP_I2C_MS5837, MS5837_ADDR, MS5837_CMD_CONV_D1, NULL, 0);
}

bool Ms5837_Read_Pressure_Depth(float *out_press, float *out_depth)
{
    uint8_t buf[3];
    
    // 读取 ADC 转换结果
    if (!bsp_i2c_mem_read(BSP_I2C_MS5837, MS5837_ADDR, MS5837_CMD_ADC_READ, buf, 3)) {
        return false;
    }
    
    uint32_t D1 = ((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 8) | buf[2];

    /* --- 压力运算 (基于前面获取的全局变量 OFF 和 SENS) --- */
    // P = (D1 * SENS / 2097152 - OFF) / 8192
    int64_t p_calc = (((int64_t)D1 * ms5837_SENS) / 2097152LL - ms5837_OFF) / 8192LL;
    float press_mbar = (float)p_calc / 10.0f;

    /* --- 初始化时记录大气压基准 --- */
    if (is_first_read) 
    {
        atmosphere_pressure = press_mbar;
        is_first_read = false;
    }

    // 赋值输出气压
    if (out_press) {
        *out_press = press_mbar;
    }
    
    // 计算输出水深 (cm)
    // 根据流体力学 P = ρgh，在此简化系数为 0.983615
    if (out_depth) {
        *out_depth = (press_mbar - atmosphere_pressure) / 0.983615f; 
    }
    
    return true;
}