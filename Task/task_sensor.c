#include <string.h>
#include "FreeRTOS.h"
#include "task.h"

#include "bsp_uart.h"
#include "bsp_delay.h"

#include "driver_ms5837.h"
#include "driver_power.h"
#include "driver_dht11.h"
#include "driver_imu.h"

#include "sys_data_pool.h" 
#include "sys_log.h" 

#include "task_sensor.h"


#define CABIN_HUMI_LEAK_THRESHOLD  85  // 舱内湿度大于 85% 判定为漏水预警

imu_data_t g_imu_body_frame[IMU_MAX_NUM];

/* ============================================================================
 * 任务句柄实体定义 (分配实际的内存空间)
 * ============================================================================ */
TaskHandle_t IMU_Task_Handler    = NULL;
TaskHandle_t MS5837_Task_Handler = NULL;
TaskHandle_t Power_Task_Handler  = NULL;
TaskHandle_t DHT11_Task_Handler  = NULL;

/* ============================================================================
 * 内部私有任务函数声明 (加 static 防止污染外部命名空间)
 * ============================================================================ */
static void vTask_IMU_Core(void *pvParameters);
static void vTask_MS5837_Core(void *pvParameters);
static void vTask_Power_Core(void *pvParameters);
static void vTask_DHT11_Core(void *pvParameters);

/* ============================================================================
 * 初始化接口：注册并创建所有的传感器任务
 * ============================================================================ */
void Task_Sensor_Init(void)
{
    // 创建 IMU 姿态解析任务
    xTaskCreate((TaskFunction_t ) vTask_IMU_Core,            // 任务核心函数
                (const char * ) "Task_IMU",                  // 任务名称(供调试用)
                (uint16_t       ) IMU_STK_SIZE,              // 任务堆栈大小(字)
                (void * ) NULL,                              // 传递给任务的参数
                (UBaseType_t    ) IMU_TASK_PRIO,             // 任务优先级
                (TaskHandle_t * ) &IMU_Task_Handler);        // 绑定任务句柄

    // 创建 MS5837 深度与水温任务
    xTaskCreate((TaskFunction_t ) vTask_MS5837_Core, 
                (const char * ) "Task_MS5837", 
                (uint16_t       ) MS5837_STK_SIZE, 
                (void * ) NULL, 
                (UBaseType_t    ) MS5837_TASK_PRIO, 
                (TaskHandle_t * ) &MS5837_Task_Handler);

    // 创建 Power 电源监控任务
    xTaskCreate((TaskFunction_t ) vTask_Power_Core, 
                (const char * ) "Task_Power", 
                (uint16_t       ) POWER_STK_SIZE, 
                (void * ) NULL, 
                (UBaseType_t    ) POWER_TASK_PRIO, 
                (TaskHandle_t * ) &Power_Task_Handler);

    // 创建 DHT11 舱内环境监控任务
    xTaskCreate((TaskFunction_t ) vTask_DHT11_Core, 
                (const char * ) "Task_DHT11", 
                (uint16_t       ) DHT11_STK_SIZE, 
                (void * ) NULL, 
                (UBaseType_t    ) DHT11_TASK_PRIO, 
                (TaskHandle_t * ) &DHT11_Task_Handler);
}

static void IMU_Align_To_BodyFrame(imu_data_t *imu)
{
    float temp;

    // --- 加速度映射 ---
    temp = imu->acc[0];
    imu->acc[0] = -imu->acc[1];  // AccX = -AccY_old
    imu->acc[1] = -temp;         // AccY = -AccX_old
    imu->acc[2] = -imu->acc[2];  // AccZ = -AccZ_old

    // --- 角速度映射 ---
    temp = imu->gyro[0];
    imu->gyro[0] = -imu->gyro[1];
    imu->gyro[1] = -temp;
    imu->gyro[2] = -imu->gyro[2];

    // --- 四元数映射 (内部规约: 0=W, 1=X, 2=Y, 3=Z) ---
    temp = imu->quat[0];
    imu->quat[0] =  imu->quat[3]; // W_new = Z_old
    imu->quat[1] =  imu->quat[1]; // X_new = X_old
    imu->quat[2] = -imu->quat[2]; // Y_new = -Y_old
    imu->quat[3] =  temp;         // Z_new = W_old
}

static void IMU_Fuse(const imu_data_t frames[], bot_body_state_t *out)
{
    if ((frames == NULL) || (out == NULL)) return;
    out->acc_x = frames[IMU_IM948].acc[0]; // 以 IM948 为主
    out->acc_y = frames[IMU_IM948].acc[1];
    out->acc_z = frames[IMU_IM948].acc[2];
    out->gyro_x = frames[IMU_IM948].gyro[0];
    out->gyro_y = frames[IMU_IM948].gyro[1];
    out->gyro_z = frames[IMU_IM948].gyro[2];
    memcpy(out->Quater, frames[IMU_JY901S].quat, sizeof(float) * 4);
}

/* =========================================================
 * 任务 1：传感器数据获取
 * 加速度、角速度、四元数 50Hz (20ms周期)
 * ========================================================= */

static void vTask_IMU_Core(void *pvParameters)
{
    imu_data_t data_jy901s;
    imu_data_t data_im948;
    bot_body_state_t fused_imu;
    uint8_t log_divider = 0;

    // 设定 20ms 的绝对轮询节拍 (50Hz)
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(20); 

    // 3. 实时闭环运行
    while (1)
    {
        // --- 轮询 JY901S ---
        uint16_t remain_1 = bsp_uart_get_dma_rx_remaining(BSP_UART_IMU2);
        Driver_IMU_Poll_DMA_Update(IMU_JY901S, remain_1);
        
        if (Driver_IMU_Process(IMU_JY901S, &data_jy901s) == true) 
        {
            // 坐标系转换 (解耦出来的纯逻辑操作)
            IMU_Align_To_BodyFrame(&data_jy901s);
            
            // 存入全局数组
            g_imu_body_frame[IMU_JY901S] = data_jy901s;
        }

        // --- 轮询 IM948 ---
        uint16_t remain_2 = bsp_uart_get_dma_rx_remaining(BSP_UART_IMU1);
        Driver_IMU_Poll_DMA_Update(IMU_IM948, remain_2);
        
        if (Driver_IMU_Process(IMU_IM948, &data_im948) == true) 
        {
            // 坐标系转换
            IMU_Align_To_BodyFrame(&data_im948);
            
            
            // 存入全局数组
            g_imu_body_frame[IMU_IM948] = data_im948;
        }
        IMU_Fuse(g_imu_body_frame, &fused_imu);
        Bot_State_Push_IMU(&fused_imu);
        if(++log_divider >= 50) // 每 50 次循环打印一次日志 (即每 1000ms)
        {
            LOG_INFO("IMU_JY901S Quat[0:%.2f 1:%.2f 2:%.2f 3:%.2f]", 
                      g_imu_body_frame[IMU_JY901S].quat[0], g_imu_body_frame[IMU_JY901S].quat[1], 
                      g_imu_body_frame[IMU_JY901S].quat[2], g_imu_body_frame[IMU_JY901S].quat[3]);
            
            LOG_INFO("IMU_IM948  Quat[0:%.2f 1:%.2f 2:%.2f 3:%.2f]", 
                      g_imu_body_frame[IMU_IM948].quat[0], g_imu_body_frame[IMU_IM948].quat[1], 
                      g_imu_body_frame[IMU_IM948].quat[2], g_imu_body_frame[IMU_IM948].quat[3]);
            log_divider = 0;
        }

        // 严格睡眠 20ms
        Bot_Task_CheckIn_Monitor(TASK_ID_IMU);
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}



/* =========================================================
 * 任务 2：MS5837 深度与水温高频任务
 * 架构优化：深度 50Hz (20ms周期)，温度 2.5Hz (每20次更新一次)
 * ========================================================= */
static void vTask_MS5837_Core(void *pvParameters)
{
    
    uint8_t log_divider = 0;
    
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
    Driver_Ms5837_Start_Temp_Conversion();
    vTaskDelay(pdMS_TO_TICKS(10)); 
    Driver_Ms5837_Read_Temp(&cached_temp);

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
                    Driver_Ms5837_Read_Temp(&cached_temp);
                    temp_state = 0; 
                }
                
                // 启动深度转换 (随后交出 CPU 10ms)
                Driver_Ms5837_Start_Pressure_Conversion();
                depth_state = 1;
                break;

            case 1:
                // 【后半拍】：经过了 10ms 睡眠，深度 ADC 肯定转换完了，读数据
                Driver_Ms5837_Read_Pressure_Depth(&press_val, &depth_val);
                
                Bot_State_Push_DepthTemp(depth_val, cached_temp);

                if (++log_divider >= 50) {
                    LOG_INFO("MS5837 - Depth: %.2fm, Temp: %.1f C", depth_val, cached_temp);
                    log_divider = 0;
                }

                depth_state = 0; // 重置深度状态
                temp_count++;    // 温度更新倒计时器递增

                // 如果够 20 次了，且总线空闲，启动一次温度转换
                if (temp_count >= 20)
                {
                    if (temp_state == 0) 
                    {
                        Driver_Ms5837_Start_Temp_Conversion();
                        temp_state = 2; // 标记为正在转换，下个 case 0 去读
                        temp_count = 0;
                    }
                }
                break;
        }

        Bot_Task_CheckIn_Monitor(TASK_ID_MS5837);

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
        v_val = Driver_Power_GetVoltage();
        c_val = Driver_Power_GetCurrent();

        Bot_State_Push_Power(v_val, c_val);

        LOG_INFO("Power - Vol: %.2fV, Cur: %.2fA", v_val, c_val);

        Bot_Task_CheckIn_Monitor(TASK_ID_POWER);
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
    bool is_already_leaking = false;

    while (1)
    {
        // 1. 发起通信并让出 CPU
        Driver_DHT11_Read_Data(&t_val, &h_val); 
        // vTaskDelay(pdMS_TO_TICKS(20)); 
        bsp_delay_ms(30); // DHT11 需要至少 18ms 的等待时间，给它充足的时间完成测量并准备好数据
        
        // 2. 读取成功
        if (Driver_DHT11_Read_Data(&t_val, &h_val) == 0) 
        {
            //拉取全局快照，看看其他传感器有没有触发过漏水
            Bot_State_LeakStatus_Pull(&is_already_leaking);
            
            //多重漏水融合判定 (核心逻辑！)
            // 如果原本就已经漏水了，或者现在的湿度超过了设定的死亡阈值，就判定为漏水
            bool is_now_leaking = is_already_leaking || (h_val >= CABIN_HUMI_LEAK_THRESHOLD);
            
            //把温湿度和融合后的漏水状态一起安全地推入池中
            Bot_State_Push_CabinEnv((float)t_val, (float)h_val, is_now_leaking);

            if (is_now_leaking) {
                LOG_WARNING("Cabin DHT11 - LEAK DETECTED! Temp: %d C, Humi: %d%%", t_val, h_val);
            } else {
                LOG_INFO( "Cabin DHT11 - Temp: %d C, Humi: %d%%, Safe.", t_val, h_val);
            }
        }

        // 3. 睡满 2 秒
        Bot_Task_CheckIn_Monitor(TASK_ID_DHT11);
        vTaskDelayUntil(&xLastWakeTime, xFrequency); 
    }
}
