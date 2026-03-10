#ifndef __JY901B_H
#define __JY901B_H

#include "Types.h"

extern volatile char s_cDataUpdate;
extern volatile char s_cCmd;


extern struct_JY901BFifo JY901BFifo;


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

