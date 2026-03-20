/* 硬件/OS相关 */
#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "task.h"

/*  BSP相关	 */
#include "bsp_uart.h"
#include "bsp_delay.h"
#include "bsp_gpio.h"
/*  Sys相关	 */
#include "sys_log.h"
#include "sys_data_pool.h"
#include "sys_boot_flag.h"
#include "sys_systick.h"

/*  Driver相关	 */
#include "driver_imu.h"
#include "driver_ms5837.h"
#include "driver_thruster.h"
#include "driver_dht11.h"
#include "driver_power.h"

/* 任务相关 */
#include "task_sensor.h"
#include "task_comm.h"
#include "task_nrt_cmd.h"

int main()
{
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);//设置系统中断优先级分组4
	
	/* BSP层初始化 */
	bsp_delay_init(); // 初始化 DWT 延时模块
	bsp_uart_init_default(); // 初始化所有串口设备，统一配置为 115200-8N1
	bsp_gpio_init();


	/*  Driver层初始化	*/
  	Driver_IMU_Init();	// 初始化 IMU 传感器
	Driver_Ms5837_Init();	// 初始化 MS5837 深度传感器
	Driver_DHT11_Init(); // 初始化 DHT11 温湿度传感器
	Driver_Power_Init(); // 初始化 ADC 或相关电源监控硬件
	Driver_Thruster_Init();	// 初始化推进器驱动 (PWM 输出)

	/* 初始化系统基础组件 */
	System_SysTick_Init(168);
	Sys_BootFlag_MarkBootSuccess();
  	Log_Init();

	Bot_Data_Pool_Init();   // 初始化全局数据池
//		LOG_INFO("======================================");
//    LOG_INFO("           OTA Success!!!!       ");
//    LOG_INFO("======================================");
    /* 打印开机横幅，验证日志系统正常工作 */
    LOG_INFO("======================================");
    LOG_INFO("   BRICOS System Booting...       ");
    LOG_INFO("   Sensor Tasks Initialization...     ");
    LOG_INFO("======================================");

	Task_Sensor_Init(); // 创建并启动所有传感器任务
	Task_Comm_Init();
	Task_NRT_Cmd_Init();
	vTaskStartScheduler(); // 启动 FreeRTOS 调度器，开始多任务运行


	while(1);
}
