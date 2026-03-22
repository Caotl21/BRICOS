#include <string.h>

#include "driver_hydrocore.h"
#include "sys_data_pool.h"

typedef struct
{
    float a;
    float b;
    float c;
    float d;
} rt_motion_payload_t;

static float Read_Float_LE(const uint8_t *buf)
{
    float value = 0.0f;

    if (buf != NULL)
    {
        memcpy(&value, buf, sizeof(float));
    }

    return value;
}

static void On_Motion_Ctrl_Received(const uint8_t *payload, uint16_t len)
{
    bot_target_t target;
    rt_motion_payload_t motion;

    if ((payload == NULL) || (len != (uint16_t)(1u + (4u * sizeof(float)))))
    {
        return;
    }

    memset(&target, 0, sizeof(target));
    target.target_mode = payload[0];

    motion.a = Read_Float_LE(&payload[1]);
    motion.b = Read_Float_LE(&payload[1 + sizeof(float)]);
    motion.c = Read_Float_LE(&payload[1 + (2u * sizeof(float))]);
    motion.d = Read_Float_LE(&payload[1 + (3u * sizeof(float))]);

    switch (target.target_mode)
    {
        case MOTION_STATE_MANUAL:
            target.cmd.manual_cmd.surge = motion.a;
            target.cmd.manual_cmd.sway = motion.b;
            target.cmd.manual_cmd.heave = motion.c;
            target.cmd.manual_cmd.yaw_cmd = motion.d;
            Bot_Target_Push(&target);
            break;

        case MOTION_STATE_STABILIZE:
        case MOTION_STATE_AUTO:
            target.cmd.stab_cmd.surge = motion.a;
            target.cmd.stab_cmd.sway = motion.b;
            target.cmd.stab_cmd.target_depth = motion.c;
            target.cmd.stab_cmd.target_yaw = motion.d;
            Bot_Target_Push(&target);
            break;

        default:
            return;
    }
}

void Task_RT_Cmd_Init(void)
{
    Driver_Protocol_Register(DATA_TYPE_THRUSTER, On_Motion_Ctrl_Received);
}
