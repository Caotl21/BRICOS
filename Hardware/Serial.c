#include "stm32f4xx.h"
#include "stm32f4xx_conf.h"
#include <stdio.h>
#include <stdarg.h>
#include "Serial.h"

uint8_t Serial_RxData;		//定义串口接收的数据变量
uint8_t Serial_RxFlag;		//定义串口接收的标志位变量

/* ============ UART 函数 ============ */

static void USART3_Init(uint32_t baudrate)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    // 1. 开启时钟：USART3 挂在 APB1，GPIO B 挂在 AHB1
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);

    // 2. 配置引脚复用：将 PB10, PB11 映射到 AF7 (USART3)
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource10, GPIO_AF_USART3); // PB10 -> USART3_TX
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource11, GPIO_AF_USART3); // PB11 -> USART3_RX

    // 3. GPIO 初始化
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10 | GPIO_Pin_11;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;      // 复用模式
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;    // 推挽输出
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;      // 上拉
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    // 4. USART 参数配置
    USART_InitStructure.USART_BaudRate = baudrate;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART3, &USART_InitStructure);

    // 5. 开启接收中断 (RXNE)
    USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);

    // 6. NVIC 中断优先级配置
    NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;          // 改为 USART3 的中断通道
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;  // 抢占优先级
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;         // 子优先级
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    // 7. 使能 USART3
    USART_Cmd(USART3, ENABLE);
}

static void UART4_Init(uint32_t baudrate)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    // 1. 开启时钟
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_UART4, ENABLE);

    // 2. 配置引脚复用
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource0, GPIO_AF_UART4);   // PA0 -> UART4_TX
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource1, GPIO_AF_UART4);   // PA1 -> UART4_RX

    // 3. GPIO 初始化
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // 4. UART 参数配置
    USART_InitStructure.USART_BaudRate = baudrate;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(UART4, &USART_InitStructure);

    // 5. 开启接收中断
    USART_ITConfig(UART4, USART_IT_RXNE, ENABLE);

    // 6. NVIC 配置
    NVIC_InitStructure.NVIC_IRQChannel = UART4_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    // 7. 使能 UART4
    USART_Cmd(UART4, ENABLE);
}

/**
 * @brief  UART初始化 (USART3 + USART4, 115200, 8N1)
 *         UART4: PA0(TX), PA1(RX) - 调试/命令
 *         USART3: PB10(TX), PB11(RX)  - Ymodem通信
 */
void UART_Init(void)
{
    USART3_Init(115200);
    UART4_Init(115200);
}


/**
  * 函    数：使用UART4串口发送一个字节
  * 参    数：Byte 要发送的一个字节
  * 返 回 值：无
  */
void Serial_SendByte(uint8_t Byte)
{
	  while (USART_GetFlagStatus(UART4, USART_FLAG_TXE) == RESET);
    USART_SendData(UART4, Byte);
    while (USART_GetFlagStatus(UART4, USART_FLAG_TC) == RESET);
}

void UART_SendString(const char *str)
{
    Serial_SendString((char *)str);
}

void UART_SendByte(uint8_t byte)
{
    Serial_SendByte(byte);
}

/**
 * @brief  发送十六进制数
 */
void UART_SendHex(uint32_t value)
{
    char hex[] = "0123456789ABCDEF";
    int i;

    Serial_SendString("0x");
    for (i = 28; i >= 0; i -= 4)
    {
        Serial_SendByte(hex[(value >> i) & 0x0F]);
    }
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
 * @brief  发送整数（支持负数）
 */
void Serial_SendInt(int32_t value)
{
    char buffer[12];
    int i = 0;

    if (value == 0)
    {
        Serial_SendByte('0');
        return;
    }

    if (value < 0)
    {
        Serial_SendByte('-');
        value = -value;
    }

    while (value > 0)
    {
        buffer[i++] = '0' + (value % 10);
        value /= 10;
    }

    while (i > 0)
    {
        Serial_SendByte(buffer[--i]);
    }
}

/**
 * @brief  发送十六进制数
 */
void Serial_SendHex(uint32_t value)
{
    char hex[] = "0123456789ABCDEF";
    int i;

    Serial_SendString("0x");
    for (i = 28; i >= 0; i -= 4)
    {
        Serial_SendByte(hex[(value >> i) & 0x0F]);
    }
}

void UART_SendInt(int32_t value)
{
    Serial_SendInt(value);
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
