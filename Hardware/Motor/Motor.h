#ifndef __MOTOR_H
#define __MOTOR_H

extern uint16_t motor_pulse[6];
extern uint16_t led_pulse[2];
extern uint16_t servo_pulse[2];
extern uint8_t motor_num;
extern uint8_t led_num;
extern uint8_t servo_num;

void Motor_SetSpeed(uint16_t pulse_width[], uint8_t num);
void LED_SetSpeed(uint16_t pulse_width[], uint8_t num);
void Servo_SetSpeed(uint16_t pulse_width[], uint8_t num);
void ESC_Init(void);

#endif
