#include "stm32f10x.h"                  // Device header
#include <stdio.h>
#include <stdarg.h>
#include "im948_CMD.h"
#include "Serial.h"
#include "OLED.h"
#include "TaskScheduler.h"


uint8_t Serial_RxData;		//定义串口接收的数据变量
uint8_t Serial_RxFlag;		//定义串口接收的标志位变量

uint16_t Serial_RxPWM_Thruster[6] = {0};     // 接收的PWM值数组
uint16_t Serial_RxPWM_Servo[2] = {0};        // 接收的舵机PWM值
uint16_t Serial_RxPWM_Light[2] = {0};        // 接收的LED PWM值
uint8_t Serial_RxPacket[MAX_PACKET_SIZE];          // 接收数据包缓冲区
uint8_t Serial_TxPacket[MAX_PACKET_SIZE];          // 发送数据包缓冲区

// 传感器数据（模拟数据）
SensorData_t Serial_SensorData = {
    .accel_x = 100, .accel_y = 200, .accel_z = 300,
    .gyro_x = 10, .gyro_y = 20, .gyro_z = 30,
    .mag_x = 100, .mag_y = 200, .mag_z = 300,
    .angle_x = 10, .angle_y = 20, .angle_z = 30,
    .depth = 1500, .temperature = 250,
    .humidity = 50
};

/**
  * 函    数：串口1 TXD->PA9, RXD->PA10 串口2 TXD->PA2, RXD->PA3 初始化
  * 参    数：无
  * 返 回 值：无
  */
void Serial_Init(void)
{
	/*开启时钟*/
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);	//开启USART1的时钟
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);	//开启USART2的时钟
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);	//开启GPIOA的时钟
	
	/*GPIO初始化 - UART1 (PA9-TX, PA10-RX)*/
	GPIO_InitTypeDef GPIO_InitStructure;
	
	// UART1 TX (PA9) - 复用推挽输出
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);					//将PA9引脚初始化为复用推挽输出
	
	// UART1 RX (PA10) - 上拉输入
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);					//将PA10引脚初始化为上拉输入

	/*GPIO初始化 - UART2 (PA2-TX, PA3-RX)*/
    // UART2 TX (PA2) - 复用推挽输出
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);					//将PA2引脚初始化为复用推挽输出	

    // UART2 RX (PA3) - 上拉输入
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);					//将PA3引脚初始化为上拉输入
	
	/*USART初始化*/
	USART_InitTypeDef USART_InitStructure;					//定义结构体变量
	USART_InitStructure.USART_BaudRate = 115200;				//波特率
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;	//硬件流控制，不需要
	USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;	//模式，发送模式和接收模式均选择
	USART_InitStructure.USART_Parity = USART_Parity_No;		//奇偶校验，不需要
	USART_InitStructure.USART_StopBits = USART_StopBits_1;	//停止位，选择1位
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;		//字长，选择8位
	
	USART_Init(USART1, &USART_InitStructure);				//将结构体变量交给USART_Init，配置USART1
	USART_Init(USART2, &USART_InitStructure);				//将结构体变量交给USART_Init，配置USART2
	
	/*中断输出配置*/
	USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);			//开启串口接收数据的中断
	USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);			//开启串口接收数据的中断
	
	/*NVIC中断分组*/
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);			//配置NVIC为分组2
	
	/*NVIC配置*/
	NVIC_InitTypeDef NVIC_InitStructure;					//定义结构体变量
	NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;		//选择配置NVIC的USART1线
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;			//指定NVIC线路使能
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;		//指定NVIC线路的抢占优先级为1
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;		//指定NVIC线路的响应优先级为1
	NVIC_Init(&NVIC_InitStructure);							//将结构体变量交给NVIC_Init，配置NVIC外设

	NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;		//选择配置NVIC的USART2线
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;		//指定NVIC线路的抢占优先级为2
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 2;		//指定NVIC线路的响应优先级为1	
	NVIC_Init(&NVIC_InitStructure);							//将结构体变量交给NVIC_Init，配置NVIC外设
	
	/*USART使能*/
	USART_Cmd(USART1, ENABLE);								//使能USART1，串口开始运行
	USART_Cmd(USART2, ENABLE);								//使能USART2，串口开始运行
}

/**
  * 函    数：串口发送一个字节
  * 参    数：Byte 要发送的一个字节
  * 返 回 值：无
  */
void Serial_SendByte(uint8_t Byte)
{
	USART_SendData(USART1, Byte);		//将字节数据写入数据寄存器，写入后USART自动生成时序波形
	while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);	//等待发送完成
	/*下次写入数据寄存器会自动清除发送完成标志位，故此循环后，无需清除标志位*/
}

/**
  * 函    数：串口发送一个数组
  * 参    数：Array 要发送数组的首地址
  * 参    数：Length 要发送数组的长度
  * 返 回 值：无
  */
void Serial_SendArray(uint8_t *Array, uint16_t Length)
{
	uint16_t i;
	for (i = 0; i < Length; i ++)		//遍历数组
	{
		Serial_SendByte(Array[i]);		//依次调用Serial_SendByte发送每个字节数据
	}
}

//------------------------------------------------------------------------------
// 描述: Uart同步发送数据，等待发送完毕
// 输入: n=串口号, buf[Len]=要发送的内容
// 返回: 返回发送字节数
//------------------------------------------------------------------------------
int UART_Write(U8 n, const U8 *buf, int Len)
{
    int i;

    switch (n)
    {
    case 0: // 串口1
        for (i = 0; i < Len; i++)
        {
            while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
            USART_SendData(USART1, buf[i]);
        }
        while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);
        break;
    case 1: // 串口2
        for (i = 0; i < Len; i++)
        {
            while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);
            USART_SendData(USART2, buf[i]);
        }
        while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET);
        break;
    case 2: // 串口3
        for (i = 0; i < Len; i++)
        {
            while (USART_GetFlagStatus(USART3, USART_FLAG_TXE) == RESET);
            USART_SendData(USART3, buf[i]);
        }
        while (USART_GetFlagStatus(USART3, USART_FLAG_TC) == RESET);
        break;

    default:
        return 0;
    }

    return Len;
}

/**
  * 函    数：串口发送一个字符串
  * 参    数：String 要发送字符串的首地址
  * 返 回 值：无
  */
void Serial_SendString(char *String)
{
	uint8_t i;
	for (i = 0; String[i] != '\0'; i ++)//遍历字符数组（字符串），遇到字符串结束标志位后停止
	{
		Serial_SendByte(String[i]);		//依次调用Serial_SendByte发送每个字节数据
	}
}

/**
  * 函    数：次方函数（内部使用）
  * 返 回 值：返回值等于X的Y次方
  */
uint32_t Serial_Pow(uint32_t X, uint32_t Y)
{
	uint32_t Result = 1;	//设置结果初值为1
	while (Y --)			//执行Y次
	{
		Result *= X;		//将X累乘到结果
	}
	return Result;
}

/**
  * 函    数：串口发送数字
  * 参    数：Number 要发送的数字，范围：0~4294967295
  * 参    数：Length 要发送数字的长度，范围：0~10
  * 返 回 值：无
  */
void Serial_SendNumber(uint32_t Number, uint8_t Length)
{
	uint8_t i;
	for (i = 0; i < Length; i ++)		//根据数字长度遍历数字的每一位
	{
		Serial_SendByte(Number / Serial_Pow(10, Length - i - 1) % 10 + '0');	//依次调用Serial_SendByte发送每位数字
	}
}

/**
  * 函    数：使用printf需要重定向的底层函数
  * 参    数：保持原始格式即可，无需变动
  * 返 回 值：保持原始格式即可，无需变动
  */
int fputc(int ch, FILE *f)
{
	Serial_SendByte(ch);			//将printf的底层重定向到自己的发送字节函数
	return ch;
}

/**
  * 函    数：自己封装的prinf函数
  * 参    数：format 格式化字符串
  * 参    数：... 可变的参数列表
  * 返 回 值：无
  */
void Serial_Printf(char *format, ...)
{
	char String[100];				//定义字符数组
	va_list arg;					//定义可变参数列表数据类型的变量arg
	va_start(arg, format);			//从format开始，接收参数列表到arg变量
	vsprintf(String, format, arg);	//使用vsprintf打印格式化字符串和参数列表到字符数组中
	va_end(arg);					//结束变量arg
	Serial_SendString(String);		//串口发送字符数组（字符串）
}

/**
  * 函    数：获取串口接收标志位
  * 参    数：无
  * 返 回 值：串口接收标志位，范围：0~1，接收到数据后，标志位置1，读取后标志位自动清零
  */
uint8_t Serial_GetRxFlag(void)
{
	if (Serial_RxFlag == 1)			//如果标志位为1
	{
		Serial_RxFlag = 0;
		return 1;					//则返回1，并自动清零标志位
	}
	return 0;						//如果标志位为0，则返回0
}

/**
  * 函    数：获取串口接收的数据
  * 参    数：无
  * 返 回 值：接收的数据，范围：0~255
  */
uint8_t Serial_GetRxData(void)
{
	return Serial_RxData;			//返回接收的数据变量
}

/**
  * 函    数：CRC8校验计算
  * 参    数：data 数据指针，length 数据长度
  * 返 回 值：CRC8校验值
  */
uint8_t Serial_CRC8(uint8_t *data, uint8_t length)
{
    uint8_t crc = 0x00;
    uint8_t i, j;
    
    for (i = 0; i < length; i++) {
        crc ^= data[i];
        for (j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x07;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

/**
  * 函    数：串口发送传感器数据包
  * 参    数：无
  * 返 回 值：无
  */
void Serial_SendSensorPacket(void)
{
	// 包头2 + 类型1 + 长度1 + 数据30 + 校验1 + 包尾2 = 37字节
    uint8_t packet[37];  
    uint8_t index = 0;
    uint8_t crc;
    
    // 构建数据包
    packet[index++] = PACKET_START_BYTE1;       // 包头字节1
    packet[index++] = PACKET_START_BYTE2;       // 包头字节2
    packet[index++] = DATA_TYPE_SENSOR;         // 数据类型
    packet[index++] = SENSOR_DATA_LENGTH;       // 数据长度
    
    // 传感器数据
    packet[index++] = (uint8_t)(Serial_SensorData.accel_x >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.accel_x & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.accel_y >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.accel_y & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.accel_z >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.accel_z & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.gyro_x >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.gyro_x & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.gyro_y >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.gyro_y & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.gyro_z >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.gyro_z & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.mag_x >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.mag_x & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.mag_y >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.mag_y & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.mag_z >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.mag_z & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.angle_x >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.angle_x & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.angle_y >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.angle_y & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.angle_z >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.angle_z & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.depth >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.depth & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.temperature >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.temperature & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.humidity >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.humidity & 0xFF);
    
    // 计算校验位（从数据类型开始到数据结束）
    crc = Serial_CRC8(&packet[2], index - 2);  // 跳过包头
    packet[index++] = crc;                      // 校验位
    packet[index++] = PACKET_END_BYTE1;         // 包尾字节1
    packet[index++] = PACKET_END_BYTE2;         // 包尾字节2
    
    // 发送数据包
    Serial_SendArray(packet, index);
}

/**
  * 函    数：USART1中断函数
  * 参    数：无
  * 返 回 值：无
  * 注意事项：此函数为中断函数，无需调用，中断触发后自动执行
  *           函数名为预留的指定名称，可以从启动文件复制
  *           请确保函数名正确，不能有任何差异，否则中断函数将不能进入
  */
void USART1_IRQHandler(void)
{
	if (USART_GetITStatus(USART1, USART_IT_RXNE) == SET)		//判断是否是USART1的接收事件触发的中断
	{
		Serial_RxData = USART_ReceiveData(USART1);				//读取数据寄存器，存放在接收的数据变量
		Serial_RxFlag = 1;										//置接收标志位变量为1
		g_event_pwm_received = 1;
		
		USART_ClearITPendingBit(USART1, USART_IT_RXNE);			//清除USART1的RXNE标志位
																//读取数据寄存器会自动清除此标志位
																//如果已经读取了数据寄存器，也可以不执行此代码
	}
}
void USART2_IRQHandler(void)
{
    if (USART_GetFlagStatus(USART2, USART_IT_RXNE) == SET)
    {
		
        U16 RxByte = USART_ReceiveData(USART2);
		//Cmd_GetPkt(RxByte);
        Fifo_in(RxByte);
        g_event_im948_received = 1;
		
        USART_ClearITPendingBit(USART2, USART_IT_RXNE);
    }
}

//void USART1_IRQHandler(void)
//{
//    static uint8_t RxState = 0;         // 状态机状态
//    static uint8_t DataType = 0;        // 数据类型
//    static uint8_t DataLength = 0;      // 数据长度
//    static uint8_t pRxPacket = 0;       // 数据包位置指针
//    static uint8_t ReceivedCRC = 0;     // 接收到的校验值
//    
//    if (USART_GetITStatus(USART1, USART_IT_RXNE) == SET) //判断是否是USART1的接收事件触发的中断
//    {
//        uint8_t RxData = USART_ReceiveData(USART1);
//        
//        switch (RxState)
//        {
//            case 0: // 等待起始字节1
//                if (RxData == PACKET_START_BYTE1) {
//                    RxState = 1;
//                }
//                break;
//                
//            case 1: // 等待起始字节2
//                if (RxData == PACKET_START_BYTE2) {
//                    RxState = 2;
//                    pRxPacket = 0;  // 重置数据包指针
//                } else {
//                    RxState = 0; // 如果不是起始字节2，重新开始
//                }
//                break;
//                
//            case 2: // 接收数据类型
//                DataType = RxData;
//                Serial_RxPacket[pRxPacket++] = RxData;
//                RxState = 3;
//                break;
//                
//            case 3: // 接收数据长度
//                DataLength = RxData;
//                Serial_RxPacket[pRxPacket++] = RxData;
//                
//                // 验证数据长度是否合法
//                if ((DataType == DATA_TYPE_Thrusters && DataLength == PWM_DATA_LENGTH) ||
//                    (DataType == DATA_TYPE_SENSOR && DataLength == SENSOR_DATA_LENGTH) ||
//                    (DataType == DATA_TYPE_SERVO && DataLength == SERVO_DATA_LENGTH) ||
//                    (DataType == DATA_TYPE_LIGHT && DataLength == LIGHT_DATA_LENGTH)) {
//                    RxState = 4;
//                } else {
//                    RxState = 0; // 数据长度不匹配，重新开始
//                }
//                break;
//                
//            case 4: // 接收数据负载
//                Serial_RxPacket[pRxPacket++] = RxData;
//                // 检查是否接收完所有数据 (+2是因为包含了数据类型和长度)
//                if (pRxPacket >= (DataLength + 2)) {
//                    RxState = 5;
//                }
//                break;
//                
//            case 5: // 接收校验位
//                ReceivedCRC = RxData;
//                RxState = 6;
//                break;
//                
//            case 6: // 等待结束字节1
//                if (RxData == PACKET_END_BYTE1) {
//                    RxState = 7;
//                } else {
//                    RxState = 0; // 如果不是结束字节1，重新开始
//                }
//                break;
//                
//            case 7: // 等待结束字节2
//                if (RxData == PACKET_END_BYTE2) {
//                    // 完整数据包接收完成，进行校验
//                    uint8_t CalculatedCRC = Serial_CRC8(Serial_RxPacket, pRxPacket);
//                    //if (CalculatedCRC == ReceivedCRC) {
//					if(1){
//                        // CRC校验成功，处理数据
//                        //解码推进器电机PWM
//                        if (DataType == DATA_TYPE_Thrusters) {
//                            // 解析PWM数据
//                            for (uint8_t i = 0; i < 6; i++) {
//                                // Serial_RxPacket[2 + i*2]是高8位，Serial_RxPacket[2 + i*2 + 1]是低8位
//                                Serial_RxPWM_Thruster[i] = (Serial_RxPacket[2 + i*2] << 8) | Serial_RxPacket[2 + i*2 + 1];
//                                // 数据范围检查
//                                if (Serial_RxPWM_Thruster[i] > 5000) {
//                                    Serial_RxPWM_Thruster[i] = 5000;
//                                }
//                            }
//                            Serial_RxFlag = 1; // 设置接收完成标志
//                        }
//                        //解码舵机PWM
//                        if (DataType == DATA_TYPE_SERVO) {
//                            // 解析PWM数据
//                            for (uint8_t i = 0; i < 2; i++) {
//                                // Serial_RxPacket[2 + i*2]是高8位，Serial_RxPacket[2 + i*2 + 1]是低8位
//                                Serial_RxPWM_Servo[i] = (Serial_RxPacket[2 + i*2] << 8) | Serial_RxPacket[2 + i*2 + 1];
//                                // 数据范围检查
//                                if (Serial_RxPWM_Servo[i] > 2500) {
//                                    Serial_RxPWM_Servo[i] = 2500;
//                                }
//                            }
//                            Serial_RxFlag = 1; // 设置接收完成标志
//							
//                        }
//                        //解码探照灯PWM
//                        if (DataType == DATA_TYPE_LIGHT) {
//                            // 解析PWM数据
//                            for (uint8_t i = 0; i < 2; i++) {
//                                // Serial_RxPacket[2 + i*2]是高8位，Serial_RxPacket[2 + i*2 + 1]是低8位
//                                Serial_RxPWM_Light[i] = (Serial_RxPacket[2 + i*2] << 8) | Serial_RxPacket[2 + i*2 + 1];
//                                // 数据范围检查
//                                if (Serial_RxPWM_Light[i] > 2500) {
//                                    Serial_RxPWM_Light[i] = 2500;
//                                }
//                            }
//                            Serial_RxFlag = 1; // 设置接收完成标志
//                        }
//                    }
//                    // 无论校验是否成功，都重新开始
//                }
//                RxState = 0; // 重新开始
//                break;
//                
//            default:
//                RxState = 0;
//                break;
//        }
//        
//        USART_ClearITPendingBit(USART1, USART_IT_RXNE);
//    }
//}
