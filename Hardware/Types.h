#ifndef __TPYES_H
#define __TPYES_H

#include "stm32f4xx.h"
#include "stm32f4xx_conf.h"

#define DELAY_TIME 1000

#define FifoSize 2048 
#define JY901BFifoSize 2000   // 队列最大容量
#define ControlSignal_RxBuf_Size 64

typedef signed char            S8;
typedef unsigned char          U8;
typedef signed short           S16;
typedef unsigned short         U16;
typedef signed long            S32;
typedef unsigned long          U32;
typedef float                  F32;

typedef struct // ���� Fifo������
{
    U8 RxBuf[FifoSize];
    volatile U16 In;
    volatile U16 Out;
    volatile U16 Cnt;
}struct_UartFifo;


typedef struct 
{
    u8 JY901BRxBuf[JY901BFifoSize];
    volatile u16 In;
    volatile u16 Out;
    volatile u16 Cnt;
}struct_JY901BFifo;			//定义队列类型

#endif

