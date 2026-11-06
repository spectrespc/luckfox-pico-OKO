#pragma once

/* Pure codec for the STM8 motor protocol (0x01..0x07, 0xFF).
 *
 * The byte layout is the upstream one (src/i2c/i2c_proto.h in stm8-motor):
 *   MOVE/SET_SPEED : [cmd][motor][hi][lo]
 *   STOP           : [cmd][motor]
 *   GET_POS        : [cmd][motor]       -> int32 big-endian
 *   GET_STATUS     : [cmd][motor]       -> 1 byte
 *   GET_VERSION    : [0xFF]             -> major, minor, patch
 * No STM8 headers are used so the codec builds on the host for tests. */
#include <stddef.h>
#include <stdint.h>

#define FOCUS_CMD_MOVE        0x01u
#define FOCUS_CMD_SET_SPEED   0x02u
#define FOCUS_CMD_STOP        0x03u
#define FOCUS_CMD_GET_POS     0x04u
#define FOCUS_CMD_GET_STATUS  0x05u
#define FOCUS_CMD_RESET       0x06u
#define FOCUS_CMD_ENTER_BOOT  0x07u
#define FOCUS_CMD_GET_VERSION 0xFFu

#define FOCUS_MOTOR_FOCUS 0
#define FOCUS_MOTOR_ZOOM  1

size_t focus_proto_move(uint8_t out[4], uint8_t motor, int16_t steps);
size_t focus_proto_speed(uint8_t out[4], uint8_t motor, uint16_t speed);
size_t focus_proto_stop(uint8_t out[2], uint8_t motor);
size_t focus_proto_get_pos(uint8_t out[2], uint8_t motor);
size_t focus_proto_get_status(uint8_t out[2], uint8_t motor);
size_t focus_proto_get_version(uint8_t out[1]);
size_t focus_proto_reset(uint8_t out[1]);
size_t focus_proto_enter_boot(uint8_t out[1]);

/* Responses.  Return 0 on success, -1 on a short/invalid frame. */
int focus_proto_parse_pos(const uint8_t *in, size_t len, int32_t *position);
int focus_proto_parse_status(const uint8_t *in, size_t len, uint8_t *status);
int focus_proto_parse_version(const uint8_t *in, size_t len, uint8_t version[3]);