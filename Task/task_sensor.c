#include "task_sensor.h"
#include "FreeRTOS.h"
#include "task.h"
#include "driver_ms5837.h"
#include "driver_power.h"
#include "driver_dht11.h"
#include "bot_data_pool.h" // 引入全局数据池 API

#define CABIN_HUMI_LEAK_THRESHOLD  85  // 舱内湿度大于 85% 判定为漏水预警

/* =========================================================
 * 任务 2：MS5837 深度与水温高频任务
 * 架构优化：深度 50Hz (20ms周期)，温度 2.5Hz (每20次更新一次)
 * ========================================================= */
static void vTask_MS5837_Core(void *pvParameters)
{
    // 1. 初始化传感器
    Ms5837_Init(); 

    // 局部缓冲与状态变量
    float cached_temp = 0.0f;
    float press_val = 0.0f;
    float depth_val = 0.0f;
    
    uint8_t depth_state = 0;
    uint8_t temp_state = 0;
    uint8_t temp_count = 0;

    // ---------------------------------------------------------
    // 2. 初始温度强行获取 (系统刚上电，必须等)
    // ---------------------------------------------------------
    Ms5837_Start_Temp_Conversion();
    vTaskDelay(pdMS_TO_TICKS(10)); 
    Ms5837_Read_Temp(&cached_temp);

    // 设置基础任务节拍：10ms = 100Hz
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(10); 

    // ---------------------------------------------------------
    // 3. 进入 100Hz 的状态机大循环
    // ---------------------------------------------------------
    while (1)
    {
        switch (depth_state)
        {
            case 0:
                // 【前半拍】：如果温度转换完成了，趁现在 I2C 空闲赶紧读回来
                if (temp_state == 2)
                {
                    Ms5837_Read_Temp(&cached_temp);
                    temp_state = 0; 
                }
                
                // 启动深度转换 (随后交出 CPU 10ms)
                Ms5837_Start_Pressure_Conversion();
                depth_state = 1;
                break;

            case 1:
                // 【后半拍】：经过了 10ms 睡眠，深度 ADC 肯定转换完了，读数据
                Ms5837_Read_Pressure_Depth(&press_val, &depth_val);
                
                // ★ 每次完整读取深度后，立刻推入数据池！(严格的 50Hz 写入)
                Bot_State_Push_DepthTemp(depth_val, cached_temp);

                depth_state = 0; // 重置深度状态
                temp_count++;    // 温度更新倒计时器递增

                // 如果够 20 次了，且总线空闲，启动一次温度转换
                if (temp_count >= 20)
                {
                    if (temp_state == 0) 
                    {
                        Ms5837_Start_Temp_Conversion();
                        temp_state = 2; // 标记为正在转换，下个 case 0 去读
                        temp_count = 0;
                    }
                }
                break;
        }

        // 严格的 10ms 节拍推进
        vTaskDelayUntil(&xLastWakeTime, xFrequency); 
    }
}
/* =========================================================
 * 任务 3：电源电压电流监控任务 (严格周期: 5000ms)
 * ========================================================= */
static void vTask_Power_Core(void *pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(5000);

    float v_val = 0.0f;
    float c_val = 0.0f;

    while (1)
    {
        // 1. 读取 ADC
        v_val = Power_GetVoltage();
        c_val = Power_GetCurrent();

        // 2. 推送至数据池
        Bot_State_Push_Power(v_val, c_val);

        // 3. 深度睡眠 5000ms
        vTaskDelayUntil(&xLastWakeTime, xFrequency); 
    }
}

/* =========================================================
 * 任务 4：DHT11 舱内温湿度任务 (环境安全监控)
 * 升级：加入了基于高湿度的辅助漏水预警判断
 * ========================================================= */
static void vTask_DHT11_Core(void *pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(2000);

    uint8_t t_val = 0;
    uint8_t h_val = 0;
    
    bot_state_t current_state; 

    while (1)
    {
        // 1. 发起通信并让出 CPU
        DHT11_Read_Data(&t_val, &h_val); 
        vTaskDelay(pdMS_TO_TICKS(20)); 
        
        // 2. 读取成功
        if (DHT11_Read_Data(&t_val, &h_val) == 0) 
        {
            // ★ 第一步：拉取全局快照，看看其他传感器有没有触发过漏水
            Bot_State_Pull(&current_state);
            
            // ★ 第二步：多重漏水融合判定 (核心逻辑！)
            // 如果原本就已经漏水了，或者现在的湿度超过了设定的死亡阈值，就判定为漏水
            bool is_now_leaking = current_state.is_leak_detected || (h_val >= CABIN_HUMI_LEAK_THRESHOLD);
            
            // 第三步：把温湿度和融合后的漏水状态一起安全地推入池中
            Bot_State_Push_CabinEnv((float)t_val, (float)h_val, is_now_leaking);
        }

        // 3. 睡满 2 秒
        vTaskDelayUntil(&xLastWakeTime, xFrequency); 
    }
}

/* =========================================================
 * 初始化接口：注册传感器任务
 * ========================================================= */
void Task_Sensor_Init(void)
{
    // 因为你在 Bot_Data_Pool 中使用了更底层的 SYS_ENTER_CRITICAL()
    
    xTaskCreate(vTask_MS5837_Core, "Task_MS5837", 256, NULL, 3, NULL);
    xTaskCreate(vTask_Power_Core,  "Task_Power",  128, NULL, 3, NULL);
    xTaskCreate(vTask_DHT11_Core,  "Task_DHT11",  128, NULL, 3, NULL);
}