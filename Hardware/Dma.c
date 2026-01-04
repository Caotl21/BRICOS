#include "stm32f4xx.h"
#include "Dma.h"

void USART1_DMA_Init(void)
{
    DMA_InitTypeDef DMA_InitStructure;

    // ---------------------------------------------------------
    // 1. 配置 USART1_RX 的 DMA (DMA2 Stream2 Channel4) - IM948
    // ---------------------------------------------------------
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE); // 开启 DMA2 时钟

    DMA_DeInit(DMA2_Stream2);
    
    DMA_InitStructure.DMA_Channel = DMA_Channel_4;              // 通道 4
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&USART1->DR; // 外设地址：串口数据寄存器
    DMA_InitStructure.DMA_Memory0BaseAddr = (uint32_t)Uart1Fifo.RxBuf;     // 内存地址：我们的缓冲区
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralToMemory;     // 方向：外设 -> 内存
    DMA_InitStructure.DMA_BufferSize = FifoSize;            // 缓冲区大小
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable; // 外设地址不增
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;     // 内存地址自增
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte; // 字节传输
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;             // 【关键】循环模式
    DMA_InitStructure.DMA_Priority = DMA_Priority_VeryHigh;     // 优先级很高
    DMA_InitStructure.DMA_FIFOMode = DMA_FIFOMode_Disable;      // 不用FIFO
    DMA_InitStructure.DMA_FIFOThreshold = DMA_FIFOThreshold_Full;
    DMA_InitStructure.DMA_MemoryBurst = DMA_MemoryBurst_Single;
    DMA_InitStructure.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;

    DMA_Init(DMA2_Stream2, &DMA_InitStructure);
    DMA_Cmd(DMA2_Stream2, ENABLE); // 开启 DMA 流
}

