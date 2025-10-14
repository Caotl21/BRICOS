#ifndef __TASK_SCHEDULER_H
#define __TASK_SCHEDULER_H

#include "stm32f10x.h"

// 任务ID定义
//typedef enum {
//    TASK_SENSOR_DATA = 0,
//    TASK_SERIAL_PCSEND,
//    TASK_DISPLAY,
//    TASK_Thrusters_PWM_OUTPUT,
//    TASK_Servo_PWM_OUTPUT,
//    TASK_Light_PWM_OUTPUT,
//    TASK_COUNT
//} TaskID_t;

typedef enum {
    TASK_IM948_PROCESS = 0,
    TASK_1,
    TASK_2,
    TASK_3,
    TASK_4,
    TASK_COUNT
} TaskID_t;

// 任务状态
typedef enum {
    TASK_READY = 0,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_DISABLED
} TaskState_t;

// 任务控制块
typedef struct {
    TaskID_t id;
    uint32_t period_ms;          // 执行周期(ms)
    uint32_t last_run_time;      // 上次执行时间
    void (*task_func)(void);     // 任务函数指针
    TaskState_t state;           // 任务状态
    uint8_t enabled;             // 任务使能
    uint32_t run_count;          // 执行计数
} TaskControlBlock_t;

// 事件标志
extern volatile uint8_t g_event_pwm_received;
extern volatile uint8_t g_event_sensor_ready;
extern volatile uint8_t g_event_im948_received;
// 任务执行标志位
extern volatile uint8_t g_task_flags;

// 任务标志位定义
//#define TASK_FLAG_SENSOR_DATA   (1 << TASK_SENSOR_DATA)
//#define TASK_FLAG_SERIAL_PCSEND (1 << TASK_SERIAL_PCSEND)
//#define TASK_FLAG_DISPLAY       (1 << TASK_DISPLAY)
//#define TASK_FLAG_THRUSTERS_PWM    (1 << TASK_Thrusters_PWM_OUTPUT)
//#define TASK_FLAG_SERVO_PWM    (1 << TASK_Servo_PWM_OUTPUT)
//#define TASK_FLAG_LIGHT_PWM    (1 << TASK_Light_PWM_OUTPUT)
#define TASK_FLAG_IM948_PROCESS (1 << TASK_IM948_PROCESS)
#define TASK_FLAG_1    (1 << TASK_1)
#define TASK_FLAG_2    (1 << TASK_2)
#define TASK_FLAG_3    (1 << TASK_3)
#define TASK_FLAG_4    (1 << TASK_4)


// 函数声明
void TaskScheduler_Init(void);     // 
void TaskScheduler_Check(void);    // 检查任务，设置标志位
void TaskScheduler_Execute(void);  // 执行标志位对应的任务
void TaskScheduler_Run(void);
uint32_t GetSystemTick(void);
void SystemTick_Increment(void);

#endif
