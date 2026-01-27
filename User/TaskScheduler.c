#include "TaskScheduler.h"
#include "Delay.h"
#include "im948_CMD.h"
#include "Serial.h"
#include "OLED.h"
//#include "servo.h"
#include "DHT11.h"
#include "JY901B_UART.h"
#include "wit_c_sdk.h"
#include "Tasks.h"
#include "Timer.h"
#include "Watchdog.h"
#include "Types.h"

// 全局事件标志
volatile uint8_t g_event_pwm_received = 0;
volatile uint8_t g_event_sensor_ready = 0;
volatile uint8_t g_event_im948_received = 0;
volatile uint8_t g_event_JY901B_received = 0;

// 任务执行标志位
volatile uint16_t g_task_flags = 0;

// 系统时间计数器(ms)
static volatile uint32_t system_tick_ms = 0;

// 任务控制块数组注册表
static TaskControlBlock_t task_table[TASK_COUNT] = {
	{TASK_JY901_DMA_PDATA_POLL,  20,   0, Task_JY901_DMA_pdata_poll,    TASK_READY, 1, 0},
	{JY901B_TASK,  20,  0, JY901B_Task,  TASK_READY, 1, 0},
	{TASK_DMA_PDATA_POLL,  20,   0, Task_DMA_pdata_poll,    TASK_READY, 1, 0},
	{TASK_IM948_PROCESS,  20,   0, Task_IM948_Process,    TASK_READY, 1, 0},
	{MS5837_TASK,  100,  0, MS5837_Task,  TASK_READY, 1, 0},
    {DHT11_TASK,  20, 0, DHT11_Task,  TASK_READY, 1, 0},
    {Voltage_TASK,  5000,   0, Voltage_Task,  TASK_READY, 1, 0},
	{UART_SEND_FAST_TASK, 20, 0, Uart_send_fast_Task, TASK_READY,1,0},
	{UART_SEND_SLOW_TASK, 2000, 0, Uart_send_slow_Task, TASK_READY,1,0},
};

void TaskScheduler_Init(void)
{
    // 初始化系统滴答定时器 (1ms中断)
    //SysTick_Config(SystemCoreClock / 1000);
    Timer_Init();

    // 初始化任务表
    for(int i = 0; i < TASK_COUNT; i++) {
        task_table[i].last_run_time = GetSystemTick();
        task_table[i].run_count = 0;
        task_table[i].state = TASK_READY;
    }
    
    // 清除任务标志位
    g_task_flags = 0;
}

// 任务检查函数 - 只负责检查和设置标志位
void TaskScheduler_Check(void)
{
    uint32_t current_time = GetSystemTick();  // 只获取一次时间
	

//    if(task_table[TASK_IM948_PROCESS].period_ms > 0){
//        if((current_time - task_table[TASK_IM948_PROCESS].last_run_time) >= task_table[TASK_IM948_PROCESS].period_ms) {
//            if(g_event_im948_received && task_table[TASK_IM948_PROCESS].enabled) {
//                g_task_flags |= TASK_FLAG_IM948_PROCESS;
//                g_event_im948_received = 0;
//            }
//        }
//    }
//	 if(task_table[JY901B_TASK].period_ms > 0){
//        if((current_time - task_table[JY901B_TASK].last_run_time) >= task_table[JY901B_TASK].period_ms) {
//			
//			if(g_event_JY901B_received && task_table[JY901B_TASK].enabled) {
//                g_task_flags |= (1 << JY901B_TASK);
//                g_event_JY901B_received = 0;
//            }
//        }
//    }
    
    // 检查周期性任务
    for(int i = 0; i < TASK_COUNT; i++) {
        TaskControlBlock_t* task = &task_table[i];
        
        if(!task->enabled || task->state == TASK_DISABLED) {
            continue;
        }
        
        // 周期性任务检查
        if(task->period_ms > 0) {
            if((current_time - task->last_run_time) >= task->period_ms) {
				g_task_flags |= (1 << task->id);
            }
        }
    }

    // 检查事件驱动任务
//    if(g_event_pwm_received && task_table[TASK_4].enabled) {
//        g_task_flags |= TASK_FLAG_4;
//        g_event_pwm_received = 0;
//    }
}

// 任务执行函数 - 只负责执行标志位对应的任务
void TaskScheduler_Execute(void)
{
    uint32_t current_time = GetSystemTick();

    // 按顺序检查并执行标志位对应的任务
    for(int i = 0; i < TASK_COUNT; i++) {
        TaskControlBlock_t* task = &task_table[i];
        uint16_t task_flag = (1 << task->id);
        // 将所有任务的全局标志位和该任务标志位按位与
        if(g_task_flags & task_flag) {
            // 清除标志位
            g_task_flags &= ~(1 << task->id);
            
            // 执行任务
            task->state = TASK_RUNNING;
			task->last_run_time += task->period_ms;
			
            task->task_func();
            //task->last_run_time = GetSystemTick();
            //task->last_run_time = current_time;
            task->run_count++;
            task->state = TASK_READY;
        }
    }
}

// 时间片开始轮询
void TaskScheduler_Run(void)
{
    TaskScheduler_Check();
    TaskScheduler_Execute();
    Watchdog_Feed();
}

uint32_t GetSystemTick(void)
{
    return system_tick_ms;
}

// SysTick中断处理函数
//void SysTick_Handler(void)
//{
//    system_tick_ms++;
//}

// 供中断调用的时钟递增函数
void SystemTick_Increment(void)
{
    system_tick_ms++;
}

void TIM2_IRQHandler(void)
{
	if (TIM_GetITStatus(TIM2, TIM_IT_Update) == SET)
	{
		SystemTick_Increment();
		TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
	}
}

//将各个任务状态都修改为不可运行状态
void Task_Disable(void)
{
	for(int i = 0; i < TASK_COUNT; i++) {
        task_table[i].state = TASK_DISABLED;
    }
}

//将各个任务状态都修改为可运行状态
void Task_Enable(void)
{
	for(int i = 0; i < TASK_COUNT; i++) {
        task_table[i].state = TASK_READY;
    }	
}