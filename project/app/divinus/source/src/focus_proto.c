#include "focus_proto.h"

static size_t u16_frame(uint8_t out[4], uint8_t cmd, uint8_t motor,
                        uint16_t value)
{
    if (!out)
        return 0;
    out[0] = cmd;
    out[1] = motor;
    out[2] = (uint8_t)(value >> 8);
    out[3] = (uint8_t)value;
    return 4;
}

size_t focus_proto_move(uint8_t out[4], uint8_t motor, int16_t steps)
{
    return u16_frame(out, FOCUS_CMD_MOVE, motor, (uint16_t)steps);
}

size_t focus_proto_speed(uint8_t out[4], uint8_t motor, uint16_t speed)
{
    return u16_frame(out, FOCUS_CMD_SET_SPEED, motor, speed);
}

size_t focus_proto_set_pos(uint8_t out[6], uint8_t motor, int32_t position)
{
    uint32_t value = (uint32_t)position;

    if (!out)
        return 0;
    out[0] = FOCUS_CMD_SET_POS;
    out[1] = motor;
    out[2] = (uint8_t)(value >> 24);
    out[3] = (uint8_t)(value >> 16);
    out[4] = (uint8_t)(value >> 8);
    out[5] = (uint8_t)value;
    return 6;
}

size_t focus_proto_stop(uint8_t out[2], uint8_t motor)
{
    if (!out) return 0;
    out[0] = FOCUS_CMD_STOP;
    out[1] = motor;
    return 2;
}

size_t focus_proto_get_pos(uint8_t out[2], uint8_t motor)
{
    if (!out) return 0;
    out[0] = FOCUS_CMD_GET_POS;
    out[1] = motor;
    return 2;
}

size_t focus_proto_get_status(uint8_t out[2], uint8_t motor)
{
    if (!out) return 0;
    out[0] = FOCUS_CMD_GET_STATUS;
    out[1] = motor;
    return 2;
}

size_t focus_proto_get_version(uint8_t out[1])
{
    if (!out) return 0;
    out[0] = FOCUS_CMD_GET_VERSION;
    return 1;
}

size_t focus_proto_reset(uint8_t out[1])
{
    if (!out) return 0;
    out[0] = FOCUS_CMD_RESET;
    return 1;
}

size_t focus_proto_enter_boot(uint8_t out[1])
{
    if (!out) return 0;
    out[0] = FOCUS_CMD_ENTER_BOOT;
    return 1;
}

int focus_proto_parse_pos(const uint8_t *in, size_t len, int32_t *position)
{
    if (!in || !position || len < 4)
        return -1;
    *position = (int32_t)(((uint32_t)in[0] << 24) |
                          ((uint32_t)in[1] << 16) |
                          ((uint32_t)in[2] << 8) |
                          (uint32_t)in[3]);
    return 0;
}

int focus_proto_parse_status(const uint8_t *in, size_t len, uint8_t *status)
{
    if (!in || !status || len < 1)
        return -1;
    *status = in[0];
    return 0;
}

int focus_proto_parse_version(const uint8_t *in, size_t len, uint8_t version[3])
{
    if (!in || !version || len < 3)
        return -1;
    version[0] = in[0];
    version[1] = in[1];
    version[2] = in[2];
    return 0;
}

int32_t focus_proto_clamp(int64_t value, int32_t min_pos, int32_t max_pos)
{
    if (value < (int64_t)min_pos)
        return min_pos;
    if (value > (int64_t)max_pos)
        return max_pos;
    return (int32_t)value;
}

int focus_proto_version_supported(const uint8_t version[3])
{
    if (!version)
        return 0;
    if (version[0] > 2)
        return 1;
    if (version[0] < 2)
        return 0;
    if (version[1] > 1)
        return 1;
    if (version[1] == 1)
        return version[2] >= 1;      /* 2.1.1+; 2.1.0 lacks SET_POS */
    return version[2] >= 2;          /* 2.0.2+ */
}