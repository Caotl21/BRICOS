// #include "driver_imu.h"
// #include "bsp_uart.h"
// #include "sys_data_pool.h"
// #include "FreeRTOS.h"
// #include "task.h"

// #include "sys_log.h" // 引入日志系统

// imu_data_t g_imu_body_frame[IMU_MAX_NUM];

// static void IMU_Align_To_BodyFrame(imu_data_t *imu)
// {
//     float temp;

//     // --- 加速度映射 ---
//     temp = imu->acc[0];
//     imu->acc[0] = -imu->acc[1];  // AccX = -AccY_old
//     imu->acc[1] = -temp;         // AccY = -AccX_old
//     imu->acc[2] = -imu->acc[2];  // AccZ = -AccZ_old

//     // --- 角速度映射 ---
//     temp = imu->gyro[0];
//     imu->gyro[0] = -imu->gyro[1];
//     imu->gyro[1] = -temp;
//     imu->gyro[2] = -imu->gyro[2];

//     // --- 四元数映射 (内部规约: 0=W, 1=X, 2=Y, 3=Z) ---
//     temp = imu->quat[0];
//     imu->quat[0] =  imu->quat[3]; // W_new = Z_old
//     imu->quat[1] =  imu->quat[1]; // X_new = X_old
//     imu->quat[2] = -imu->quat[2]; // Y_new = -Y_old
//     imu->quat[3] =  temp;         // Z_new = W_old
// }


// void vTask_IMU_Core(void *pvParameters)
// {
//     imu_data_t data_jy901s;
//     imu_data_t data_im948;
//     uint8_t log_divider = 0;

//     //唤醒传感器、配置参数
//     Driver_IMU_Init();

//     //绑定底层 DMA 管道
//     bsp_uart_start_dma_rx_circular(BSP_UART_IMU1, Driver_IMU_GetRxBuf(IMU_JY901S), Driver_IMU_GetBufSize(IMU_JY901S));
//     bsp_uart_start_dma_rx_circular(BSP_UART_IMU2, Driver_IMU_GetRxBuf(IMU_IM948),  Driver_IMU_GetBufSize(IMU_IM948));

//     // 设定 20ms 的绝对轮询节拍 (50Hz)
//     TickType_t xLastWakeTime = xTaskGetTickCount();
//     const TickType_t xFrequency = pdMS_TO_TICKS(20); 

//     // 3. 实时闭环运行
//     while (1)
//     {
//         // --- 轮询 JY901S ---
//         uint16_t remain_1 = bsp_uart_get_dma_rx_remaining(BSP_UART_IMU1);
//         Driver_IMU_Poll_DMA_Update(IMU_JY901S, remain_1);
        
//         if (Driver_IMU_Process(IMU_JY901S, &data_jy901s) == true) 
//         {
//             // 坐标系转换 (解耦出来的纯逻辑操作)
//             IMU_Align_To_BodyFrame(&data_jy901s);
            
//             // 存入全局数组
//             g_imu_body_frame[IMU_JY901S] = data_jy901s;
//         }

//         // --- 轮询 IM948 ---
//         uint16_t remain_2 = bsp_uart_get_dma_rx_remaining(BSP_UART_IMU2);
//         Driver_IMU_Poll_DMA_Update(IMU_IM948, remain_2);
        
//         if (Driver_IMU_Process(IMU_IM948, &data_im948) == true) 
//         {
//             // 坐标系转换
//             IMU_Align_To_BodyFrame(&data_im948);
            
//             // 存入全局数组
//             g_imu_body_frame[IMU_IM948] = data_im948;
//         }
//         if(++log_divider >= 50) // 每 50 次循环打印一次日志 (即每 1000ms)
//         {
//             Log_Print(LOG_LEVEL_INFO, "IMU_JY901S Quat[W:%.2f X:%.2f Y:%.2f Z:%.2f]", 
//                       g_imu_body_frame[IMU_JY901S].quat[0], g_imu_body_frame[IMU_JY901S].quat[1], 
//                       g_imu_body_frame[IMU_JY901S].quat[2], g_imu_body_frame[IMU_JY901S].quat[3]);
            
//             Log_Print(LOG_LEVEL_INFO, "IMU_IM948  Quat[W:%.2f X:%.2f Y:%.2f Z:%.2f]", 
//                       g_imu_body_frame[IMU_IM948].quat[0], g_imu_body_frame[IMU_IM948].quat[1], 
//                       g_imu_body_frame[IMU_IM948].quat[2], g_imu_body_frame[IMU_IM948].quat[3]);
//             log_divider = 0;
//         }

//         // 严格睡眠 20ms
//         vTaskDelayUntil(&xLastWakeTime, xFrequency);
//     }
// }
