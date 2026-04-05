#include "sys_mode_manager.h"

#include "sys_data_pool.h"
#include "sys_port.h"

static bot_sys_mode_e s_sys_mode = SYS_MODE_STANDBY;
static bot_run_mode_e s_motion_mode = MOTION_STATE_MANUAL;
static uint32_t s_fault_flags = SYS_FAULT_NONE;

static uint8_t prv_is_sys_mode_valid(bot_sys_mode_e mode)
{
    return (mode >= SYS_MODE_STANDBY) && (mode <= SYS_MODE_FAILSAFE);
}

static uint8_t prv_is_motion_mode_valid(bot_run_mode_e mode)
{
    return (mode >= MOTION_STATE_MANUAL) && (mode <= MOTION_STATE_AUTO);
}

static uint32_t prv_collect_dynamic_faults(void)
{
    bot_sys_state_t sys_state;
    bot_params_t params;
    uint32_t faults = SYS_FAULT_NONE;

    Bot_Sys_State_Pull(&sys_state);
    Bot_Params_Pull(&params);

    if (sys_state.is_leak_detected) {
        faults |= SYS_FAULT_LEAK;
    }

    if (sys_state.is_imu_error) {
        faults |= SYS_FAULT_IMU;
    }

    if ((sys_state.bat_voltage_v > 1.0f) && (sys_state.bat_voltage_v < params.failsafe_low_voltage)) {
        faults |= SYS_FAULT_LOW_VOLTAGE;
    }

    return faults;
}

void System_ModeManager_Init(bot_sys_mode_e boot_mode)
{
    if (!prv_is_sys_mode_valid(boot_mode)) {
        boot_mode = SYS_MODE_STANDBY;
    }

    SYS_ENTER_CRITICAL();
    s_sys_mode = boot_mode;
    s_motion_mode = MOTION_STATE_MANUAL;
    s_fault_flags = SYS_FAULT_NONE;
    SYS_EXIT_CRITICAL();
}

sys_mode_mgr_status_t System_ModeManager_RequestSysMode(bot_sys_mode_e requested_mode)
{
    uint32_t dynamic_faults = prv_collect_dynamic_faults();
    bot_sys_mode_e current_mode;

    if (!prv_is_sys_mode_valid(requested_mode)) {
        return SYS_MODE_MGR_INVALID_PARAM;
    }

    SYS_ENTER_CRITICAL();
    current_mode = s_sys_mode;

    if (current_mode == SYS_MODE_FAILSAFE) {
        s_fault_flags = dynamic_faults;

        if (requested_mode != SYS_MODE_ACTIVE_DISARMED) {
            SYS_EXIT_CRITICAL();
            return SYS_MODE_MGR_FAULT_LATCHED;
        }

        if (dynamic_faults != SYS_FAULT_NONE) {
            SYS_EXIT_CRITICAL();
            return SYS_MODE_MGR_FAULT_LATCHED;
        }

        s_fault_flags = SYS_FAULT_NONE;
        s_sys_mode = SYS_MODE_ACTIVE_DISARMED;
        SYS_EXIT_CRITICAL();
        return SYS_MODE_MGR_OK;
    }

    if (requested_mode == current_mode) {
        SYS_EXIT_CRITICAL();
        return SYS_MODE_MGR_OK;
    }

    if (requested_mode == SYS_MODE_FAILSAFE) {
        SYS_EXIT_CRITICAL();
        return SYS_MODE_MGR_INVALID_TRANSITION;
    }

    if (requested_mode == SYS_MODE_STANDBY) {
        if ((current_mode != SYS_MODE_ACTIVE_DISARMED) && (current_mode != SYS_MODE_MOTION_ARMED)) {
            SYS_EXIT_CRITICAL();
            return SYS_MODE_MGR_INVALID_TRANSITION;
        }
    } else if (requested_mode == SYS_MODE_ACTIVE_DISARMED) {
        if ((current_mode != SYS_MODE_STANDBY) && (current_mode != SYS_MODE_MOTION_ARMED)) {
            SYS_EXIT_CRITICAL();
            return SYS_MODE_MGR_INVALID_TRANSITION;
        }
    } else if (requested_mode == SYS_MODE_MOTION_ARMED) {
        if (current_mode != SYS_MODE_ACTIVE_DISARMED) {
            SYS_EXIT_CRITICAL();
            return SYS_MODE_MGR_INVALID_TRANSITION;
        }

        if (dynamic_faults != SYS_FAULT_NONE) {
            SYS_EXIT_CRITICAL();
            return SYS_MODE_MGR_SAFETY_BLOCKED;
        }
    }

    s_sys_mode = requested_mode;
    SYS_EXIT_CRITICAL();
    return SYS_MODE_MGR_OK;
}

sys_mode_mgr_status_t System_ModeManager_RequestMotionMode(bot_run_mode_e requested_mode)
{
    uint32_t dynamic_faults;

    if (!prv_is_motion_mode_valid(requested_mode)) {
        return SYS_MODE_MGR_INVALID_PARAM;
    }

    SYS_ENTER_CRITICAL();
    if (s_sys_mode == SYS_MODE_FAILSAFE) {
        SYS_EXIT_CRITICAL();
        return SYS_MODE_MGR_FAULT_LATCHED;
    }
    SYS_EXIT_CRITICAL();

    dynamic_faults = prv_collect_dynamic_faults();
    if (((requested_mode == MOTION_STATE_STABILIZE) || (requested_mode == MOTION_STATE_AUTO)) &&
        ((dynamic_faults & SYS_FAULT_IMU) != 0u)) {
        return SYS_MODE_MGR_SAFETY_BLOCKED;
    }

    SYS_ENTER_CRITICAL();
    s_motion_mode = requested_mode;
    SYS_EXIT_CRITICAL();
    return SYS_MODE_MGR_OK;
}

sys_mode_mgr_status_t System_ModeManager_EnterFailsafe(uint32_t fault_flags)
{
    if (fault_flags == SYS_FAULT_NONE) {
        return SYS_MODE_MGR_INVALID_PARAM;
    }

    SYS_ENTER_CRITICAL();
    s_fault_flags |= fault_flags;
    s_sys_mode = SYS_MODE_FAILSAFE;
    SYS_EXIT_CRITICAL();

    return SYS_MODE_MGR_OK;
}

void System_ModeManager_Pull(bot_sys_mode_e *out_sys_mode, bot_run_mode_e *out_motion_mode, uint32_t *out_fault_flags)
{
    SYS_ENTER_CRITICAL();
    if (out_sys_mode != 0) {
        *out_sys_mode = s_sys_mode;
    }
    if (out_motion_mode != 0) {
        *out_motion_mode = s_motion_mode;
    }
    if (out_fault_flags != 0) {
        *out_fault_flags = s_fault_flags;
    }
    SYS_EXIT_CRITICAL();
}

uint32_t System_ModeManager_GetFaultFlags(void)
{
    uint32_t flags;

    SYS_ENTER_CRITICAL();
    flags = s_fault_flags;
    SYS_EXIT_CRITICAL();

    return flags;
}

bot_sys_mode_e System_ModeManager_GetSysMode(void)
{
    bot_sys_mode_e mode;

    SYS_ENTER_CRITICAL();
    mode = s_sys_mode;
    SYS_EXIT_CRITICAL();

    return mode;
}

bot_run_mode_e System_ModeManager_GetMotionMode(void)
{
    bot_run_mode_e mode;

    SYS_ENTER_CRITICAL();
    mode = s_motion_mode;
    SYS_EXIT_CRITICAL();

    return mode;
}
