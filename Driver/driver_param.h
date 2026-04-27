#ifndef __DRIVER_PID_PARAM_H
#define __DRIVER_PID_PARAM_H

#include <stdint.h>
#include <stdbool.h>
#include "sys_data_pool.h"
#include "sys_flash_layout.h"

#define PID_PARAM_FLASH_ADDR      SYS_FLASH_PID_PARAM_ADDR
#define PID_PARAM_FLASH_ERASE_SZ  SYS_FLASH_PID_PARAM_ERASE_SIZE

bool Driver_PidParam_Load(bot_params_t *out_params);
bool Driver_PidParam_Save(const bot_params_t *in_params);
void Driver_PidParam_FillDefault(bot_params_t *params);

#endif // __DRIVER_PID_PARAM_H

