#include "TaskScheduler.h"
#include "DHT11.h"
#include "JY901B_UART.h"
#include "wit_c_sdk.h"
#include "Serial.h"
#include "OLED.h"
#include "servo.h"
#include "Motor.h"
#include "LED.h"
#include "Delay.h"

// 外部变量
//extern float fAcc[3], fGyro[3], fAngle[3];
//extern u8 temperature, humidity;
//extern volatile char s_cDataUpdate;
//extern const uint32_t sReg[];

// 传感器数据采集任务
//void Task_SensorData(void)
//{
//    static int i;
//    
//    // 读取DHT11温湿度
//    DHT11_Read_Data(&temperature, &humidity);
//    
//    // 处理JY901B数据更新
//    if(s_cDataUpdate) {
//        for(i = 0; i < 3; i++) {
//            fAcc[i] = sReg[AX+i] / 32768.0f * 16.0f;
//            fGyro[i] = sReg[GX+i] / 32768.0f * 2000.0f;
//            fAngle[i] = sReg[Roll+i] / 32768.0f * 180.0f;
//        }
//        
//        if(s_cDataUpdate & ACC_UPDATE) {
//            s_cDataUpdate &= ~ACC_UPDATE;
//        }
//        if(s_cDataUpdate & GYRO_UPDATE) {
//            s_cDataUpdate &= ~GYRO_UPDATE;
//        }
//        if(s_cDataUpdate & ANGLE_UPDATE) {
//            s_cDataUpdate &= ~ANGLE_UPDATE;
//        }
//        if(s_cDataUpdate & MAG_UPDATE) {
//            s_cDataUpdate &= ~MAG_UPDATE;
//        }
//        
//        g_event_sensor_ready = 1;  // 设置传感器数据就绪事件
//    }
//}

//// 数据通信任务
//void Task_Serial_PCSend(void)
//{
//    // 更新传感器数据包
//    Serial_SensorData.accel_x = (int16_t)(fAcc[0]*100);
//    Serial_SensorData.accel_y = (int16_t)(fAcc[1]*100);
//    Serial_SensorData.accel_z = (int16_t)(fAcc[2]*100);
//    Serial_SensorData.gyro_x = (int16_t)(fGyro[0]*100);
//    Serial_SensorData.gyro_y = (int16_t)(fGyro[1]*100);
//    Serial_SensorData.gyro_z = (int16_t)(fGyro[2]*100);
//    Serial_SensorData.mag_x = (int16_t)(sReg[HX]*100);
//    Serial_SensorData.mag_y = (int16_t)(sReg[HY]*100);
//    Serial_SensorData.mag_z = (int16_t)(sReg[HZ]*100);
//    Serial_SensorData.angle_x = (int16_t)(fAngle[0]*100);
//    Serial_SensorData.angle_y = (int16_t)(fAngle[1]*100);
//    Serial_SensorData.angle_z = (int16_t)(fAngle[2]*100);
//    Serial_SensorData.temperature = (int16_t)(temperature);
//    Serial_SensorData.humidity = (int16_t)(humidity);
//    
//    // 发送传感器数据包
//    Serial_SendSensorPacket();
//    
//    // 检查是否接收到PWM指令
//    if (Serial_GetRxFlag() == 1) {
//        g_event_pwm_received = 1;  // 设置PWM接收事件
//    }
//}

// 显示更新任务
//void Task_Display(void)
//{
//    if (Serial_GetRxFlag() == 1) {
//        // 显示6个推进器PWM值
//        OLED_ShowNum(1, 1, Serial_RxPWM_Thruster[0], 4);
//        OLED_ShowNum(1, 6, Serial_RxPWM_Thruster[1], 4);
//        OLED_ShowNum(2, 1, Serial_RxPWM_Thruster[2], 4);
//        OLED_ShowNum(2, 6, Serial_RxPWM_Thruster[3], 4);
//        OLED_ShowNum(3, 1, Serial_RxPWM_Thruster[4], 4);
//        OLED_ShowNum(3, 6, Serial_RxPWM_Thruster[5], 4);
//        
//        // 显示舵机PWM值
//        OLED_ShowNum(4, 1, Serial_RxPWM_Servo[0], 4);
//        OLED_ShowNum(4, 6, Serial_RxPWM_Servo[1], 4);
//    }
//}

// 推进器PWM输出任务 (事件驱动)
//void Task_Thrusters_PWM(void)
//{
//    // 更新推进器推力
//    Motor_SetSpeed(Serial_RxPWM_Thruster[0]);

//}

//// 舵机PWM输出任务 (事件驱动)
//void Task_Servo_PWM(void)
//{
//    if (Serial_GetRxFlag() == 1) {
//        // 更新舵机角度
//        Servo_yaw_SetAngle(Serial_RxPWM_Servo[0]);
//        Servo_pitch_SetAngle(Serial_RxPWM_Servo[1]);
//        
//        // 清除接收标志
//        Serial_ClearRxFlag();
//    }
//}

// 照明灯PWM输出任务 (事件驱动)
//void Task_Light_PWM(void)
//{
//    /*if (Serial_GetRxFlag() == 1) {
//        // 更新舵机角度
//        Servo_yaw_SetAngle(Serial_RxPWM_Servo[0]);
//        Servo_pitch_SetAngle(Serial_RxPWM_Servo[1]);
//        
//        // 清除接收标志
//        Serial_ClearRxFlag();
//    }*/
//}

// 测试任务1：翻转LED
void Task_1(void)
{
    LED1_Turn();
}

// 测试任务2：自增计次显示
void Task_2(void)
{
    static uint32_t count = 0;
    OLED_ShowNum(4, 1, count++, 4);
}

// 测试任务3：串口发送数据
void Task_3(void)
{
    Serial_SendString("Hello World!\r\n");
}

// 测试任务4：将读入的串口值显示在OLED上 设置一个缓存变量 只读取这个变量 在串口中断中改写这个变量
//void Task_4(void)
//{
//    OLED_ShowHexNum(1, 1, Serial_RxData, 4);
//}

// 测试任务4：将读入的推进器PWM值显示在OLED上 设置一个缓存变量 只读取这个变量 在串口中断中改写这个变量
void Task_4(void)
{
    OLED_ShowNum(1, 1, Serial_RxPWM_Thruster[0], 4);
    OLED_ShowNum(1, 6, Serial_RxPWM_Thruster[1], 4);
    OLED_ShowNum(2, 1, Serial_RxPWM_Thruster[2], 4);
    OLED_ShowNum(2, 6, Serial_RxPWM_Thruster[3], 4);
    OLED_ShowNum(3, 1, Serial_RxPWM_Thruster[4], 4);
    OLED_ShowNum(3, 6, Serial_RxPWM_Thruster[5], 4);
}
