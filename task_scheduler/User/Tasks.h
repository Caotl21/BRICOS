#ifndef __TASKS_H
#define __TASKS_H

#include "stm32f10x.h"

// 任务函数声明
void Task_SensorData(void);
void Task_Serial_PCSend(void);
void Task_Display(void);
void Task_Thrusters_PWM(void);
void Task_Servo_PWM(void);
void Task_Light_PWM(void);

// 调度器测试函数声明
void Task_1(void);
void Task_2(void);
void Task_3(void);
void Task_4(void);

#endif
