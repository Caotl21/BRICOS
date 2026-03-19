#ifndef __DRIVER_IMU_H
#define __DRIVER_IMU_H

#include "bsp_core.h"

// 定义系统支持的 IMU 类型 ID
typedef enum {
    IMU_JY901S = 0,
    IMU_IM948,
    IMU_MAX_NUM
} imu_id_t;

// 统一的物理量输出结构体
typedef struct {
    float acc[3];
    float gyro[3];
    float quat[4];
} imu_data_t;

//初始化
void Driver_IMU_Init(void);

// --- 统一 API ---
uint8_t* Driver_IMU_GetRxBuf(imu_id_t id);
uint16_t Driver_IMU_GetBufSize(imu_id_t id);

// 更新 DMA 进度
void Driver_IMU_Poll_DMA_Update(imu_id_t id, uint16_t current_dma_cnt);

// 【核心】：Task层传入存储地址 (out_data)，Driver负责解析并赋值
// 如果缓冲区内有完整新数据，赋值并返回 true；否则返回 false
bool Driver_IMU_Process(imu_id_t id, imu_data_t *out_data);

#endif
