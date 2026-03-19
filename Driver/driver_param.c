#include "driver_param.h"
#include "bsp_flash.h"
#include <string.h>

#define PID_PARAM_MAGIC      ((uint32_t)0x50494450)  // "PIDP"
#define PID_PARAM_VERSION    ((uint16_t)1)

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t payload_len;
    bot_params_t params;
    uint32_t checksum;
} pid_param_blob_t;

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

void Driver_PidParam_FillDefault(bot_params_t *params)
{
    if (params == NULL) return;

    memset(params, 0, sizeof(*params));

//    params->motion_mode = MODE_MANUAL;

    params->pid_roll.kp  = 2.0f;
    params->pid_roll.ki  = 0.0f;
    params->pid_roll.kd  = 0.2f;
    params->pid_roll.max_out = 300.0f;

    params->pid_pitch.kp = 2.0f;
    params->pid_pitch.ki = 0.0f;
    params->pid_pitch.kd = 0.2f;
    params->pid_pitch.max_out = 300.0f;

    params->pid_yaw.kp   = 1.5f;
    params->pid_yaw.ki   = 0.0f;
    params->pid_yaw.kd   = 0.1f;
    params->pid_yaw.max_out = 300.0f;

    params->pid_depth.kp = 3.0f;
    params->pid_depth.ki = 0.0f;
    params->pid_depth.kd = 0.3f;
    params->pid_depth.max_out = 500.0f;

    params->failsafe_max_depth = 10.0f;
    params->failsafe_low_voltage = 10.0f;
}

bool Driver_PidParam_Load(bot_params_t *out_params)
{
    pid_param_blob_t blob;
    uint32_t calc;

    if (out_params == NULL) return false;

    bsp_flash_read(PID_PARAM_FLASH_ADDR, (uint8_t *)&blob, sizeof(blob));

    if (blob.magic != PID_PARAM_MAGIC) return false;
    if (blob.version != PID_PARAM_VERSION) return false;
    if (blob.payload_len != (uint16_t)sizeof(bot_params_t)) return false;

    calc = prv_checksum32((const uint8_t *)&blob.params, sizeof(bot_params_t));
    if (calc != blob.checksum) return false;

    memcpy(out_params, &blob.params, sizeof(bot_params_t));
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
    blob.checksum = prv_checksum32((const uint8_t *)&blob.params, sizeof(bot_params_t));

    if (!bsp_flash_erase(PID_PARAM_FLASH_ADDR, PID_PARAM_FLASH_ERASE_SZ))
    {
        return false;
    }

    if (!bsp_flash_write(PID_PARAM_FLASH_ADDR, (const uint8_t *)&blob, sizeof(blob)))
    {
        return false;
    }

    return true;
}
