#include "main.h"

int fputc(int ch, FILE *f)
{
#if (Dbp_UartNum == 0)
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    USART_SendData(USART1, ch);
    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);
#elif (Dbp_UartNum == 1)
    while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);
    USART_SendData(USART2, ch);
    while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET);
#elif (Dbp_UartNum == 2)
    while (USART_GetFlagStatus(USART3, USART_FLAG_TXE) == RESET);
    USART_SendData(USART3, ch);
    while (USART_GetFlagStatus(USART3, USART_FLAG_TC) == RESET);
#endif
    return ch;
}

//------------------------------------------------------------------------------
// 描述: 打开串口
// 输入: n       =串口号(串口号为0~4，0~4对应CPU串口1~5);
//       speed   =波特率;
//       databits=数据位数(8);
//       stopbits=停止位数(1或2);
//       paparity=校验位('n'=无  'o'=奇  'e'=偶);
//------------------------------------------------------------------------------
void UART_Open(U8 n, U32 speed, U8 databits, U8 stopbits, U8 parity)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;
    U32 rDatabits, rParity, rStopbits;

    databits = databits;
    rStopbits = (stopbits == 2)? USART_StopBits_2 : USART_StopBits_1;
    switch (parity)
    {
    case 'o':
    case 'O':
    case 1:
        rDatabits = USART_WordLength_9b;
        rParity = USART_Parity_Odd;
        break;
    case 'e':
    case 'E':
    case 2:
        rDatabits = USART_WordLength_9b;
        rParity = USART_Parity_Even;
        break;
    default:
        rDatabits = USART_WordLength_8b;
        rParity = USART_Parity_No;
        break;
    }

    switch (n)
    {
    case 0: // 串口0 TXD->PA9, RXD->PA10, 也可管脚设为Txd->PB6,Rxd->PB7, 这里设为前一组管脚
        USART_DeInit(USART1);

        // 配置对应的GPIO
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
        GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
        GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
        GPIO_Init(GPIOA, &GPIO_InitStructure);
        GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
        GPIO_Init(GPIOA, &GPIO_InitStructure);

        // 配置 USART1 mode
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
        USART_InitStructure.USART_BaudRate = speed;
        USART_InitStructure.USART_WordLength = rDatabits;
        USART_InitStructure.USART_StopBits = rStopbits;
        USART_InitStructure.USART_Parity =  rParity;
        USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
        USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
        USART_Init(USART1, &USART_InitStructure);

        // 设为接收中断
        USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
        NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
        NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
        NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
        NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
        NVIC_Init(&NVIC_InitStructure);

        USART_Cmd(USART1, ENABLE);
        break;
    case 1: // 串口1 TXD->PA2, RXD->PA3 或 TXD->PD5, RXD->PD6
        USART_DeInit(USART2);

        // 配置对应的GPIO
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
        GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
        GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
        GPIO_Init(GPIOA, &GPIO_InitStructure);
        GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
        GPIO_Init(GPIOA, &GPIO_InitStructure);

        // 配置 USART2 mode
        RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
        USART_InitStructure.USART_BaudRate = speed;
        USART_InitStructure.USART_WordLength = rDatabits;
        USART_InitStructure.USART_StopBits = rStopbits;
        USART_InitStructure.USART_Parity =  rParity;
        USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
        USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
        USART_Init(USART2, &USART_InitStructure);

        // 设为接收中断
        USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
        NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
        NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
        NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
        NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
        NVIC_Init(&NVIC_InitStructure);

        USART_Cmd(USART2, ENABLE);
        break;
    case 2: // 串口2 TXD->PB10, RXD->PB11
        USART_DeInit(USART3);

        // 配置对应的GPIO
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
        GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
        GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
        GPIO_Init(GPIOB, &GPIO_InitStructure);
        GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
        GPIO_Init(GPIOB, &GPIO_InitStructure);

        // 配置 USART3 mode
        RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);
        USART_InitStructure.USART_BaudRate = speed;
        USART_InitStructure.USART_WordLength = rDatabits;
        USART_InitStructure.USART_StopBits = rStopbits;
        USART_InitStructure.USART_Parity =  rParity;
        USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
        USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
        USART_Init(USART3, &USART_InitStructure);

        // 设为接收中断
        USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);
        NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
        NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
        NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;
        NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
        NVIC_Init(&NVIC_InitStructure);

        USART_Cmd(USART3, ENABLE);
        break;

    default:
        break;
    }
    Dbp("open uart%d OK\r\n", n);
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

//------------------------------------------------------------------------------
// 描述: 串口1 中断服务
//------------------------------------------------------------------------------
void USART1_IRQHandler(void)
{
    if (USART_GetFlagStatus(USART1, USART_IT_RXNE) == SET)
    {
    #if (CMD_UartNum == 0)
        U16 RxByte = USART_ReceiveData(USART1);
        Fifo_in(RxByte);
    #endif
        USART_ClearITPendingBit(USART1, USART_IT_RXNE);
    }
}
//------------------------------------------------------------------------------
// 描述: 串口2 中断服务
//------------------------------------------------------------------------------
void USART2_IRQHandler(void)
{
    if (USART_GetFlagStatus(USART2, USART_IT_RXNE) == SET)
    {
    #if (CMD_UartNum == 1)
        U16 RxByte = USART_ReceiveData(USART2);
        Fifo_in(RxByte);
    #endif

        USART_ClearITPendingBit(USART2, USART_IT_RXNE);
    }
}
//------------------------------------------------------------------------------
// 描述: 串口3 中断服务
//------------------------------------------------------------------------------
void USART3_IRQHandler(void)
{
    if (USART_GetFlagStatus(USART3, USART_IT_RXNE) == SET)
    {
    #if (CMD_UartNum == 2)
        U16 RxByte = USART_ReceiveData(USART3);
        Fifo_in(RxByte);
    #endif
        USART_ClearITPendingBit(USART3, USART_IT_RXNE);
    }
}

