#include "TaskScheduler.h"
#include "DHT11.h"
#include "wit_c_sdk.h"
#include "im948_CMD.h"
#include "Serial.h"
#include "Motor.h"
#include "Delay.h"
#include "JY901B.h"
#include "MS5837_I2C.h"
#include "AD.h"

u8 temperature = 0;
u8 humidity = 0;
float Mea_Temp,Mea_Press,Mea_Depth;
float fAcc[3], fGyro[3], fQuater[4];
float voltage;
float current;

#define QW 0
#define QX_HAT 0.7071
#define QY_HAT -0.7071
#define QZ_HAT 0

//状态机和计数
u8 MS5837_Temp_State = 0;
u8 MS5837_Depth_State = 0;
u8 MS5837_Temp_Init_Flag = 0;
u8 MS6837_Temp_count = 0;


// 传感器数据采集任务
//任务：IM948数据缓存区指针更新
void Task_DMA_pdata_poll(void)
{
    // 读取寄存器，开销极小
    uint16_t current_dma_cnt = DMA_GetCurrDataCounter(DMA2_Stream2);
    static uint16_t last_dma_cnt = FifoSize;

    if (current_dma_cnt != last_dma_cnt)
    {
        Uart1Fifo.In = FifoSize - current_dma_cnt;
		if (Uart1Fifo.In == FifoSize) Uart1Fifo.In = 0; // 防止读取到 0 瞬间导致的越界
        last_dma_cnt = current_dma_cnt;
    }
}


//任务：IM948数据读取
void Task_IM948_Process(void)
{
    IM948_process();
}

//任务：JY901数据缓存区指针更新
void Task_JY901_DMA_pdata_poll(void)
{
    // 读取寄存器，开销极小
	static uint16_t JY901_last_dma_cnt = JY901BFifoSize;
    uint16_t current_dma_cnt = DMA_GetCurrDataCounter(DMA1_Stream5);
	if (current_dma_cnt != JY901_last_dma_cnt)
    {
        // 仅仅更新 In 指针，其他什么都不做
        JY901BFifo.In = JY901BFifoSize - current_dma_cnt;
		if (JY901BFifo.In == JY901BFifoSize) JY901BFifo.In = 0; // 防止读取到 0 瞬间导致的越界
        JY901_last_dma_cnt = current_dma_cnt;
    }
}

// 任务：JY901B数据读取
void JY901B_Task(void)
{
	JY901B_process();
	JY901B_GetData(fAcc,fGyro,fQuater);
}


//任务：MS5837深度和温度信息读取
void MS5837_Task(void)
{
	//在读取压强信息之前要先获取温度
	if(MS5837_Temp_Init_Flag == 0)
	{
		switch(MS5837_Temp_State)
		{
			case 0:
				MS5837_GetTemp_1();
				MS5837_Temp_State = 1;
			break;
			case 1:
				MS5837_GetTemp_2(&Mea_Temp);
				MS5837_Temp_State = 0;
				MS5837_Temp_Init_Flag = 1;
			break;
		}
	}
	else
	{
		//如果运行过一定时间则开始获取温度
		if(MS6837_Temp_count>=20)
		{
			MS5837_Temp_State = 1;
			MS6837_Temp_count = 0;
		}
		//进入状态机
		switch(MS5837_Depth_State)
		{
			case 0:
				//如果完成了温度读取子任务1，现在要先将温度读取读完，再发送指令读取深度
				if(MS5837_Temp_State == 2)
				{
					MS5837_GetTemp_2(&Mea_Temp);
					MS5837_Temp_State = 0;
				}
				//深度读取子任务1
				MS5837_GetDepth_1();
				MS5837_Depth_State = 1;
			break;
			case 1:
				//此时将深度读取读完
				MS5837_GetDepth_2(&Mea_Press,&Mea_Depth);
				MS5837_Depth_State = 0;
				//如果深度读取读完，此时I2C空闲，可以进行温度读取，执行温度读取子任务1
				if(MS5837_Temp_State == 1) 
				{
					MS5837_GetTemp_1();
					MS5837_Temp_State = 2;
				}
			break;
		}
		MS6837_Temp_count++;
	}
}

// 任务：DHT11读取
void DHT11_Task(void)
{
	//读取数据
	DHT11_Read_Data(&temperature, &humidity);
}

//任务：电压采样信息
void Voltage_Task(void)
{
	current = current_Getdata();
	voltage = voltage_Getdata();
}

void Uart_send_fast_Task(void)
{

	// 更新传感器数据包
   if(IM948_GetNewData || isNewData_JY901B){
		IM948_GetNewData = 0;
		isNewData_JY901B = 0;
		Serial_SensorData.accel_x1 = (int16_t)(fAcc[0]*100);
		Serial_SensorData.accel_y1 = (int16_t)(fAcc[1]*100);
		Serial_SensorData.accel_z1 = (int16_t)(fAcc[2]*100);
		Serial_SensorData.gyro_x1 = (int16_t)(fGyro[0]*100);
		Serial_SensorData.gyro_y1 = (int16_t)(fGyro[1]*100);
		Serial_SensorData.gyro_z1 = (int16_t)(fGyro[2]*100);
		Serial_SensorData.Wquat_1 = (int16_t)(fQuater[0]*100);
		Serial_SensorData.Xquat_1 = (int16_t)(fQuater[1]*100);
		Serial_SensorData.Yquat_1 = (int16_t)(fQuater[2]*100);
		Serial_SensorData.Zquat_1 = (int16_t)(fQuater[3]*100);
		Serial_SensorData.accel_x2 = (int16_t)(AccX_im948*100);
		Serial_SensorData.accel_y2 = (int16_t)(AccY_im948*100);
		Serial_SensorData.accel_z2 = (int16_t)(AccZ_im948*100);
		Serial_SensorData.gyro_x2 = (int16_t)(GyroX_im948*100);
		Serial_SensorData.gyro_y2 = (int16_t)(GyroY_im948*100);
		Serial_SensorData.gyro_z2 = (int16_t)(GyroZ_im948*100);
		Serial_SensorData.Wquat_2 = (int16_t)(W_quat_im948*100);
		Serial_SensorData.Xquat_2 = (int16_t)(X_quat_im948*100);
		Serial_SensorData.Yquat_2 = (int16_t)(Y_quat_im948*100);
	    Serial_SensorData.Zquat_2 = (int16_t)(Z_quat_im948*100);
		Serial_SensorData.press = (int16_t)(Mea_Press*100);

		// 发送传感器数据包
		Fast_Serial_SendSensorPacket();
   }
}

void Uart_send_slow_Task(void)
{
	Serial_SensorData.temperature_water = (int16_t)(Mea_Temp*100);
	Serial_SensorData.humidity = (int16_t)(humidity*100);
	Serial_SensorData.temperature_AUV = (int16_t)(temperature*100);
	Serial_SensorData.voltage = (int16_t)(voltage*100);
	Serial_SensorData.current = (int16_t)(current*100);
	
	// 发送传感器数据包
	Slow_Serial_SendSensorPacket();	
}


