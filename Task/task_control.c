#include "stm32f4xx.h"

#include "FreeRTOS.h"
#include "task.h"

#include "sys_data_pool.h"
#include "sys_log.h"
#include "sys_mode_manager.h"
#include "sys_monitor.h"
#include "sys_pid_algo.h"

#include "driver_hydrocore.h"
#include "driver_imu.h"
#include "driver_thruster.h"

#include "task_comm.h"
#include "task_control.h"
#include "task_sensor.h"

#include <string.h>

#define TASK_CONTROL_PERIOD_S            (0.01f)
#define TASK_CONTROL_PERIOD_MS           (10u)
#define TASK_CONTROL_STANDBY_PERIOD_MS   (500u)    /* STANDBY 2Hz */

#define STANDBY_ESC_KICK_INTERVAL_MS     (120000u) /* 2min */
#define STANDBY_ESC_KICK_DURATION_MS     (1000u)
#define STANDBY_ESC_KICK_SPEED           (6.0f)
#define STANDBY_ESC_KICK_INTERVAL_CYCLES ((STANDBY_ESC_KICK_INTERVAL_MS + TASK_CONTROL_STANDBY_PERIOD_MS - 1u) / TASK_CONTROL_STANDBY_PERIOD_MS)
#define STANDBY_ESC_KICK_DURATION_CYCLES ((STANDBY_ESC_KICK_DURATION_MS + TASK_CONTROL_STANDBY_PERIOD_MS - 1u) / TASK_CONTROL_STANDBY_PERIOD_MS)

/* ARMED 进入动作：每个推进器依次轻转 0.2s */
#define ARMED_ENTRY_SPIN_DURATION_MS     (200u)
#define ARMED_ENTRY_SPIN_SPEED           (8.0f)

/* 控制器实例 */
Cascade_PID_t pid_roll, pid_pitch, pid_yaw;
PID_Controller_t pid_depth;
TaskHandle_t Control_Task_Handler = NULL;

typedef struct control_fsm_ctx_s control_fsm_ctx_t;
typedef void (*control_state_fn_t)(control_fsm_ctx_t *ctx);

typedef struct {
    const char *name;
    control_state_fn_t on_enter;
    control_state_fn_t on_run;
    control_state_fn_t on_exit;
} control_state_ops_t;

/* task_control 运行时上下文 */
struct control_fsm_ctx_s {
    bot_params_t *params;

    bot_body_state_t state;
    bot_target_t target;

    bot_sys_mode_e current_mode;
    bot_run_mode_e current_motion_mode;
    bot_run_mode_e last_armed_motion_mode;
    uint8_t initialized;

    /* STANDBY 子状态数据 */
    uint8_t standby_tasks_paused;
    uint8_t standby_kick_active;
    uint16_t standby_cycle_counter;
    uint16_t standby_kick_cycles_left;

    Bot_Wrench_t wrench_out;
    float thruster_pwm[THRUSTER_COUNT];

    float euler_roll;
    float euler_pitch;
    float euler_yaw;
};

static float Clamp_Symmetric(float value, float limit)
{
    if (limit <= 0.0f) {
        return value;
    }
    if (value > limit) {
        return limit;
    }
    if (value < -limit) {
        return -limit;
    }
    return value;
}

static float Normalize_Angle_Error(float error_deg)
{
    while (error_deg > 180.0f) {
        error_deg -= 360.0f;
    }
    while (error_deg < -180.0f) {
        error_deg += 360.0f;
    }
    return error_deg;
}

static void Reset_PID(PID_Controller_t *pid)
{
    if (pid == NULL) {
        return;
    }
    pid->error_int = 0.0f;
    pid->error_last = 0.0f;
}

static void Reset_Cascade_PID(Cascade_PID_t *pid)
{
    if (pid == NULL) {
        return;
    }

    Reset_PID(&pid->outer);
    Reset_PID(&pid->inner);
}

static void Reset_All_Controllers(void)
{
    Reset_Cascade_PID(&pid_roll);
    Reset_Cascade_PID(&pid_pitch);
    Reset_Cascade_PID(&pid_yaw);
    Reset_PID(&pid_depth);
}

static void Sync_PID_Config(PID_Controller_t *runtime, const PID_Controller_t *config)
{
    if ((runtime == NULL) || (config == NULL)) {
        return;
    }

    runtime->kp = config->kp;
    runtime->ki = config->ki;
    runtime->kd = config->kd;
    runtime->integral_max = config->integral_max;
    runtime->output_max = config->output_max;
}

static void Sync_Cascade_Config(Cascade_PID_t *runtime, const Cascade_PID_t *config)
{
    if ((runtime == NULL) || (config == NULL)) {
        return;
    }

    Sync_PID_Config(&runtime->outer, &config->outer);
    Sync_PID_Config(&runtime->inner, &config->inner);
}

static float PID_Update(PID_Controller_t *pid, float target, float measurement, float dt_s, uint8_t wrap_angle)
{
    float error;
    float derivative;
    float output;

    if ((pid == NULL) || (dt_s <= 0.0f)) {
        return 0.0f;
    }

    error = target - measurement;
    if (wrap_angle) {
        error = Normalize_Angle_Error(error);
    }

    pid->error_int += error * dt_s;
    pid->error_int = Clamp_Symmetric(pid->error_int, pid->integral_max);

    derivative = (error - pid->error_last) / dt_s;
    output = (pid->kp * error) + (pid->ki * pid->error_int) + (pid->kd * derivative);
    output = Clamp_Symmetric(output, pid->output_max);

    pid->error_last = error;
    return output;
}

static float Cascade_PID_Update(Cascade_PID_t *pid,
                                float angle_target,
                                float angle_measurement,
                                float rate_measurement,
                                float dt_s,
                                uint8_t wrap_angle)
{
    float target_rate;

    if (pid == NULL) {
        return 0.0f;
    }

    target_rate = PID_Update(&pid->outer, angle_target, angle_measurement, dt_s, wrap_angle);
    return PID_Update(&pid->inner, target_rate, rate_measurement, dt_s, 0u);
}

static void Normalize_Thruster_Outputs(float *thruster_pwm, uint8_t count, float max_output)
{
    float max_pwm = 0.0f;
    uint8_t i;

    for (i = 0; i < count; i++) {
        float abs_pwm = (thruster_pwm[i] >= 0.0f) ? thruster_pwm[i] : -thruster_pwm[i];
        if (abs_pwm > max_pwm) {
            max_pwm = abs_pwm;
        }
    }

    if (max_pwm > max_output) {
        float scale = max_output / max_pwm;
        for (i = 0; i < count; i++) {
            thruster_pwm[i] *= scale;
        }
    }
}

static float Thruster_Map(float total_thrust)
{
    float pwm;

    if (total_thrust < 0.0f) {
        pwm = total_thrust * 25.0f;
    } else {
        pwm = total_thrust * 8.0f;
    }

    if (pwm > 100.0f) {
        pwm = 100.0f;
    }
    if (pwm < -100.0f) {
        pwm = -100.0f;
    }
    return pwm;
}

static void TAM_Mixer(const Bot_Wrench_t *wrench_out, float *thruster_pwm, const bot_tam_t *tam_config)
{
    float wrench_array[TAM_MAX_DOF];
    int t;
    int dof;

    wrench_array[0] = wrench_out->force_x;
    wrench_array[1] = wrench_out->force_y;
    wrench_array[2] = wrench_out->force_z;
    wrench_array[3] = wrench_out->torque_x;
    wrench_array[4] = wrench_out->torque_y;
    wrench_array[5] = wrench_out->torque_z;

    for (t = 0; t < (int)tam_config->active_thrusters; t++) {
        float total_thrust = 0.0f;
        for (dof = 0; dof < TAM_MAX_DOF; dof++) {
            total_thrust += tam_config->matrix[t][dof] * wrench_array[dof];
        }
        thruster_pwm[t] = Thruster_Map(total_thrust);
    }

    Normalize_Thruster_Outputs(&thruster_pwm[0], 4u, 100.0f);
    Normalize_Thruster_Outputs(&thruster_pwm[4], 2u, 100.0f);
}

static uint16_t Serialize_Body_Report(uint8_t *buf, const bot_body_state_t *body_state)
{
    uint16_t offset = 0u;

    if ((buf == NULL) || (body_state == NULL)) {
        return 0u;
    }

    memcpy(&buf[offset], &body_state->Quater[0], sizeof(float)); offset += sizeof(float);
    memcpy(&buf[offset], &body_state->Quater[1], sizeof(float)); offset += sizeof(float);
    memcpy(&buf[offset], &body_state->Quater[2], sizeof(float)); offset += sizeof(float);
    memcpy(&buf[offset], &body_state->Quater[3], sizeof(float)); offset += sizeof(float);
    memcpy(&buf[offset], &body_state->gyro_x, sizeof(float)); offset += sizeof(float);
    memcpy(&buf[offset], &body_state->gyro_y, sizeof(float)); offset += sizeof(float);
    memcpy(&buf[offset], &body_state->gyro_z, sizeof(float)); offset += sizeof(float);
    memcpy(&buf[offset], &body_state->vel_x, sizeof(float)); offset += sizeof(float);
    memcpy(&buf[offset], &body_state->vel_y, sizeof(float)); offset += sizeof(float);
    memcpy(&buf[offset], &body_state->vel_z, sizeof(float)); offset += sizeof(float);
    memcpy(&buf[offset], &body_state->acc_x, sizeof(float)); offset += sizeof(float);
    memcpy(&buf[offset], &body_state->acc_y, sizeof(float)); offset += sizeof(float);
    memcpy(&buf[offset], &body_state->acc_z, sizeof(float)); offset += sizeof(float);
    memcpy(&buf[offset], &body_state->depth_m, sizeof(float)); offset += sizeof(float);

    return offset;
}

static void Report_Body_State_To_OrangePi(const bot_body_state_t *body_state)
{
    uint8_t report_buf[13u * sizeof(float)];
    uint16_t report_len;

    if (body_state == NULL) {
        return;
    }

    report_len = Serialize_Body_Report(report_buf, body_state);
    if (report_len == 0u) {
        return;
    }

    Driver_Protocol_SendFrame(BSP_UART_OPI_RT,
                              DATA_TYPE_STATE_BODY,
                              report_buf,
                              (uint8_t)report_len,
                              USE_CPU);
}

static void prv_set_idle_output(void)
{
    Driver_Thruster_Set_Idle();
}

static void prv_armed_entry_spin_once(void)
{
    TickType_t spin_ticks = pdMS_TO_TICKS(ARMED_ENTRY_SPIN_DURATION_MS);
    int i;

    if (spin_ticks == 0u) {
        spin_ticks = 1u;
    }

    for (i = 0; i < THRUSTER_COUNT; i++) {
        prv_set_idle_output();
        Driver_Thruster_SetSpeed((bsp_pwm_ch_t)(BSP_PWM_THRUSTER_1 + i), ARMED_ENTRY_SPIN_SPEED);
        vTaskDelay(spin_ticks);
    }

    prv_set_idle_output();
}

static void prv_pause_standby_tasks(control_fsm_ctx_t *ctx)
{
    if (ctx->standby_tasks_paused) {
        return;
    }

    if (IMU_Task_Handler != NULL) {
        vTaskSuspend(IMU_Task_Handler);
    }
    if (MS5837_Task_Handler != NULL) {
        vTaskSuspend(MS5837_Task_Handler);
    }
    if (Power_Task_Handler != NULL) {
        vTaskSuspend(Power_Task_Handler);
    }
    if (DHT11_Task_Handler != NULL) {
        vTaskSuspend(DHT11_Task_Handler);
    }

    if (RT_Comm_Task_Handler != NULL) {
        vTaskSuspend(RT_Comm_Task_Handler);
    }
    Task_Comm_SetRtChannelEnabled(0u);

    ctx->standby_tasks_paused = 1u;
}

static void prv_resume_standby_tasks(control_fsm_ctx_t *ctx)
{
    if (!ctx->standby_tasks_paused) {
        return;
    }

    if (IMU_Task_Handler != NULL) {
        vTaskResume(IMU_Task_Handler);
    }
    if (MS5837_Task_Handler != NULL) {
        vTaskResume(MS5837_Task_Handler);
    }
    if (Power_Task_Handler != NULL) {
        vTaskResume(Power_Task_Handler);
    }
    if (DHT11_Task_Handler != NULL) {
        vTaskResume(DHT11_Task_Handler);
    }

    Task_Comm_SetRtChannelEnabled(1u);
    if (RT_Comm_Task_Handler != NULL) {
        vTaskResume(RT_Comm_Task_Handler);
    }

    ctx->standby_tasks_paused = 0u;
}

static void prv_apply_standby_kick_output(void)
{
    int i;

    /* STANDBY 仅用于岸上调试：避免 ESC 长时间静止啸叫 */
    prv_set_idle_output();
    for (i = 0; i < THRUSTER_COUNT; i++) {
        Driver_Thruster_SetSpeed((bsp_pwm_ch_t)(BSP_PWM_THRUSTER_1 + i), STANDBY_ESC_KICK_SPEED);
    }
}

/* -------------------------- 状态函数：STANDBY -------------------------- */
static void prv_standby_enter(control_fsm_ctx_t *ctx)
{
    Reset_All_Controllers();
    prv_set_idle_output();
    prv_pause_standby_tasks(ctx);

    ctx->standby_kick_active = 0u;
    ctx->standby_cycle_counter = 0u;
    ctx->standby_kick_cycles_left = 0u;
}

static void prv_standby_run(control_fsm_ctx_t *ctx)
{
    // 非脉冲状态下计数器递增，达到阈值时进入脉冲状态
    if (!ctx->standby_kick_active) {
        // 到达脉冲触发周期，进入脉冲状态  
        if ((uint16_t)(ctx->standby_cycle_counter + 1u) >= (uint16_t)STANDBY_ESC_KICK_INTERVAL_CYCLES) {
            uint16_t duration_cycles = (uint16_t)STANDBY_ESC_KICK_DURATION_CYCLES;
            if (duration_cycles == 0u) {
                duration_cycles = 1u;
            }
            ctx->standby_kick_active = 1u;                      ///< 进入脉冲状态
            ctx->standby_kick_cycles_left = duration_cycles;    ///< 设置脉冲持续周期
            ctx->standby_cycle_counter = 0u;                    ///< 重置循环计数器
            LOG_INFO("STANDBY kick start");
        } else {
            ctx->standby_cycle_counter++;
        }
    }
    
    // 根据当前是否处于脉冲状态，设置输出
    if (ctx->standby_kick_active) {
        prv_apply_standby_kick_output();
        if (ctx->standby_kick_cycles_left > 0u) {
            ctx->standby_kick_cycles_left--;
        } 

        // 脉冲持续周期为standby_kick_cycles_left，递减至0后退出脉冲状态
        if (ctx->standby_kick_cycles_left == 0u) {
            ctx->standby_kick_active = 0u;
            prv_set_idle_output();
            LOG_INFO("STANDBY kick end");
        }
    } else {
        prv_set_idle_output();
    }

    /* 进入睡眠，等待中断唤醒（UART/NVIC 等） */
    __WFI();
}

static void prv_standby_exit(control_fsm_ctx_t *ctx)
{
    ctx->standby_kick_active = 0u;
    ctx->standby_kick_cycles_left = 0u;
    ctx->standby_cycle_counter = 0u;

    prv_set_idle_output();
    prv_resume_standby_tasks(ctx);
}

/* -------------------------- 状态函数：DISARMED ------------------------- */
static void prv_disarmed_enter(control_fsm_ctx_t *ctx)
{
    (void)ctx;
    Reset_All_Controllers();
    prv_set_idle_output();
}

static void prv_disarmed_run(control_fsm_ctx_t *ctx)
{
    (void)ctx;
    prv_set_idle_output();
}

static void prv_disarmed_exit(control_fsm_ctx_t *ctx)
{
    (void)ctx;
}

/* --------------------------- 状态函数：ARMED --------------------------- */
static void prv_armed_enter(control_fsm_ctx_t *ctx)
{
    Reset_All_Controllers();
    ctx->last_armed_motion_mode = ctx->current_motion_mode;

    LOG_INFO("ARMED entry spin start");
    prv_armed_entry_spin_once();
    LOG_INFO("ARMED entry spin done");
}

static void prv_armed_run(control_fsm_ctx_t *ctx)
{
    int i;

    memset(&ctx->wrench_out, 0, sizeof(ctx->wrench_out));
    memset(ctx->thruster_pwm, 0, sizeof(ctx->thruster_pwm));

    if (ctx->current_motion_mode != ctx->last_armed_motion_mode) {
        Reset_All_Controllers();
        ctx->last_armed_motion_mode = ctx->current_motion_mode;
    }

    if (ctx->target.target_mode != (uint8_t)ctx->current_motion_mode) {
        Reset_All_Controllers();
        prv_set_idle_output();
        LOG_ERROR("Motion mode mismatch!");
        return;
    }

    switch (ctx->current_motion_mode) {
        case MOTION_STATE_MANUAL:
            ctx->wrench_out.force_x = ctx->target.cmd.manual_cmd.surge;
            ctx->wrench_out.force_y = ctx->target.cmd.manual_cmd.sway;
            ctx->wrench_out.force_z = ctx->target.cmd.manual_cmd.heave;
            ctx->wrench_out.torque_z = ctx->target.cmd.manual_cmd.yaw_cmd;
            break;

        case MOTION_STATE_STABILIZE:
            Driver_IMU_Quaternion_ToEuler_Deg(ctx->state.Quater,
                                              &ctx->euler_roll,
                                              &ctx->euler_pitch,
                                              &ctx->euler_yaw);

            ctx->wrench_out.torque_x = Cascade_PID_Update(&pid_roll,
                                                          0.0f,
                                                          ctx->euler_roll,
                                                          ctx->state.gyro_x,
                                                          TASK_CONTROL_PERIOD_S,
                                                          0u);
            ctx->wrench_out.torque_y = Cascade_PID_Update(&pid_pitch,
                                                          0.0f,
                                                          ctx->euler_pitch,
                                                          ctx->state.gyro_y,
                                                          TASK_CONTROL_PERIOD_S,
                                                          0u);
            ctx->wrench_out.torque_z = Cascade_PID_Update(&pid_yaw,
                                                          ctx->target.cmd.stab_cmd.target_yaw,
                                                          ctx->euler_yaw,
                                                          ctx->state.gyro_z,
                                                          TASK_CONTROL_PERIOD_S,
                                                          1u);

            {
                float target_depth = ctx->target.cmd.stab_cmd.target_depth;
                if (target_depth < 0.0f) {
                    target_depth = 0.0f;
                }
                if (target_depth > ctx->params->failsafe_max_depth) {
                    target_depth = ctx->params->failsafe_max_depth;
                }

                ctx->wrench_out.force_z = PID_Update(&pid_depth,
                                                     target_depth,
                                                     ctx->state.depth_m,
                                                     TASK_CONTROL_PERIOD_S,
                                                     0u);
            }

            ctx->wrench_out.force_x = ctx->target.cmd.stab_cmd.surge;
            ctx->wrench_out.force_y = ctx->target.cmd.stab_cmd.sway;
            break;

        case MOTION_STATE_AUTO:
            /* 预留：自动模式控制逻辑 */
            break;
    }

    TAM_Mixer(&ctx->wrench_out, ctx->thruster_pwm, &ctx->params->tam_config);

    for (i = 0; i < THRUSTER_COUNT; i++) {
        Driver_Thruster_SetSpeed((bsp_pwm_ch_t)(BSP_PWM_THRUSTER_1 + i), ctx->thruster_pwm[i]);
    }

    LOG_DEBUG("Thruster PWMs: [%.1f, %.1f, %.1f, %.1f, %.1f, %.1f]",
              ctx->thruster_pwm[0], ctx->thruster_pwm[1], ctx->thruster_pwm[2],
              ctx->thruster_pwm[3], ctx->thruster_pwm[4], ctx->thruster_pwm[5]);
}

static void prv_armed_exit(control_fsm_ctx_t *ctx)
{
    Reset_All_Controllers();
    prv_set_idle_output();
    (void)ctx;
}

/* ------------------------- 状态函数：FAILSAFE -------------------------- */
static void prv_failsafe_enter(control_fsm_ctx_t *ctx)
{
    (void)ctx;
    Reset_All_Controllers();
    prv_set_idle_output();
}

static void prv_failsafe_run(control_fsm_ctx_t *ctx)
{
    (void)ctx;
    prv_set_idle_output();
}

static void prv_failsafe_exit(control_fsm_ctx_t *ctx)
{
    (void)ctx;
}

/* 状态操作表 */
static const control_state_ops_t s_state_ops[] = {
    [SYS_MODE_STANDBY] = {
        "STANDBY",
        prv_standby_enter,
        prv_standby_run,
        prv_standby_exit
    },
    [SYS_MODE_ACTIVE_DISARMED] = {
        "DISARMED",
        prv_disarmed_enter,
        prv_disarmed_run,
        prv_disarmed_exit
    },
    [SYS_MODE_MOTION_ARMED] = {
        "ARMED",
        prv_armed_enter,
        prv_armed_run,
        prv_armed_exit
    },
    [SYS_MODE_FAILSAFE] = {
        "FAILSAFE",
        prv_failsafe_enter,
        prv_failsafe_run,
        prv_failsafe_exit
    }
};

static uint8_t prv_state_valid(bot_sys_mode_e mode)
{
    if ((mode < SYS_MODE_STANDBY) || (mode > SYS_MODE_FAILSAFE)) {
        return 0u;
    }
    if (s_state_ops[mode].on_run == NULL) {
        return 0u;
    }
    return 1u;
}

static void prv_fsm_transition(control_fsm_ctx_t *ctx, bot_sys_mode_e next_mode)
{
    if (ctx->initialized && prv_state_valid(ctx->current_mode)) {
        if (s_state_ops[ctx->current_mode].on_exit != NULL) {
            s_state_ops[ctx->current_mode].on_exit(ctx);
        }
    }

    if (!prv_state_valid(next_mode)) {
        next_mode = SYS_MODE_FAILSAFE;
    }

    ctx->current_mode = next_mode;
    ctx->initialized = 1u;

    LOG_INFO("Control FSM -> %s", s_state_ops[ctx->current_mode].name);

    if (s_state_ops[ctx->current_mode].on_enter != NULL) {
        s_state_ops[ctx->current_mode].on_enter(ctx);
    }
}

static void vTask_Control(void *pvParameters)
{
    bot_params_t *local_params = (bot_params_t *)pvParameters;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    control_fsm_ctx_t ctx;
    bot_sys_mode_e requested_mode = SYS_MODE_STANDBY;

    if (local_params == NULL) {
        vTaskDelete(NULL);
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.params = local_params;
    ctx.current_mode = (bot_sys_mode_e)0xFF;
    ctx.current_motion_mode = MOTION_STATE_MANUAL;
    ctx.last_armed_motion_mode = MOTION_STATE_MANUAL;

    Sync_Cascade_Config(&pid_roll, &local_params->pid_roll);
    Sync_Cascade_Config(&pid_pitch, &local_params->pid_pitch);
    Sync_Cascade_Config(&pid_yaw, &local_params->pid_yaw);
    Sync_PID_Config(&pid_depth, &local_params->pid_depth);

    while (1) {
        TickType_t period_ticks = pdMS_TO_TICKS(TASK_CONTROL_PERIOD_MS);

        Bot_State_Pull(&ctx.state);
        Bot_Target_Pull(&ctx.target);
        System_ModeManager_Pull(&requested_mode, &ctx.current_motion_mode, NULL);

        if ((!ctx.initialized) || (requested_mode != ctx.current_mode)) {
            prv_fsm_transition(&ctx, requested_mode);
        }

        if (prv_state_valid(ctx.current_mode)) {
            s_state_ops[ctx.current_mode].on_run(&ctx);
        } else {
            prv_set_idle_output();
        }

        /* STANDBY 下关闭 RT 遥测上报，降低实时通信负担 */
        if (ctx.current_mode != SYS_MODE_STANDBY) {
            Report_Body_State_To_OrangePi(&ctx.state);
        }

        Bot_Task_CheckIn_Monitor(TASK_ID_CONTROL);

        if (ctx.current_mode == SYS_MODE_STANDBY) {
            period_ticks = pdMS_TO_TICKS(TASK_CONTROL_STANDBY_PERIOD_MS);
        }
        vTaskDelayUntil(&xLastWakeTime, period_ticks);
    }
}

void Task_Control_Init(void)
{
    static bot_params_t local_params;

    Bot_Params_Pull(&local_params);
    LOG_INFO("Control Task PID Config Loaded: Roll PID (%.2f, %.2f, %.2f), Pitch PID (%.2f, %.2f, %.2f), Yaw PID (%.2f, %.2f, %.2f), Depth PID (%.2f, %.2f, %.2f)",
             local_params.pid_roll.outer.kp, local_params.pid_roll.outer.ki, local_params.pid_roll.outer.kd,
             local_params.pid_pitch.outer.kp, local_params.pid_pitch.outer.ki, local_params.pid_pitch.outer.kd,
             local_params.pid_yaw.outer.kp, local_params.pid_yaw.outer.ki, local_params.pid_yaw.outer.kd,
             local_params.pid_depth.kp, local_params.pid_depth.ki, local_params.pid_depth.kd);
    LOG_INFO("Control Task TAM Matrix:");
    for (uint8_t t = 0; t < local_params.tam_config.active_thrusters; t++) {
        LOG_INFO(" Thruster %d: [%.2f, %.2f, %.2f, %.2f, %.2f, %.2f]",
                 t + 1,
                 local_params.tam_config.matrix[t][0],
                 local_params.tam_config.matrix[t][1],
                 local_params.tam_config.matrix[t][2],
                 local_params.tam_config.matrix[t][3],
                 local_params.tam_config.matrix[t][4],
                 local_params.tam_config.matrix[t][5]);
    }

    xTaskCreate((TaskFunction_t)vTask_Control,
                (const char *)"Task_Control",
                (uint16_t)CONTROL_STK_SIZE,
                (void *)&local_params,
                (UBaseType_t)CONTROL_TASK_PRIO,
                (TaskHandle_t *)&Control_Task_Handler);
}
