#ifndef __DRIVER_PID_PARAM_H
#define __DRIVER_PID_PARAM_H

#include <stdint.h>
#include <stdbool.h>
#include "bot_data_pool.h"

#define PID_PARAM_FLASH_ADDR      ((uint32_t)0x080E0000)   // Sector 11
#define PID_PARAM_FLASH_ERASE_SZ  ((uint32_t)0x00020000)   // 128KB

bool Driver_PidParam_Load(bot_params_t *out_params);
bool Driver_PidParam_Save(const bot_params_t *in_params);
void Driver_PidParam_FillDefault(bot_params_t *params);

#endif // __DRIVER_PID_PARAM_H