#include "sys_mode_manager.h"

#include "sys_data_pool.h"
#include "sys_port.h"

/* 模式管理器独占管理的运行态：系统模式、运动模式、故障锁存位 */
static bot_sys_mode_e s_sys_mode = SYS_MODE_STANDBY;
static bot_run_mode_e s_motion_mode = MOTION_STATE_MANUAL;
static uint32_t s_fault_flags = SYS_FAULT_NONE;

/* 校验系统模式枚举是否合法 */
static uint8_t prv_is_sys_mode_valid(bot_sys_mode_e mode)
{
    return (mode >= SYS_MODE_STANDBY) && (mode <= SYS_MODE_FAILSAFE);
}

/* 校验运动模式枚举是否合法 */
static uint8_t prv_is_motion_mode_valid(bot_run_mode_e mode)
{
    return (mode >= MOTION_STATE_MANUAL) && (mode <= MOTION_STATE_AUTO);
}

/* 从数据池快照提取“动态故障”（实时故障，不等同于锁存故障） */
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

/*
 * 初始化模式管理器。
 * 当前版本固定上电进入 STANDBY，运动模式 MANUAL，清空故障锁存。
 */
void System_ModeManager_Init()
{
    SYS_ENTER_CRITICAL();
    s_sys_mode = SYS_MODE_STANDBY;
    s_motion_mode = MOTION_STATE_MANUAL;
    s_fault_flags = SYS_FAULT_NONE;
    SYS_EXIT_CRITICAL();
}

/*
 * 系统模式切换请求（系统状态机）。
 * 关键规则：
 * 1. 外部不能直接请求 FAILSAFE；
 * 2. FAILSAFE 仅允许恢复到 ACTIVE_DISARMED；
 * 3. 进入 MOTION_ARMED 前必须在 ACTIVE_DISARMED 且动态故障清零。
 */
sys_mode_mgr_status_t System_ModeManager_RequestSysMode(bot_sys_mode_e requested_mode)
{
    uint32_t dynamic_faults = prv_collect_dynamic_faults();
    bot_sys_mode_e current_mode;

    if (!prv_is_sys_mode_valid(requested_mode)) {
        return SYS_MODE_MGR_INVALID_PARAM;
    }

    SYS_ENTER_CRITICAL();
    current_mode = s_sys_mode;

    /* FAILSAFE 状态下：只允许受控恢复路径 */
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

    /*
     * 常态迁移矩阵：
     * STANDBY <-> ACTIVE_DISARMED <-> MOTION_ARMED
     * 并允许 MOTION_ARMED -> STANDBY 快速回待机。
     */
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

        /* 解锁前最终安全门限 */
        if (dynamic_faults != SYS_FAULT_NONE) {
            SYS_EXIT_CRITICAL();
            return SYS_MODE_MGR_SAFETY_BLOCKED;
        }
    }

    s_sys_mode = requested_mode;
    SYS_EXIT_CRITICAL();
    return SYS_MODE_MGR_OK;
}

/*
 * 运动模式切换请求。
 * 仅切换控制通道（MANUAL/STABILIZE/AUTO），不改变系统模式。
 */
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

    /*
     * STABILIZE/AUTO 依赖 IMU 健康。
     * 若 IMU 异常，仅允许 MANUAL。
     */
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

/* 进入 FAILSAFE，并将故障按位 OR 累加到锁存位 */
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

/* 原子快照读取：一次性返回系统模式、运动模式与故障锁存位 */
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

/* 轻量 Getter：获取故障锁存位 */
uint32_t System_ModeManager_GetFaultFlags(void)
{
    uint32_t flags;

    SYS_ENTER_CRITICAL();
    flags = s_fault_flags;
    SYS_EXIT_CRITICAL();

    return flags;
}

/* 轻量 Getter：获取系统模式 */
bot_sys_mode_e System_ModeManager_GetSysMode(void)
{
    bot_sys_mode_e mode;

    SYS_ENTER_CRITICAL();
    mode = s_sys_mode;
    SYS_EXIT_CRITICAL();

    return mode;
}

/* 轻量 Getter：获取运动模式 */
bot_run_mode_e System_ModeManager_GetMotionMode(void)
{
    bot_run_mode_e mode;

    SYS_ENTER_CRITICAL();
    mode = s_motion_mode;
    SYS_EXIT_CRITICAL();

    return mode;
}
