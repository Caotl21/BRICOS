#include "driver_imu.h"
#include "bsp_core.h"
#include "bsp_delay.h"
#include "bsp_uart.h"
#include "im948_CMD.h"

#define IMU_FIFO_SIZE 256

// 统一的环形缓冲区设备结构体
typedef struct {
    uint8_t  rx_buf[IMU_FIFO_SIZE];
    volatile uint16_t In;
    volatile uint16_t Out;
    uint16_t last_dma_cnt;
} imu_dev_t;

static imu_dev_t s_devs[IMU_MAX_NUM] = {0};

/* ===================================================================
 * 各个 IMU 专属的解析逻辑 (未来添加新 IMU 就在这里加)
 * =================================================================== */

// --- JY901S：初始化以及解析 ---
static void Init_JY901S(void) 
{
    bsp_uart_start_dma_rx_circular(BSP_UART_IMU2,
                                   Driver_IMU_GetRxBuf(IMU_JY901S),
                                   Driver_IMU_GetBufSize(IMU_JY901S));
                                   
}

static bool JY901S_Parse_Payload(const uint8_t *frame, imu_data_t *out_data)
{
    bool is_frame_completed = false;

    switch (frame[1]) 
    {
        case 0x51: /* 解析加速度数据 (量程: ±16g) */
            out_data->acc[0] = (float)((int16_t)(frame[3] << 8 | frame[2])) / 32768.0f * 16.0f;
            out_data->acc[1] = (float)((int16_t)(frame[5] << 8 | frame[4])) / 32768.0f * 16.0f;
            out_data->acc[2] = (float)((int16_t)(frame[7] << 8 | frame[6])) / 32768.0f * 16.0f;
            break;
            
        case 0x52: /* 解析角速度数据 (量程: ±2000°/s) */
            out_data->gyro[0] = (float)((int16_t)(frame[3] << 8 | frame[2])) / 32768.0f * 2000.0f;
            out_data->gyro[1] = (float)((int16_t)(frame[5] << 8 | frame[4])) / 32768.0f * 2000.0f;
            out_data->gyro[2] = (float)((int16_t)(frame[7] << 8 | frame[6])) / 32768.0f * 2000.0f;
            break;
            
        case 0x59: /* 解析四元数数据 (归一化数值) */
            out_data->quat[0] = (float)((int16_t)(frame[3] << 8 | frame[2])) / 32768.0f;
            out_data->quat[1] = (float)((int16_t)(frame[5] << 8 | frame[4])) / 32768.0f;
            out_data->quat[2] = (float)((int16_t)(frame[7] << 8 | frame[6])) / 32768.0f;
            out_data->quat[3] = (float)((int16_t)(frame[9] << 8 | frame[8])) / 32768.0f;
            
            /* 接收到四元数数据包，标志着当前周期内的传感器状态已全部更新完毕 */
            is_frame_completed = true; 
            break;
            
        default:
            /* 预留接口：可在此处拓展磁场(0x54)、气压(0x56)等其他协议的解析 */
            break;
    }
    
    return is_frame_completed;
}

static bool Parse_JY901S(imu_dev_t *dev, imu_data_t *out_data) 
{
    bool has_new_data = false;
    
    /* 1. 将 volatile 全局指针提取到 CPU 局部寄存器中 */
    uint16_t head = dev->In;
    uint16_t tail = dev->Out;
    
    /* 计算积压的待处理数据量 */
    uint16_t cnt = (head >= tail) ? (head - tail) : (IMU_FIFO_SIZE + head - tail);

    /* 2. 在局部空间内疯狂试探与解包 */
    while (cnt >= 11) 
    {
        /* A. 寻找帧头 (使用 tail) */
        if (dev->rx_buf[tail] != 0x55) {
            tail = (tail + 1) % IMU_FIFO_SIZE;
            cnt--; 
            continue;
        }

        /* B. 提取 11 字节完整帧并处理回绕 (使用 tail) */
        uint8_t frame[11];
        for (int i = 0; i < 11; i++) {
            frame[i] = dev->rx_buf[(tail + i) % IMU_FIFO_SIZE];
        }

        /* C. 校验和计算 */
        uint8_t sum = 0;
        for (int i = 0; i < 10; i++) sum += frame[i];
        
        if (sum != frame[10]) {
            tail = (tail + 1) % IMU_FIFO_SIZE;
            cnt--; 
            continue;
        }

        /* D. 载荷解算 */
        if (JY901S_Parse_Payload(frame, out_data) == true) {
            has_new_data = true;
        }

        /* E. 成功解析一帧，推进局部读指针 11 步 */
        tail = (tail + 11) % IMU_FIFO_SIZE;
        cnt -= 11;
    }
    
    /* 3. 循环彻底结束后，将最终进度一次性原子提交回结构体 */
    dev->Out = tail;
    
    return has_new_data;
}


// --- IM948: 初始化以及解析 ---

static void IM948_Hardware_Tx(uint8_t *pBuf, uint16_t len)
{
    // 调用 BSP 层的接口，把数据实打实地从外设串口 2 发出去
    bsp_uart_send_buffer(BSP_UART_IMU1, pBuf, len);
}

static void Init_IM948(void) 
{
    bsp_uart_start_dma_rx_circular(BSP_UART_IMU1,
                                   Driver_IMU_GetRxBuf(IMU_IM948),
                                   Driver_IMU_GetBufSize(IMU_IM948));

    IM948_RegisterTxCallback(IM948_Hardware_Tx);
    bsp_delay_ms(200);// 延时一下让传感器上电准备完毕，传感器上电后需要初始化完毕后才会接收指令的

    Cmd_03(); // 唤醒传感器
    
    // 设置设备参数
    Cmd_12(5, 255, 0, 0, 3, 25, 2, 4, 9, 0x35); 
    Cmd_19(); // 开启数据主动上报
    bsp_delay_ms(100);
    Cmd_40(2);
   
}


// --- 专属解析器 2：IM948 (变长状态机) ---
// 你的 Cmd_GetPkt 状态机内部逻辑可以放这里面，或者调用外部函数
static bool Parse_IM948(imu_dev_t *dev, imu_data_t *out_data) 
{
    bool has_new_data = false;
    
    //使用局部变量接管读写指针，提高 CPU 寄存器寻址速度 (你的原版思路)
    uint16_t head = dev->In;
    uint16_t tail = dev->Out;

    if (head == tail) return false;

    // 计算当前积压的字节数
    uint16_t cnt = (head >= tail) ? (head - tail) : (IMU_FIFO_SIZE + head - tail);

    //贪婪吞噬循环：吃干榨净缓冲区里的所有数据
    while (cnt > 0) 
    {
        // 提取字节，推进局部读指针
        uint8_t rx_byte = dev->rx_buf[tail];
        tail = (tail + 1) % IMU_FIFO_SIZE;
        cnt--;

        // 将字节喂给专属的状态机
        if (Cmd_GetPkt(rx_byte, out_data->acc, out_data->gyro, out_data->quat) == 1) 
            {
                has_new_data = true; 
            }
        }

    //循环结束，把最终的读进度同步回真实的驱动设备结构体中
    dev->Out = tail;
    
    return has_new_data;
}



/* ===================================================================
 * 【通用区】：对外的统一接口
 * =================================================================== */
uint8_t* Driver_IMU_GetRxBuf(imu_id_t id)   { return s_devs[id].rx_buf; }
uint16_t Driver_IMU_GetBufSize(imu_id_t id) { return IMU_FIFO_SIZE; }

void Driver_IMU_Poll_DMA_Update(imu_id_t id, uint16_t current_dma_cnt) {
    if (current_dma_cnt != s_devs[id].last_dma_cnt) {
        s_devs[id].In = IMU_FIFO_SIZE - current_dma_cnt;
        if (s_devs[id].In == IMU_FIFO_SIZE) s_devs[id].In = 0;
        s_devs[id].last_dma_cnt = current_dma_cnt;
    }
}

// 对外暴露的大一统初始化接口
void Driver_IMU_Init(void) 
{
    Init_JY901S();
    Init_IM948();
}
    

// 统一的分发器：上层传地址进来，下层路由给具体的解析器去填空
bool Driver_IMU_Process(imu_id_t id, imu_data_t *out_data) {
    if (out_data == NULL) return false;
    
    switch (id) {
        case IMU_JY901S: return Parse_JY901S(&s_devs[id], out_data);
        case IMU_IM948:  return Parse_IM948(&s_devs[id], out_data);
        default:         return false;
    }
}
