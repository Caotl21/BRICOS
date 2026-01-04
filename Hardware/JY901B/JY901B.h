#ifndef __JY901B_H
#define __JY901B_H

extern volatile char s_cDataUpdate;
extern volatile char s_cCmd;

#define JY901BFifoSize 100   // 队列最大容量
typedef struct 
{
    u8 JY901BRxBuf[JY901BFifoSize];
    volatile u8 In;
    volatile u8 Out;
    volatile u8 Cnt;
}struct_JY901BFifo;			//定义队列类型
extern struct_JY901BFifo JY901BFifo;
#define JY901BFifo_in(RxByte) JY901BFifo.JY901BRxBuf[JY901BFifo.Cnt++] = (RxByte);\
                            if(JY901BFifo.Cnt >= JY901BFifoSize)\
                            {\
                                JY901BFifo.Cnt = 0;\
                            }

void JY901B_Init(void);
void CopeCmdData(unsigned char ucData);
void ShowHelp(void);
void CmdProcess(void);
void AutoScanSensor(void);
void SensorUartSend(uint8_t *p_data, uint32_t uiSize);
void SensorDataUpdata(uint32_t uiReg, uint32_t uiRegNum);
void Delayms(uint16_t ucMs);
void JY901B_GetData(float fAcc[],float fGyro[],float fAngle[]);
void JY901B_process(void);

#endif

//------------------End of File----------------------------

