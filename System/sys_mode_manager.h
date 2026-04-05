#ifndef __SYS_MODE_MANAGER_H
#define __SYS_MODE_MANAGER_H

#include <stdint.h>

/* ============================================================================
 * System Mode / Motion Mode 定义
 * 说明：
 * 1. 系统模式（bot_sys_mode_e）用于定义整机安全级别与执行权限；
 * 2. 运动模式（bot_run_mode_e）用于定义控制算法通道（手动/自稳/自动）；
 * 3. 两者均由 Mode Manager 独占管理，外部模块仅通过 API 请求切换。
 * ============================================================================ */

typedef enum {
    SYS_MODE_STANDBY         = 0, /* 待机模式：安全输出、低活动度 */
    SYS_MODE_ACTIVE_DISARMED = 1, /* 激活但未解锁：允许感知与通讯，不允许有效推力 */
    SYS_MODE_MOTION_ARMED    = 2, /* 运动解锁：允许正常推力输出 */
    SYS_MODE_FAILSAFE        = 3  /* 故障保护：强制安全输出，锁定风险动作 */
} bot_sys_mode_e;

typedef enum {
    MOTION_STATE_MANUAL    = 0, /* 手动控制 */
    MOTION_STATE_STABILIZE = 1, /* 自稳控制（定深/定向） */
    MOTION_STATE_AUTO      = 2  /* 自动控制（上位机策略） */
} bot_run_mode_e;

/* ============================================================================
 * Mode Manager 返回状态码
 * 说明：所有 Request/Enter 接口都通过该状态码向调用方反馈结果。
 * ============================================================================ */
typedef enum {
    SYS_MODE_MGR_OK = 0,           /* 操作成功 */
    SYS_MODE_MGR_INVALID_PARAM,    /* 参数非法 */
    SYS_MODE_MGR_INVALID_TRANSITION,/* 状态迁移不合法 */
    SYS_MODE_MGR_SAFETY_BLOCKED,   /* 被安全条件阻止 */
    SYS_MODE_MGR_FAULT_LATCHED     /* 故障锁存中，不允许请求通过 */
} sys_mode_mgr_status_t;

/* ============================================================================
 * 故障位定义（可组合 bitmask）
 * 说明：Monitor 或其他安全模块可按位上报故障，Mode Manager 统一锁存处理。
 * ============================================================================ */
typedef enum {
    SYS_FAULT_NONE         = 0u,
    SYS_FAULT_LEAK         = (1u << 0), /* 漏水故障 */
    SYS_FAULT_IMU          = (1u << 1), /* IMU 异常 */
    SYS_FAULT_LOW_VOLTAGE  = (1u << 2), /* 低电压 */
    SYS_FAULT_TASK_TIMEOUT = (1u << 3), /* 任务超时 */
    SYS_FAULT_LOS          = (1u << 4)  /* 链路丢失（Line-Of-Sight / Link-Of-Signal） */
} sys_fault_flag_e;

/* ============================================================================
 * 生命周期接口
 * ============================================================================ */

/**
 * @brief  初始化 Mode Manager。
 * @param  boot_mode 上电初始系统模式（非法值会被降级为 SYS_MODE_STANDBY）。
 * @note   建议在 RTOS 调度启动前调用一次。
 */
void System_ModeManager_Init(bot_sys_mode_e boot_mode);

/* ============================================================================
 * 模式切换接口（请求型）
 * ============================================================================ */

/**
 * @brief  请求切换系统模式（STANDBY / DISARMED / ARMED）。
 * @param  requested_mode 目标系统模式。
 * @return sys_mode_mgr_status_t 切换结果。
 * @note
 * - 不支持外部直接请求到 FAILSAFE（FAILSAFE 由故障触发接口进入）；
 * - FAILSAFE 退出需满足管理器内部故障清除条件。
 */
sys_mode_mgr_status_t System_ModeManager_RequestSysMode(bot_sys_mode_e requested_mode);

/**
 * @brief  请求切换运动模式（MANUAL / STABILIZE / AUTO）。
 * @param  requested_mode 目标运动模式。
 * @return sys_mode_mgr_status_t 切换结果。
 * @note   该接口仅管理“运动控制通道”，不改变系统模式。
 */
sys_mode_mgr_status_t System_ModeManager_RequestMotionMode(bot_run_mode_e requested_mode);

/**
 * @brief  触发进入 FAILSAFE（故障上报入口）。
 * @param  fault_flags 故障位掩码（可多位组合，必须非 0）。
 * @return sys_mode_mgr_status_t 触发结果。
 */
sys_mode_mgr_status_t System_ModeManager_EnterFailsafe(uint32_t fault_flags);

/* ============================================================================
 * 快照读取接口
 * ============================================================================ */

/**
 * @brief  原子拉取当前模式与故障快照。
 * @param  out_sys_mode    输出：当前系统模式，可为 NULL。
 * @param  out_motion_mode 输出：当前运动模式，可为 NULL。
 * @param  out_fault_flags 输出：当前故障锁存位，可为 NULL。
 */
void System_ModeManager_Pull(bot_sys_mode_e *out_sys_mode,
                             bot_run_mode_e *out_motion_mode,
                             uint32_t *out_fault_flags);

/* ============================================================================
 * 便捷 Getter
 * ============================================================================ */
uint32_t System_ModeManager_GetFaultFlags(void);
bot_sys_mode_e System_ModeManager_GetSysMode(void);
bot_run_mode_e System_ModeManager_GetMotionMode(void);

#endif /* __SYS_MODE_MANAGER_H */

