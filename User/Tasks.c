#include "TaskScheduler.h"
#include "DHT11.h"
#include "JY901B_UART.h"
#include "wit_c_sdk.h"
#include "im948_CMD.h"
#include "Serial.h"
#include "OLED.h"
//#include "servo.h"
#include "Motor.h"
#include "LED.h"
#include "Delay.h"
#include "JY901B.h"
#include "MS5837_I2C.h"
#include "AD.h"

// 外部变量
//extern float fAcc[3], fGyro[3], fAngle[3];
//extern u8 temperature, humidity;
//extern volatile char s_cDataUpdate;
//extern const uint32_t sReg[];
u8 temperature = 0;
u8 humidity = 0;
float Mea_Temp,Mea_Press,Mea_Depth;
float fAcc[3], fGyro[3], fQuater[4];
float voltage;
float current;

//四元数的共轭
//#define QW 0.7071
//#define QX_HAT 0
//#define QY_HAT 0
//#define QZ_HAT -0.7071
#define QW 0
#define QX_HAT 0.7071
#define QY_HAT -0.7071
#define QZ_HAT 0
//标志位


//状态机和计数
u8 MS5837_Temp_State = 0;
u8 MS5837_Depth_State = 0;
u8 MS5837_Temp_Init_Flag = 0;
u8 MS6837_Temp_count = 0;


// 传感器数据采集任务
//任务：IM948数据缓存区指针更新
void Task_DMA_pdata_poll(void)
{
//	printf("1");
    // 读取寄存器，开销极小
    uint16_t current_dma_cnt = DMA_GetCurrDataCounter(DMA2_Stream2);
    static uint16_t last_dma_cnt = FifoSize;

    if (current_dma_cnt != last_dma_cnt)
    {
        // 仅仅更新 In 指针，其他什么都不做
		
		g_event_im948_received = 1;
        Uart1Fifo.In = FifoSize - current_dma_cnt;
		//printf("current_dma_cnt is:%d",Uart1Fifo.In);
		if (Uart1Fifo.In == FifoSize) Uart1Fifo.In = 0; // 防止读取到 0 瞬间导致的越界
        last_dma_cnt = current_dma_cnt;
    }
}


//任务：IM948数据读取
void Task_IM948_Process(void)
{
//	int current_time = GetSystemTick();
//	printf("\r\n%d ",current_time);
    IM948_process();
	//printf("g_event_pwm_received: %d\r\n", g_event_im948_received);
}

//任务：JY901数据缓存区指针更新
void Task_JY901_DMA_pdata_poll(void)
{
	//printf("1");
    // 读取寄存器，开销极小
	static uint16_t JY901_last_dma_cnt = JY901BFifoSize;
    uint16_t current_dma_cnt = DMA_GetCurrDataCounter(DMA1_Stream5);
//    printf("current_dma_cnt is:%d\r\n",current_dma_cnt);
//	printf("JY901_last_dma_cnt is:%d\r\n",JY901_last_dma_cnt);
	if (current_dma_cnt != JY901_last_dma_cnt)
    {
        // 仅仅更新 In 指针，其他什么都不做
        JY901BFifo.In = JY901BFifoSize - current_dma_cnt;
//		printf("JY901BFifo.In is:%d\r\n",JY901BFifo.In);
		if (JY901BFifo.In == JY901BFifoSize) JY901BFifo.In = 0; // 防止读取到 0 瞬间导致的越界
        JY901_last_dma_cnt = current_dma_cnt;
    }
}

// 任务：JY901B数据读取
void JY901B_Task(void)
{
	int current_time = GetSystemTick();
//	printf("\r\nt:%d ",current_time);
	//printf("1");
	JY901B_process();
	JY901B_GetData(fAcc,fGyro,fQuater);
//	if(1)
//	{
//		float temp = 0;
//		temp = fAcc[0];
//		fAcc[0] = -fAcc[1];
//		fAcc[1] = -temp;
//		fAcc[2] = -fAcc[2];
//		temp = fGyro[0];
//		fGyro[0] = -fGyro[1];
//		fGyro[1] = -temp;
//		fGyro[2] = -fGyro[2];
//		float w_temp=fQuater[0];
//		float x_temp=fQuater[1];
//		float y_temp=fQuater[2];
//		float z_temp=fQuater[3];
//		fQuater[0] = w_temp*QW - x_temp*QX_HAT - y_temp*QY_HAT - z_temp* QZ_HAT;
//		fQuater[1] = w_temp*QX_HAT + x_temp*QW + y_temp*QZ_HAT - z_temp* QY_HAT;
//		fQuater[2] = w_temp*QY_HAT + y_temp*QW + z_temp*QX_HAT - x_temp* QZ_HAT;
//		fQuater[3] = w_temp*QZ_HAT + z_temp*QW + x_temp*QY_HAT - y_temp* QX_HAT;
//		float invN = 1.0f / sqrtf(fQuater[0]*fQuater[0] + fQuater[1]*fQuater[1] + 
//                          fQuater[2]*fQuater[2] + fQuater[3]*fQuater[3]);
//		fQuater[0] *= invN; fQuater[1] *= invN; fQuater[2] *= invN; fQuater[3] *= invN;
//	}
//		printf("\r\n%d ",current_time);
//		printf("%f\r\n",fQuater[0]);
	
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
	//printf("\r\nT:%f,D:%d\r\n",(uint32_t)(Mea_Temp),(uint32_t)(Mea_Depth));
}

// 任务：DHT11读取
void DHT11_Task(void)
{
	//读取数据
	DHT11_Read_Data(&temperature, &humidity);
	//显示数据
	//printf("\r\nT:%d,H:%d\r\n",temperature,humidity);
}

//任务：电压采样信息
void Voltage_Task(void)
{
	current = current_Getdata();
	voltage = voltage_Getdata();
	//printf("\r\nI:%f,V:%f\r\n",current,voltage);
//	OLED_ShowString(1,8,"V:");
//	OLED_ShowNum(1, 10, (uint32_t)(voltage*10), 4);		//显示转换结果第0个数据
//	OLED_ShowString(4, 7, ".");
//	OLED_ShowNum(4, 8, ((voltage*10)-(uint32_t)(voltage)*10), 1);
}

//// 数据通信任务
//void Task_Serial_PCSend(void)
//{
//   // 更新传感器数据包
//   if(IM948_GetNewData || isNewData_JY901B){
//		IM948_GetNewData = 0;
//		isNewData_JY901B = 0;
//		printf("IM948: angleX: %.3f, angleY: %.3f, angleZ: %.3f\r\n", AngleX,AngleY,AngleZ);
//		
//		printf("JY901: angleX: %.3f, angleY: %.3f, angleZ: %.3f\n\r",fAngle[0],fAngle[1],fAngle[2]);
//		Serial_SensorData.accel_x = (int16_t)(fAcc[0]*100);
//		Serial_SensorData.accel_y = (int16_t)(fAcc[1]*100);
//		Serial_SensorData.accel_z = (int16_t)(fAcc[2]*100);
//		Serial_SensorData.gyro_x = (int16_t)(fGyro[0]*100);
//		Serial_SensorData.gyro_y = (int16_t)(fGyro[1]*100);
//		Serial_SensorData.gyro_z = (int16_t)(fGyro[2]*100);
//		Serial_SensorData.mag_x = (int16_t)(sReg[HX]*100);
//		Serial_SensorData.mag_y = (int16_t)(sReg[HY]*100);
//		Serial_SensorData.mag_z = (int16_t)(sReg[HZ]*100);
//		Serial_SensorData.angle_x = (int16_t)(fAngle[0]*100);
//		Serial_SensorData.angle_y = (int16_t)(fAngle[1]*100);
//		Serial_SensorData.angle_z = (int16_t)(fAngle[2]*100);
//		Serial_SensorData.temperature = (int16_t)(temperature);
//		Serial_SensorData.humidity = (int16_t)(humidity);
//		
//		// 发送传感器数据包
//		Serial_SendSensorPacket();
//   }
//   
//}

void Uart_send_fast_Task(void)
{
	//printf("JY901: angleX: %.3f, angleY: %.3f, angleZ: %.3f\n\r",fAngle[0],fAngle[1],fAngle[2]);
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


