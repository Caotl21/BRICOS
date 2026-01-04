#ifndef __PWM_H
#define __PWM_H

void PWM_Init(void);
void Motor_SetPWM(uint16_t Compare[]);
void LED_SetPWM(uint16_t Compare[]);
void Servo_SetPWM(uint16_t Compare[]);

#endif
