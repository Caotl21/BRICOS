#include "driver_dht11.h"
#include "bsp_core.h"
#include "bsp_gpio.h"
#include "bsp_delay.h"

/* =========================================
 * 宏定义与全局变量
 * ========================================= */

#define DHT11_DQ_OUT_1 bsp_gpio_write(BSP_GPIO_DHT11, true)
#define DHT11_DQ_OUT_0 bsp_gpio_write(BSP_GPIO_DHT11, false)
#define DHT11_DQ_IN    bsp_gpio_read(BSP_GPIO_DHT11)  

// 状态机标志位
uint8_t dht11_delay_flag = 0;

/* =========================================
 * 内部辅助函数：配置 GPIO 输入/输出模式
 * ========================================= */

static void DHT11_IO_OUT(void)
{
    // 调用 BSP 接口设置为输出模式（默认推挽）
    bsp_gpio_set_direction(BSP_GPIO_DHT11, true);
}

static void DHT11_IO_IN(void)
{
    // 调用 BSP 接口设置为输入模式（默认上拉）
    bsp_gpio_set_direction(BSP_GPIO_DHT11, false);
}

/* =========================================
 * 驱动核心函数
 * ========================================= */

// 阻塞式复位 DHT11 (通常仅用于初始化检查)
static void DHT11_Rst(void)       
{  
    DHT11_IO_OUT();       // SET OUTPUT
    DHT11_DQ_OUT_0;       // 拉低 DQ
    bsp_delay_ms(20);     // 拉低至少 18ms
    DHT11_DQ_OUT_1;       // DQ=1 
    bsp_delay_us(30);     // 主机拉高 20~40us
}

// 等待 DHT11 的回应
// 返回值：1:未检测到DHT11的存在, 0:存在
static uint8_t DHT11_Check(void)      
{   
    uint8_t retry = 0;
    DHT11_IO_IN(); // SET INPUT    
    
    // DHT11 会拉低 40~80us
    while (DHT11_DQ_IN && retry < 100) 
    {
        retry++;
        bsp_delay_us(1);
    }   
    if(retry >= 100) return 1;
    
    retry = 0;
    // DHT11 接着拉高 40~80us
    while (!DHT11_DQ_IN && retry < 100) 
    {
        retry++;
        bsp_delay_us(1);
    }
    if(retry >= 100) return 1;    
    
    return 0;
}

// 从 DHT11 读取一个位
// 返回值：1/0
static uint8_t DHT11_Read_Bit(void)             
{
    uint8_t retry = 0;
    
    // 等待变为低电平
    while(DHT11_DQ_IN && retry < 100) 
    {
        retry++;
        bsp_delay_us(1);
    }
    
    retry = 0;
    // 等待变高电平
    while(!DHT11_DQ_IN && retry < 100) 
    {
        retry++;
        bsp_delay_us(1);
    }
    
    // 等待 40us，判断高电平持续时间
    bsp_delay_us(40);
    
    if(DHT11_DQ_IN) return 1;
    else return 0;         
}

// 从 DHT11 读取一个字节
// 返回值：读到的数据
static uint8_t DHT11_Read_Byte(void)    
{        
    uint8_t i, dat = 0;
    for (i = 0; i < 8; i++) 
    {
        dat <<= 1; 
        dat |= DHT11_Read_Bit();
    }                           
    return dat;
}

// 从 DHT11 读取一次数据 (状态机非阻塞启动版)
// temp:温度值(范围:0~50°)
// humi:湿度值(范围:20%~90%)
// 返回值：0,正常; 1,读取失败或等待阶段
uint8_t DHT11_Read_Data(uint8_t *temp, uint8_t *humi)    
{        
    uint8_t buf[5];
    uint8_t i;
    
    // 状态机处理 20ms 的启动延时
    switch(dht11_delay_flag)
    {
        case 0:
            DHT11_IO_OUT();     // SET OUTPUT
            DHT11_DQ_OUT_0;     // 拉低 DQ
            dht11_delay_flag = 1;
            // 注意：此处直接返回 1。调用者必须在约 20ms 后再次调用此函数！
            return 1; 
            
        case 1:
            DHT11_DQ_OUT_1;     // DQ=1 
            bsp_delay_us(30);   // 主机拉高 20~40us 等待 DHT11 响应
            dht11_delay_flag = 0; // 重置状态，进入数据读取流程
            break;
    }
    
    // 检查响应并读取数据
    if(DHT11_Check() == 0)
    {
        for(i = 0; i < 5; i++) // 读取 40 位数据
        {
            buf[i] = DHT11_Read_Byte();
        }
        
        // 校验和验证
        if((buf[0] + buf[1] + buf[2] + buf[3]) == buf[4])
        {
            *humi = buf[0];
            *temp = buf[2];
            return 0; // 校验成功，返回 0
        }
        else 
        {
            return 1; // 校验失败，返回错误码 1
        }
    }
    else return 1; // 设备未响应，返回错误码 1
}

// 初始化 DHT11
// 返回值：1:不存在; 0:存在         
uint8_t DHT11_Init(void)
{               
    DHT11_Rst();          // 阻塞式复位 DHT11，确保启动时传感器就绪
    return DHT11_Check(); // 等待 DHT11 的回应
}