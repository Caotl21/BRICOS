#ifndef __TASKS_H
#define __TASKS_H

#include "stm32f4xx.h"
#include "Serial.h"


extern u8 temperature;
extern u8 humidity;


// 任务函数声明
void Task_SensorData(void);
void Task_Serial_PCSend(void);
void Task_Display(void);
void Task_Thrusters_PWM(void);
void Task_Servo_PWM(void);
void Task_Light_PWM(void);

void JY901B_Task(void);
void MS5837_Task(void);
void DHT11_Task(void);
void Voltage_Task(void);

void Uart_send_fast_Task(void);
void Uart_send_slow_Task(void);

void Task_IM948_Process(void);
void Task_DMA_pdata_poll(void);
void Task_JY901_DMA_pdata_poll(void);

#endif
