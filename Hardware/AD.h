#ifndef __AD_H
#define __AD_H

extern uint16_t AD_Value[4];

void AD_Init(void);
float current_Getdata(void);
float voltage_Getdata(void);

#endif
