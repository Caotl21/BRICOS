#ifndef __SYS_MODE_MANAGER_H
#define __SYS_MODE_MANAGER_H

#include <stdint.h>

#include "sys_data_pool.h"

typedef enum {
    SYS_MODE_MGR_OK = 0,
    SYS_MODE_MGR_INVALID_PARAM,
    SYS_MODE_MGR_INVALID_TRANSITION,
    SYS_MODE_MGR_SAFETY_BLOCKED,
    SYS_MODE_MGR_FAULT_LATCHED
} sys_mode_mgr_status_t;

typedef enum {
    SYS_FAULT_NONE         = 0u,
    SYS_FAULT_LEAK         = (1u << 0),
    SYS_FAULT_IMU          = (1u << 1),
    SYS_FAULT_LOW_VOLTAGE  = (1u << 2),
    SYS_FAULT_TASK_TIMEOUT = (1u << 3),
    SYS_FAULT_LOS          = (1u << 4)
} sys_fault_flag_e;

void System_ModeManager_Init(bot_sys_mode_e boot_mode);

sys_mode_mgr_status_t System_ModeManager_RequestMode(bot_sys_mode_e requested_mode, const bot_sys_state_t *sys_state,
                                                     const bot_params_t *params, bot_sys_mode_e *out_mode);

sys_mode_mgr_status_t System_ModeManager_EnterFailsafe(uint32_t fault_flags, bot_sys_mode_e *out_mode);

uint32_t System_ModeManager_GetFaultFlags(void);
bot_sys_mode_e System_ModeManager_GetMode(void);

#endif /* __SYS_MODE_MANAGER_H */

