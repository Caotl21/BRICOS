#include "stm32f4xx.h"
#include "stm32f4xx_conf.h"
#include <stdio.h>
#include <stdarg.h>
#include "Serial.h"
//#include "im948_CMD.h"
//#include "Serial.h"
//#include "OLED.h"
//#include "TaskScheduler.h"
//#include "JY901B.h"


uint8_t Serial_RxData;		//定义串口接收的数据变量
uint8_t Serial_RxFlag;		//定义串口接收的标志位变量

volatile uint8_t Serial_RxPWM_Control[64] = {0};     // 接收的PWM值数组
uint16_t Serial_RxPWM_Servo[2] = {0};        // 接收的舵机PWM值
uint16_t Serial_RxPWM_Light[2] = {0};        // 接收的LED PWM值
uint8_t Serial_RxPacket[MAX_PACKET_SIZE];          // 接收数据包缓冲区
uint8_t Serial_TxPacket[MAX_PACKET_SIZE];          // 发送数据包缓冲区
static void Thruster_SetPWM(volatile uint8_t *RxBuf);

// 传感器数据（模拟数据）
SensorData_t Serial_SensorData = {0};
/*	USART1_TX:PA9	USART1_RX:PA10
	USART2_TX:PA2	USART2_RX:PA3
	USART3_TX:PB10	USART3_RX:PB11
	UART4_TX:PA0	UART4_RX:PA1
	UART5_TX:PC12	UART5_RX:PD2
	USART6_TX:PC8	USART6_RX:PC7
*/

/**
  * 函    数：串口1 TXD->PA9, RXD->PA10 初始化
  * 参    数：无
  * 返 回 值：无
  */
void USART1_Init(uint32_t baudrate)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    //NVIC_InitTypeDef NVIC_InitStructure;

    // 1. 开启时钟
    // 【注意】PA口挂在 AHB1，USART1 挂在 APB2 (高速总线)
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);

    // 2. 配置 GPIO (PA9=TX, PA10=RX)
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9 | GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF; // 【关键】必须是复用模式
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP; // RX 上拉是个好习惯，防噪
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // 3. 【关键】连接引脚复用映射
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource9, GPIO_AF_USART1);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource10, GPIO_AF_USART1);

    // 4. 配置 USART 参数
    USART_InitStructure.USART_BaudRate = baudrate;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &USART_InitStructure);
	
	// 【新增 1】开启串口接收 DMA 请求
	USART_DMACmd(USART1, USART_DMAReq_Rx, ENABLE);
	
	// 【新增 2】开启空闲中断 (IDLE Interrupt)
    // 意思是：USART1，当你发现数据线有一段时间没信号了（一包发完了），再喊 CPU
    //USART_ITConfig(USART1, USART_IT_IDLE, ENABLE);

    // 5. 配置接收中断 (IM948 吐数据很快，推荐用中断接)
    //USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);

//    NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
//    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0; // 优先级高一点，防止丢数据
//    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
//    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
//    NVIC_Init(&NVIC_InitStructure);

    // 6. 开启串口
    USART_Cmd(USART1, ENABLE);
}

/**
  * 函    数：串口2 TXD->PA2, RXD->PA3 初始化
  * 参    数：无
  * 返 回 值：无
  */
void USART2_Init(uint32_t baudrate)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    // 1. 开启时钟
    // USART2 挂载在 APB1，GPIOA 挂载在 AHB1
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

    // 2. 引脚复用映射
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource2, GPIO_AF_USART2);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource3, GPIO_AF_USART2);

    // 3. GPIO 初始化
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2 | GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // 4. USART 参数配置
    USART_InitStructure.USART_BaudRate = baudrate; // 注意确认这个IMU的波特率
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART2, &USART_InitStructure);

    // 5. 中断配置 (IMU 数据量大且快，必须用中断)
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);

    // 设置优先级：IMU 的优先级要高 (数字越小优先级越高)
    // 防止打印日志的时候，IMU 突发数据导致丢包
    NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0; // 抢占优先级 0 (最高)
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    // 6. 使能串口
    USART_Cmd(USART2, ENABLE);
}

/**
  * 函    数：串口3 TXD->PB10, RXD->PB11 初始化
  * 参    数：无
  * 返 回 值：无
  */
void Serial_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    // 1. 开启时钟
    // USART3 挂载在 APB1，GPIOB 挂载在 AHB1
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);

    // 2. 引脚复用映射 (F4 必须步骤)
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource10, GPIO_AF_USART3);
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource11, GPIO_AF_USART3);

    // 3. GPIO 初始化
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10 | GPIO_Pin_11;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;    // 复用模式
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;  // 推挽输出
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;    // 上拉
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    // 4. USART 参数配置
    USART_InitStructure.USART_BaudRate = 115200;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART3, &USART_InitStructure);

    // 5. 中断配置 (用于接收电脑发的指令)
    //USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);
    USART_ITConfig(USART3, USART_IT_IDLE, ENABLE); // 开启空闲中断

    // 设置优先级：调试串口优先级稍微低一点，不要打断 IMU 的数据读取
    NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2; // 抢占优先级 2 (较低)
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    // 6. 使能串口
    USART_Cmd(USART3, ENABLE);
}

/**
  * 函    数：串口发送一个字节
  * 参    数：Byte 要发送的一个字节
  * 返 回 值：无
  */
void Serial_SendByte(uint8_t Byte)
{
	USART_SendData(USART3, Byte);		//将字节数据写入数据寄存器，写入后USART自动生成时序波形
	while (USART_GetFlagStatus(USART3, USART_FLAG_TXE) == RESET);	//等待发送完成
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
// 描述: 使用USART1向IM948同步发送数据，等待发送完毕
// 输入: buf[Len]=要发送的内容
// 返回: 返回发送字节数
//------------------------------------------------------------------------------
int IM948_Write(const U8 *buf, int Len)
{
	int i;
	
	for (i = 0; i < Len; i++)
	{
		while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
		USART_SendData(USART1, buf[i]);
	}
	while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);
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
  * 函    数：串口发送自定义数据包
  * 参    数：发送的数据类型、数据长度和数据载荷
  * 返 回 值：无
  */
 void Serial_SendPacket2PC(uint8_t DataType, uint8_t DataLength, uint8_t *DataPayload)
 {
	// 包头2 + 类型1 + 长度1 + 数据n + 校验1 + 包尾2 = n+7字节
    uint8_t packet[DataLength + 7];  // 数据包缓冲区
    uint8_t index = 0;
    uint8_t crc;
    
    // 构建数据包
    packet[index++] = PACKET_START_BYTE1;       // 包头字节1
    packet[index++] = PACKET_START_BYTE2;       // 包头字节2
    packet[index++] = DataType;                 // 数据类型
    packet[index++] = DataLength;               // 数据长度
    for (uint8_t i = 0; i < DataLength; i++) {  // 数据载荷
        packet[index++] = DataPayload[i];
    }
    crc = Serial_CRC8(&packet[2], index - 2);   // 计算校验位（从数据类型开始到数据结束）
    packet[index++] = crc;                      // 校验位
    packet[index++] = PACKET_END_BYTE1;         // 包尾字节1
    packet[index++] = PACKET_END_BYTE2;         // 包尾字节2
    
    // 发送数据包
    Serial_SendArray(packet, index);
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
    packet[index++] = (uint8_t)(Serial_SensorData.accel_x1 >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.accel_x1 & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.accel_y1 >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.accel_y1 & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.accel_z1 >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.accel_z1 & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.gyro_x1 >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.gyro_x1 & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.gyro_y1 >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.gyro_y1 & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.gyro_z1 >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.gyro_z1 & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.Wquat_2 >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.Wquat_2 & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.Xquat_2 >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.Xquat_2 & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.Yquat_2 >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.Yquat_2 & 0xFF);
	packet[index++] = (uint8_t)(Serial_SensorData.Zquat_2 >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.Zquat_2 & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.press >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.press & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.temperature_water >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.temperature_water & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.humidity >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.humidity & 0xFF);
	packet[index++] = (uint8_t)(Serial_SensorData.temperature_AUV >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.temperature_AUV & 0xFF);
	packet[index++] = (uint8_t)(Serial_SensorData.voltage >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.voltage & 0xFF);
	packet[index++] = (uint8_t)(Serial_SensorData.current >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.current & 0xFF);
	
    
    // 计算校验位（从数据类型开始到数据结束）
    crc = Serial_CRC8(&packet[2], index - 2);  // 跳过包头
    packet[index++] = crc;                      // 校验位
    packet[index++] = PACKET_END_BYTE1;         // 包尾字节1
    packet[index++] = PACKET_END_BYTE2;         // 包尾字节2
    
    // 发送数据包
    Serial_SendArray(packet, index);
}

/**
  * 函    数：高频串口发送传感器数据包
  * 参    数：无
  * 返 回 值：无
  */
void Fast_Serial_SendSensorPacket(void)
{
	// 包头2 + 类型1 + 长度1 + 数据40 + 校验1 + 包尾2 = 47字节
    uint8_t packet[47];  
    uint8_t index = 0;
    uint8_t crc;
    
    // 构建数据包
    packet[index++] = PACKET_START_BYTE1;       // 包头字节1
    packet[index++] = PACKET_START_BYTE2;       // 包头字节2
    packet[index++] = DATA_TYPE_FAST;         // 数据类型
    packet[index++] = FAST_SENSOR_DATA_LENGTH;       // 数据长度
    
    // 传感器数据
    packet[index++] = (uint8_t)(Serial_SensorData.accel_x1 >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.accel_x1 & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.accel_y1 >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.accel_y1 & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.accel_z1 >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.accel_z1 & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.gyro_x1 >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.gyro_x1 & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.gyro_y1 >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.gyro_y1 & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.gyro_z1 >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.gyro_z1 & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.angle_x1 >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.angle_x1 & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.angle_y1 >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.angle_y1 & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.angle_z1 >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.angle_z1 & 0xFF);
	packet[index++] = (uint8_t)(Serial_SensorData.accel_x2 >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.accel_x2 & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.accel_y2 >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.accel_y2 & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.accel_z2 >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.accel_z2 & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.gyro_x2 >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.gyro_x2 & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.gyro_y2 >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.gyro_y2 & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.gyro_z2 >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.gyro_z2 & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.Wquat_2 >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.Wquat_2 & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.Xquat_2 >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.Xquat_2 & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.Yquat_2 >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.Yquat_2 & 0xFF);
	packet[index++] = (uint8_t)(Serial_SensorData.Zquat_2 >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.Zquat_2 & 0xFF);
	packet[index++] = (uint8_t)(Serial_SensorData.press >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.press & 0xFF);
    
	
    
    // 计算校验位（从数据类型开始到数据结束）
    crc = Serial_CRC8(&packet[2], index - 2);  // 跳过包头
    packet[index++] = crc;                      // 校验位
    packet[index++] = PACKET_END_BYTE1;         // 包尾字节1
    packet[index++] = PACKET_END_BYTE2;         // 包尾字节2
    
    // 发送数据包
    Serial_SendArray(packet, index);
}

/**
  * 函    数：低频串口发送传感器数据包
  * 参    数：无
  * 返 回 值：无
  */
void Slow_Serial_SendSensorPacket(void)
{
	// 包头2 + 类型1 + 长度1 + 数据10 + 校验1 + 包尾2 = 17字节
    uint8_t packet[17];  
    uint8_t index = 0;
    uint8_t crc;
    
    // 构建数据包
    packet[index++] = PACKET_START_BYTE1;       // 包头字节1
    packet[index++] = PACKET_START_BYTE2;       // 包头字节2
    packet[index++] = DATA_TYPE_SLOW;         // 数据类型
    packet[index++] = SLOW_SENSOR_DATA_LENGTH;       // 数据长度
    
    // 传感器数据
    packet[index++] = (uint8_t)(Serial_SensorData.temperature_water >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.temperature_water & 0xFF);
    packet[index++] = (uint8_t)(Serial_SensorData.humidity >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.humidity & 0xFF);
	packet[index++] = (uint8_t)(Serial_SensorData.temperature_AUV >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.temperature_AUV & 0xFF);
	packet[index++] = (uint8_t)(Serial_SensorData.voltage >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.voltage & 0xFF);
	packet[index++] = (uint8_t)(Serial_SensorData.current >> 8);
    packet[index++] = (uint8_t)(Serial_SensorData.current & 0xFF);
	
    
    // 计算校验位（从数据类型开始到数据结束）
    crc = Serial_CRC8(&packet[2], index - 2);  // 跳过包头
    packet[index++] = crc;                      // 校验位
    packet[index++] = PACKET_END_BYTE1;         // 包尾字节1
    packet[index++] = PACKET_END_BYTE2;         // 包尾字节2
    
    // 发送数据包
    Serial_SendArray(packet, index);
}

void USART1_IRQHandler(void)
{
//    uint8_t temp;
//    
//    // 检测空闲中断
//    if(USART_GetITStatus(USART1, USART_IT_IDLE) != RESET)
//    {
//        // 1. 清除 IDLE 标志 (读取 SR 后读取 DR 序列)
//        temp = USART1->SR;
//        temp = USART1->DR; 
//        
//        // 2. 计算当前 DMA 写到哪里了
//        // DMA_GetCurrDataCounter 返回的是“剩余传输量” (从 FifoSize 减到 0)
//        // 所以：当前位置 = 总大小 - 剩余大小
//        // 在循环模式下，这个逻辑是自动回环的
//        Uart1Fifo.In = FifoSize - DMA_GetCurrDataCounter(DMA2_Stream2);
//        
//        // 3. 通知消费者任务 (置位事件标志)
//        // 此时，Uart1Fifo.RxBuf 里的数据已经由 DMA 填好了，In 指针也更新了
//        g_event_im948_received = 1;
//        
//        // 不需要清除 PendingBit，上面的 SR+DR 读取操作已经清除了
//    }
}

void USART3_IRQHandler(void)
{
    uint8_t temp;
    
    // 检测空闲中断
    if(USART_GetITStatus(USART3, USART_IT_IDLE) != RESET)
    {
        // 1. 清除 IDLE 标志 (读取 SR 后读取 DR 序列)
        temp = USART3->SR;
        temp = USART3->DR; 

        DMA_Cmd(DMA1_Stream1, DISABLE);
        while((DMA1_Stream1->CR & DMA_SxCR_EN) != 0);

        if (Serial_RxPWM_Control[0] != PACKET_START_BYTE1 || Serial_RxPWM_Control[1] != PACKET_START_BYTE2)
        {
            DMA_Cmd(DMA1_Stream1, ENABLE);
            return; // 如果包头不对，直接返回
        }

        switch(Serial_RxPWM_Control[2]) // 根据数据类型处理
        {
            case DATA_TYPE_Thrusters:
                if (Serial_RxPWM_Control[3] == PWM_DATA_LENGTH)
                {
                    // 记得填入影子寄存器
                    Thruster_SetPWM(Serial_RxPWM_Control);
                }
                break;
            // 可以添加更多数据类型的处理
            default:
                break;
        }
        
        // DMA重置
        DMA1->LIFCR = (uint32_t)(0x3D << 6);
        DMA1_Stream1->NDTR = ControlSignal_RxBuf_Size;
        DMA1_Stream1->M0AR = (uint32_t)Serial_RxPWM_Control;
        DMA_Cmd(DMA1_Stream1, ENABLE);

        
        // 不需要清除 PendingBit，上面的 SR+DR 读取操作已经清除了
    }
}

static void Thruster_SetPWM(volatile uint8_t *RxBuf)
{
    TIM3->CCR1 = (uint16_t)(RxBuf[4] << 8) | RxBuf[5];  // Thruster 1
    TIM3->CCR2 = (uint16_t)(RxBuf[6] << 8) | RxBuf[7];  // Thruster 2
    TIM3->CCR3 = (uint16_t)(RxBuf[8] << 8) | RxBuf[9];  // Thruster 3
    TIM3->CCR4 = (uint16_t)(RxBuf[10] << 8) | RxBuf[11];  // Thruster 4
    
    TIM4->CCR1 = (uint16_t)(RxBuf[12] << 8) | RxBuf[13];  // Thruster 5
    TIM4->CCR2 = (uint16_t)(RxBuf[14] << 8) | RxBuf[15];  // Thruster 6

}

//void USART1_IRQHandler(void)
//{
//    if (USART_GetFlagStatus(USART1, USART_IT_RXNE) == SET)
//    {
//		
//        U16 RxByte = USART_ReceiveData(USART1);
//		//Cmd_GetPkt(RxByte);
//        IM948_Fifo_in(RxByte);
//		g_event_im948_received = 1;
//		//printf("get new data!");
//		
//        USART_ClearITPendingBit(USART1, USART_IT_RXNE);
//    }
//}

/**
  * 函    数：USART3中断函数
  * 参    数：无
  * 返 回 值：无
  * 注意事项：此函数为中断函数，无需调用，中断触发后自动执行
  *           函数名为预留的指定名称，可以从启动文件复制
  *           请确保函数名正确，不能有任何差异，否则中断函数将不能进入
  */
//void USART3_IRQHandler(void)
//{
//	if (USART_GetITStatus(USART3, USART_IT_RXNE) == SET)		//判断是否是USART3的接收事件触发的中断
//	{
//		Serial_RxData = USART_ReceiveData(USART3);				//读取数据寄存器，存放在接收的数据变量
//		Serial_RxFlag = 1;										//置接收标志位变量为1
//		g_event_pwm_received = 1;
//		
//		USART_ClearITPendingBit(USART3, USART_IT_RXNE);			//清除USART3的RXNE标志位
//																//读取数据寄存器会自动清除此标志位
//																//如果已经读取了数据寄存器，也可以不执行此代码
//	}
//}
//void UART4_IRQHandler(void)
//{
//    if (USART_GetFlagStatus(UART4, USART_IT_RXNE) == SET)
//    {
//		
//        U16 RxByte = USART_ReceiveData(UART4);
//		//Cmd_GetPkt(RxByte);
//        Fifo_in(RxByte);
//        g_event_im948_received = 1;
//		
//        USART_ClearITPendingBit(UART4, USART_IT_RXNE);
//    }
//}


//}


// void USART3_IRQHandler(void)
// {
//     static uint8_t RxState = 0;         // 状态机状态
//     static uint8_t DataType = 0;        // 数据类型
//     static uint8_t DataLength = 0;      // 数据长度
//     static uint8_t pRxPacket = 0;       // 数据包位置指针
//     static uint8_t ReceivedCRC = 0;     // 接收到的校验值
    
//     if (USART_GetITStatus(USART3, USART_IT_RXNE) == SET) //判断是否是USART3的接收事件触发的中断
//     {
//         uint8_t RxData = USART_ReceiveData(USART3);
        
//         switch (RxState)
//         {
//             case 0: // 等待起始字节1
//                 if (RxData == PACKET_START_BYTE1) {
//                     RxState = 1;
//                 }
//                 break;
                
//             case 1: // 等待起始字节2
//                 if (RxData == PACKET_START_BYTE2) {
//                     RxState = 2;
//                     pRxPacket = 0;  // 重置数据包指针
//                 } else {
//                     RxState = 0; // 如果不是起始字节2，重新开始
//                 }
//                 break;
                
//             case 2: // 接收数据类型
//                 DataType = RxData;
//                 Serial_RxPacket[pRxPacket++] = RxData;
//                 RxState = 3;
//                 break;
                
//             case 3: // 接收数据长度
//                 DataLength = RxData;
//                 Serial_RxPacket[pRxPacket++] = RxData;
                
//                 // 验证数据长度是否合法
//                 if ((DataType == DATA_TYPE_Thrusters && DataLength == PWM_DATA_LENGTH) ||
//                     (DataType == DATA_TYPE_SENSOR && DataLength == SENSOR_DATA_LENGTH) ||
//                     (DataType == DATA_TYPE_SERVO && DataLength == SERVO_DATA_LENGTH) ||
//                     (DataType == DATA_TYPE_LIGHT && DataLength == LIGHT_DATA_LENGTH)) {
//                     RxState = 4;
//                 } else {
//                     RxState = 0; // 数据长度不匹配，重新开始
//                 }
//                 break;
                
//             case 4: // 接收数据负载
//                 Serial_RxPacket[pRxPacket++] = RxData;
//                 // 检查是否接收完所有数据 (+2是因为包含了数据类型和长度)
//                 if (pRxPacket >= (DataLength + 2)) {
//                     RxState = 5;
//                 }
//                 break;
                
//             case 5: // 接收校验位
//                 ReceivedCRC = RxData;
//                 RxState = 6;
//                 break;
                
//             case 6: // 等待结束字节1
//                 if (RxData == PACKET_END_BYTE1) {
//                     RxState = 7;
//                 } else {
//                     RxState = 0; // 如果不是结束字节1，重新开始
//                 }
//                 break;
                
//             case 7: // 等待结束字节2
//                 if (RxData == PACKET_END_BYTE2) {
//                     // 完整数据包接收完成，进行校验
//                     uint8_t CalculatedCRC = Serial_CRC8(Serial_RxPacket, pRxPacket);
//                     //if (CalculatedCRC == ReceivedCRC) {
// 					if(1){
//                         // CRC校验成功，处理数据
//                         //解码推进器电机PWM
//                         if (DataType == DATA_TYPE_Thrusters) {
//                             // 解析PWM数据
//                             for (uint8_t i = 0; i < 6; i++) {
//                                 // Serial_RxPacket[2 + i*2]是高8位，Serial_RxPacket[2 + i*2 + 1]是低8位
//                                 Serial_RxPWM_Thruster[i] = (Serial_RxPacket[2 + i*2] << 8) | Serial_RxPacket[2 + i*2 + 1];
//                                 // 数据范围检查
//                                 if (Serial_RxPWM_Thruster[i] > 5000) {
//                                     Serial_RxPWM_Thruster[i] = 5000;
//                                 }
//                             }
//                             Serial_RxFlag = 1; // 设置接收完成标志
//                         }
//                         //解码舵机PWM
//                         if (DataType == DATA_TYPE_SERVO) {
//                             // 解析PWM数据
//                             for (uint8_t i = 0; i < 2; i++) {
//                                 // Serial_RxPacket[2 + i*2]是高8位，Serial_RxPacket[2 + i*2 + 1]是低8位
//                                 Serial_RxPWM_Servo[i] = (Serial_RxPacket[2 + i*2] << 8) | Serial_RxPacket[2 + i*2 + 1];
//                                 // 数据范围检查
//                                 if (Serial_RxPWM_Servo[i] > 2500) {
//                                     Serial_RxPWM_Servo[i] = 2500;
//                                 }
//                             }
//                             Serial_RxFlag = 1; // 设置接收完成标志
							
//                         }
//                         //解码探照灯PWM
//                         if (DataType == DATA_TYPE_LIGHT) {
//                             // 解析PWM数据
//                             for (uint8_t i = 0; i < 2; i++) {
//                                 // Serial_RxPacket[2 + i*2]是高8位，Serial_RxPacket[2 + i*2 + 1]是低8位
//                                 Serial_RxPWM_Light[i] = (Serial_RxPacket[2 + i*2] << 8) | Serial_RxPacket[2 + i*2 + 1];
//                                 // 数据范围检查
//                                 if (Serial_RxPWM_Light[i] > 2500) {
//                                     Serial_RxPWM_Light[i] = 2500;
//                                 }
//                             }
//                             Serial_RxFlag = 1; // 设置接收完成标志
//                         }
//                     }
//                     // 无论校验是否成功，都重新开始
//                 }
//                 RxState = 0; // 重新开始
//                 break;
                
//             default:
//                 RxState = 0;
//                 break;
//         }
        
//         USART_ClearITPendingBit(USART3, USART_IT_RXNE);
//     }
// }
