#ifndef __JY901B_H
#define __JY901B_H

#include "Types.h"

extern volatile char s_cDataUpdate;
extern volatile char s_cCmd;


extern struct_JY901BFifo JY901BFifo;
//#define JY901BFifo_in(RxByte) JY901BFifo.JY901BRxBuf[JY901BFifo.Cnt++] = (RxByte);\
//                            if(JY901BFifo.Cnt >= JY901BFifoSize)\
//                            {\
//                                JY901BFifo.Cnt = 0;\
//                            }
#define JY901BFifo_in(RxByte) if(JY901BFifoSize > JY901BFifo.Cnt)\
								{\
									JY901BFifo.JY901BRxBuf[JY901BFifo.In] = RxByte;\
									if(++JY901BFifo.In>=JY901BFifoSize) JY901BFifo.In=0;\
									JY901BFifo.Cnt++;\
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

