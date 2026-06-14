#include "driver_param.h"
#include "bsp_flash.h"
#include "bsp_cpu.h"
#include <string.h>

#define PID_PARAM_MAGIC      ((uint32_t)0x50494450)  // "PIDP"
#define PID_PARAM_VERSION    ((uint16_t)3)

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t payload_len;
    bot_params_t params;
    uint32_t checksum;
} pid_param_blob_t;

pid_param_blob_t blob;

static uint32_t prv_checksum32(const uint8_t *data, uint32_t len)
{
    uint32_t sum = 0x13572468u;
    uint32_t i;
    for (i = 0; i < len; i++)
    {
        sum ^= data[i];
        sum = (sum << 5) | (sum >> 27);
        sum += 0x9E3779B9u;
    }
    return sum;
}

static void prv_fill_pid(PID_Controller_t *pid, float kp, float ki, float kd, float integral_max, float output_max)
{
    if (pid == NULL) return;

    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    pid->error_int = 0.0f;
    pid->error_last = 0.0f;
    pid->integral_max = integral_max;
    pid->output_max = output_max;
}

static void prv_fill_cascade_pid(Cascade_PID_t *pid,
                                 float outer_kp,
                                 float outer_ki,
                                 float outer_kd,
                                 float outer_integral_max,
                                 float outer_output_max,
                                 float inner_kp,
                                 float inner_ki,
                                 float inner_kd,
                                 float inner_integral_max,
                                 float inner_output_max)
{
    if (pid == NULL) return;

    prv_fill_pid(&pid->outer, outer_kp, outer_ki, outer_kd, outer_integral_max, outer_output_max);
    prv_fill_pid(&pid->inner, inner_kp, inner_ki, inner_kd, inner_integral_max, inner_output_max);
}

static void prv_clear_runtime_state(bot_params_t *params)
{
    if (params == NULL) return;

    params->pid_roll.outer.error_int = 0.0f;
    params->pid_roll.outer.error_last = 0.0f;
    params->pid_roll.inner.error_int = 0.0f;
    params->pid_roll.inner.error_last = 0.0f;

    params->pid_pitch.outer.error_int = 0.0f;
    params->pid_pitch.outer.error_last = 0.0f;
    params->pid_pitch.inner.error_int = 0.0f;
    params->pid_pitch.inner.error_last = 0.0f;

    params->pid_yaw.outer.error_int = 0.0f;
    params->pid_yaw.outer.error_last = 0.0f;
    params->pid_yaw.inner.error_int = 0.0f;
    params->pid_yaw.inner.error_last = 0.0f;

    params->pid_depth.error_int = 0.0f;
    params->pid_depth.error_last = 0.0f;
}

void Driver_PidParam_FillDefault(bot_params_t *params)
{
    if (params == NULL) return;

    memset(params, 0, sizeof(*params));

    prv_fill_cascade_pid(&params->pid_roll,
                         2.0f, 0.0f, 0.2f, 30.0f, 45.0f,
                         1.2f, 0.0f, 0.1f, 120.0f, 300.0f);

    prv_fill_cascade_pid(&params->pid_pitch,
                         2.0f, 0.0f, 0.2f, 30.0f, 45.0f,
                         1.2f, 0.0f, 0.1f, 120.0f, 300.0f);

    prv_fill_cascade_pid(&params->pid_yaw,
                         1.5f, 0.0f, 0.1f, 45.0f, 60.0f,
                         1.0f, 0.0f, 0.08f, 120.0f, 300.0f);

    prv_fill_pid(&params->pid_depth, 3.0f, 0.0f, 0.3f, 50.0f, 500.0f);

    params->failsafe_max_depth = 10.0f;
    params->failsafe_low_voltage = 10.0f;


    // ==========================================
    // 动态推力分配矩阵 (TAM) 默认初始化
    // ==========================================
    params->tam_config.active_thrusters = 6; 
    
    
    const float default_tam[6][TAM_MAX_DOF] = {
    /* T1 */ {  0.707107f,  0.707107f,  0.000000f,  0.000000f,  0.000000f,  0.180666f },
    /* T2 */ { -0.707107f,  0.707107f,  0.000000f,  0.000000f,  0.000000f,  0.180100f }, // 取负
    /* T3 */ { -0.707107f,  0.707107f,  0.000000f,  0.000000f,  0.000000f, -0.180666f },
    /* T4 */ {  0.707107f,  0.707107f,  0.000000f,  0.000000f,  0.000000f, -0.180100f }, // 取负
    /* T5 */ {  0.000000f,  0.000000f, -1.000000f,  0.123500f,  0.000000f,  0.000000f },
    /* T6 */ {  0.000000f,  0.000000f,  1.000000f,  0.123500f,  0.000000f,  0.000000f }  // 取负
    };

    // 拷贝至参数池 (按推进器数量逐行拷贝)
    for (int t = 0; t < 6; t++) {
        for (int dof = 0; dof < TAM_MAX_DOF; dof++) {
            params->tam_config.matrix[t][dof] = default_tam[t][dof];
        }
    }
}

bool Driver_PidParam_Load(bot_params_t *out_params)
{
    
    uint32_t calc;

    if (out_params == NULL) return false;

    bsp_flash_read(PID_PARAM_FLASH_ADDR, (uint8_t *)&blob, sizeof(blob));

    if (blob.magic != PID_PARAM_MAGIC) return false;
    if (blob.version != PID_PARAM_VERSION) return false;
    if (blob.payload_len != (uint16_t)sizeof(bot_params_t)) return false;

    calc = prv_checksum32((const uint8_t *)&blob.params, sizeof(bot_params_t));
    if (calc != blob.checksum) return false;

    memcpy(out_params, &blob.params, sizeof(bot_params_t));
    prv_clear_runtime_state(out_params);

    // 防止读取到不可理喻的推进器数量导致后续数组越界
    if (out_params->tam_config.active_thrusters > TAM_MAX_THRUSTERS) {
        out_params->tam_config.active_thrusters = TAM_MAX_THRUSTERS;
    }

    return true;
}

bool Driver_PidParam_Save(const bot_params_t *in_params)
{
    pid_param_blob_t blob;

    if (in_params == NULL) return false;

    blob.magic = PID_PARAM_MAGIC;
    blob.version = PID_PARAM_VERSION;
    blob.payload_len = (uint16_t)sizeof(bot_params_t);
    memcpy(&blob.params, in_params, sizeof(bot_params_t));
    prv_clear_runtime_state(&blob.params);
    blob.checksum = prv_checksum32((const uint8_t *)&blob.params, sizeof(bot_params_t));

    if (!bsp_flash_erase(PID_PARAM_FLASH_ADDR, PID_PARAM_FLASH_ERASE_SZ))
    {
        return false;
    }

    if (!bsp_flash_write(PID_PARAM_FLASH_ADDR, (const uint8_t *)&blob, sizeof(blob)))
    {
        return false;
    }
    bsp_cpu_reset();
    return true;
}
