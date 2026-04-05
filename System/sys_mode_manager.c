#include "sys_mode_manager.h"

static bot_sys_mode_e s_current_mode = SYS_MODE_STANDBY;
static uint32_t s_fault_flags = SYS_FAULT_NONE;

// 根据当前系统状态和参数，评估是否存在需要触发 Failsafe 的动态故障
static uint32_t prv_collect_dynamic_faults(const bot_sys_state_t *sys_state, const bot_params_t *params)
{
    uint32_t faults = SYS_FAULT_NONE;

    if ((sys_state == 0) || (params == 0)) {
        return faults;
    }

    if (sys_state->is_leak_detected) {
        faults |= SYS_FAULT_LEAK;
    }

    if (sys_state->is_imu_error) {
        faults |= SYS_FAULT_IMU;
    }

    if ((sys_state->bat_voltage_v > 1.0f) && (sys_state->bat_voltage_v < params->failsafe_low_voltage)) {
        faults |= SYS_FAULT_LOW_VOLTAGE;
    }

    return faults;
}

static uint8_t prv_is_mode_valid(bot_sys_mode_e mode)
{
    return (mode >= SYS_MODE_STANDBY) && (mode <= SYS_MODE_FAILSAFE);
}

void System_ModeManager_Init(bot_sys_mode_e boot_mode)
{
    if (!prv_is_mode_valid(boot_mode)) {
        boot_mode = SYS_MODE_STANDBY;
    }

    s_current_mode = boot_mode;
    s_fault_flags = SYS_FAULT_NONE;
}

// API: 尝试切换系统模式 (加锁/解锁)
sys_mode_mgr_status_t System_ModeManager_RequestMode(bot_sys_mode_e requested_mode, const bot_sys_state_t *sys_state,
                                                     const bot_params_t *params, bot_sys_mode_e *out_mode)
{
    uint32_t dynamic_faults;

    if ((out_mode == 0) || (sys_state == 0) || (params == 0)) {
        return SYS_MODE_MGR_INVALID_PARAM;
    }

    if (!prv_is_mode_valid(requested_mode)) {
        return SYS_MODE_MGR_INVALID_PARAM;
    }

    dynamic_faults = prv_collect_dynamic_faults(sys_state, params);

    if (s_current_mode == SYS_MODE_FAILSAFE) {
        s_fault_flags = dynamic_faults;

        if (requested_mode != SYS_MODE_ACTIVE_DISARMED) {
            *out_mode = s_current_mode;
            return SYS_MODE_MGR_FAULT_LATCHED;
        }

        if (dynamic_faults != SYS_FAULT_NONE) {
            *out_mode = s_current_mode;
            return SYS_MODE_MGR_FAULT_LATCHED;
        }

        s_fault_flags = SYS_FAULT_NONE;
        s_current_mode = SYS_MODE_ACTIVE_DISARMED;
        *out_mode = s_current_mode;
        return SYS_MODE_MGR_OK;
    }

    if (requested_mode == SYS_MODE_FAILSAFE) {
        *out_mode = s_current_mode;
        return SYS_MODE_MGR_INVALID_TRANSITION;
    }

    if (requested_mode == s_current_mode) {
        *out_mode = s_current_mode;
        return SYS_MODE_MGR_OK;
    }

    if (requested_mode == SYS_MODE_MOTION_ARMED) {
        if (s_current_mode != SYS_MODE_ACTIVE_DISARMED) {
            *out_mode = s_current_mode;
            return SYS_MODE_MGR_INVALID_TRANSITION;
        }

        if (dynamic_faults != SYS_FAULT_NONE) {
            *out_mode = s_current_mode;
            return SYS_MODE_MGR_SAFETY_BLOCKED;
        }
    } else if (requested_mode == SYS_MODE_ACTIVE_DISARMED) {
        if (s_current_mode != SYS_MODE_STANDBY && s_current_mode != SYS_MODE_MOTION_ARMED) {
            *out_mode = s_current_mode;
            return SYS_MODE_MGR_INVALID_TRANSITION;
        }
    } else if (requested_mode == SYS_MODE_STANDBY) {
        if (s_current_mode != SYS_MODE_ACTIVE_DISARMED && s_current_mode != SYS_MODE_MOTION_ARMED) {
            *out_mode = s_current_mode;
            return SYS_MODE_MGR_INVALID_TRANSITION;
        }
    }

    s_current_mode = requested_mode;
    *out_mode = s_current_mode;
    return SYS_MODE_MGR_OK;
}

// API: 进入 Failsafe 模式，参数为触发 Failsafe 的故障标志位
sys_mode_mgr_status_t System_ModeManager_EnterFailsafe(uint32_t fault_flags, bot_sys_mode_e *out_mode)
{
    if (out_mode == 0) {
        return SYS_MODE_MGR_INVALID_PARAM;
    }

    if (fault_flags == SYS_FAULT_NONE) {
        *out_mode = s_current_mode;
        return SYS_MODE_MGR_INVALID_PARAM;
    }

    s_fault_flags |= fault_flags;
    s_current_mode = SYS_MODE_FAILSAFE;
    *out_mode = s_current_mode;

    return SYS_MODE_MGR_OK;
}

uint32_t System_ModeManager_GetFaultFlags(void)
{
    return s_fault_flags;
}

bot_sys_mode_e System_ModeManager_GetMode(void)
{
    return s_current_mode;
}
