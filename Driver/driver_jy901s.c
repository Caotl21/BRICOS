#include "driver_jy901s.h"
#include "bsp_core.h"
#include "bsp_uart.h"
#include "misc.h"
#include "wit_c_sdk.h"
#include "bot_data_pool.h"


// --- 内部状态与缓存 ---
// 用于暂存未凑齐 11 字节的零碎数据包
static uint8_t s_rx_buf[256]; 
static uint16_t s_rx_len = 0;

static uint8_t __CaliSum(uint8_t *data, uint32_t len)
{
    uint32_t i;
    uint8_t ucCheck = 0;
    for(i=0; i<len; i++) ucCheck += *(data + i);
    return ucCheck;
}


// IMU 发送是连续多包(先发加速度，再发角速度，最后发角度)
// 我们用静态变量把角速度暂存起来，等收到角度时，一起打包推入数据池
static float s_cached_gx = 0.0f, s_cached_gy = 0.0f, s_cached_gz = 0.0f;

/**
 * @brief 解析 IMU 字节流 (极简高效的滑动窗口解析器)
 * @param data 从串口/DMA 拿到的数据块指针
 * @param len 数据块长度
 */
void Driver_JY901S_Parse_Stream(const uint8_t *data, uint16_t len)
{
    // 1. 防溢出保护：将新数据追加到解析缓冲区
    if (s_rx_len + len > sizeof(s_rx_buf)) {
        s_rx_len = 0; // 发生溢出，丢弃旧数据，重新同步
    }
    memcpy(&s_rx_buf[s_rx_len], data, len);
    s_rx_len += len;

    // 2. 只要缓冲区里大于等于一帧 (11字节)，就开始疯狂解析
    while (s_rx_len >= 11) 
    {
        // 步骤 A：寻找包头 0x55
        if (s_rx_buf[0] != 0x55) 
        {
            // 如果头不是 0x55，说明丢包错位了，剔除第一个字节，继续找
            memmove(s_rx_buf, s_rx_buf + 1, s_rx_len - 1);
            s_rx_len--;
            continue;
        }

        // 步骤 B：计算 Checksum (前 10 字节累加)
        uint8_t sum = 0;
        for (int i = 0; i < 10; i++) {
            sum += s_rx_buf[i];
        }

        // 步骤 C：校验失败，剔除包头，重新寻找
        if (sum != s_rx_buf[10]) 
        {
            memmove(s_rx_buf, s_rx_buf + 1, s_rx_len - 1);
            s_rx_len--;
            continue; 
        }

        // ==========================================
        // 步骤 D：校验成功！提取数据 (组合两个 uint8_t 成为有符号 int16_t)
        // ==========================================
        int16_t d0 = (int16_t)(((uint16_t)s_rx_buf[3] << 8) | s_rx_buf[2]);
        int16_t d1 = (int16_t)(((uint16_t)s_rx_buf[5] << 8) | s_rx_buf[4]);
        int16_t d2 = (int16_t)(((uint16_t)s_rx_buf[7] << 8) | s_rx_buf[6]);

        switch (s_rx_buf[1]) // 判断包类型
        {
            case 0x52: // 角速度包 (Gyro)
                s_cached_gx = (float)d0 / 32768.0f * 2000.0f;
                s_cached_gy = (float)d1 / 32768.0f * 2000.0f;
                s_cached_gz = (float)d2 / 32768.0f * 2000.0f;
                break;

            case 0x53: // 角度包 (Angle) -> 这一包通常是 IMU 一帧数据的结尾
            {
                float roll  = (float)d0 / 32768.0f * 180.0f;
                float pitch = (float)d1 / 32768.0f * 180.0f;
                float yaw   = (float)d2 / 32768.0f * 180.0f;

                // ★ 一键神迹：打包角速度和姿态角，直接推入全局数据池！
                Bot_State_Push_IMU(roll, pitch, yaw, s_cached_gx, s_cached_gy, s_cached_gz);
                break;
            }
            
            // 如果需要加速度 0x51 可以在这里加，不需要就不写
        }

        // 步骤 E：成功消费 11 个字节，将剩余数据往前移
        s_rx_len -= 11;
        if (s_rx_len > 0) {
            memmove(s_rx_buf, s_rx_buf + 11, s_rx_len);
        }
    }
}