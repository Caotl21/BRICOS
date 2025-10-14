/******************************************************************************
                             IM948模块使用示例
基于stm32f103c8
    1、调试口硬件接线: 串口1 Rx(PA10)、Tx(PA9) GND 分别接到USB转ttl串口工具的Tx、Rx、GND， 921600波特率，8数据位、1停止位、无校验
       传感器硬件接线: 串口2 Rx(PA3)、 Tx(PA2) GND 分别接到传感器的Tx、Rx、GND， 115200波特率，8数据位、1停止位、无校验
    2、a.用户把 im948_CMD.c 和 im948_CMD.h 添加到自己的项目里
       b.实现 Cmd_Write(U8 *pBuf, int Len) 函数给串口发送数据
       c.并把串口接收到的每字节数据填入 Cmd_GetPkt(U8 byte)  本示例使用了fifo做缓存，然后在主循环处理
       d.收到模块发来的数据包会回调 Cmd_RxUnpack(U8 *buf, U8 DLen)，用户可在该函数里处理需要的数据功能
         例如定义全局变量欧拉角AngleX、AngleY、AngleZ，在Cmd_RxUnpack里赋值，然后在主函数里使用全局变量做业务开发即可

    以上即可实现所需的功能，若需进一步研究，可继续看以下e和f
       e.参考手册通信协议和本示例的 im948_test()，进行调试每个功能即可
       f.若用户需要扩展485总线方式并联多个模块使用时，可通过设置模块地址 targetDeviceAddress 来指定操作对象
*******************************************************************************/
#include "stm32f10x.h"
#include "im948_CMD.h"
#include "drv_Uart.h"
#include "Serial.h"
#include "OLED.h"

#pragma import(__use_no_semihosting)
/* 定义 _sys_exit() 以避免使用半主机模式 */
void _sys_exit(int x)
{
    x = x;
}
/* 标准库需要的支持类型 */
struct __FILE
{
    int handle;
};
FILE __stdout;

int main(void)
{	
	Serial_Init();
	OLED_Init();
	
	IM948_Init();

	
    while (1)
    {
        // 处理模块发过来的数据----------------------------------
        
        //printf("UartFifo.Cnt: %d\r\n", UartFifo.Cnt);  // 修改这行，添加格式化字符串
        
		IM948_process();
        //在这里，用户可增加自己的数据业务处理逻辑
        if (isNewData)
        {// 已更新数据
            isNewData = 0;
            printf("AngleX:%.3f, AngleY:%.3f, AngleZ:%.3f\r\n", AngleX, AngleY, AngleZ);
            printf("AccX:%.3f, AccY:%.3f, AccZ:%.3f, Accabs:%.3f\r\n", AccX, AccY, AccZ, Accabs);
            printf("GyroX:%.3f, GyroY:%.3f, GyroZ:%.3f, Gyroabs:%.3f\r\n", GyroX, GyroY, GyroZ, Gyroabs);
            printf("MagX:%.3f, MagY:%.3f, MagZ:%.3f, Magabs:%.3f\r\n", MagX, MagY, MagZ, Magabs);
//          display(AngleX); // 显示全局变量的欧拉角X角度值
//          display(AngleY); // 显示全局变量的欧拉角Y角度值
//          display(AngleZ); // 显示全局变量的欧拉角Z角度值
//          ........
        }
    }
}

