/* Host tests for the STM8 motor protocol codec. */
#include <stdio.h>
#include <string.h>

#include "focus_proto.h"

static int failures;

static void check(int condition, const char *name)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL %s\n", name);
        failures++;
    }
}

int main(void)
{
    uint8_t frame[8];
    uint8_t version[3];
    int32_t position;
    uint8_t status;
    size_t len;

    len = focus_proto_move(frame, FOCUS_MOTOR_FOCUS, 100);
    check(len == 4, "move length");
    check(frame[0] == 0x01 && frame[1] == 0 && frame[2] == 0 &&
          frame[3] == 100, "move +100");

    len = focus_proto_move(frame, FOCUS_MOTOR_ZOOM, -1);
    check(len == 4, "move -1 length");
    check(frame[2] == 0xFF && frame[3] == 0xFF, "move -1 big-endian");

    len = focus_proto_speed(frame, FOCUS_MOTOR_FOCUS, 500);
    check(len == 4 && frame[0] == 0x02 && frame[2] == 0x01 &&
          frame[3] == 0xF4, "speed 500");

    len = focus_proto_stop(frame, FOCUS_MOTOR_ZOOM);
    check(len == 2 && frame[0] == 0x03 && frame[1] == 1, "stop zoom");

    len = focus_proto_get_pos(frame, FOCUS_MOTOR_FOCUS);
    check(len == 2 && frame[0] == 0x04, "get position");

    len = focus_proto_get_status(frame, FOCUS_MOTOR_FOCUS);
    check(len == 2 && frame[0] == 0x05, "get status");

    len = focus_proto_get_version(frame);
    check(len == 1 && frame[0] == 0xFF, "get version");

    len = focus_proto_enter_boot(frame);
    check(len == 1 && frame[0] == 0x07, "enter boot");

    {
        const uint8_t raw[4] = { 0xFF, 0xFF, 0xFF, 0x9C }; /* -100 */
        check(focus_proto_parse_pos(raw, 4, &position) == 0, "parse pos");
        check(position == -100, "parse pos value");
        check(focus_proto_parse_pos(raw, 3, &position) != 0, "short pos");
    }

    check(focus_proto_parse_status((const uint8_t *)"\x02", 1, &status) == 0,
          "parse status");
    check(status == 2, "status value");
    check(focus_proto_parse_status((const uint8_t *)"", 0, &status) != 0,
          "short status");

    check(focus_proto_parse_version((const uint8_t *)"\x02\x00\x01", 3,
          version) == 0, "parse version");
    check(version[0] == 2 && version[1] == 0 && version[2] == 1,
          "version value");
    check(focus_proto_parse_version((const uint8_t *)"\x02", 1, version) != 0,
          "short version");

    if (failures)
    {
        fprintf(stderr, "%d protocol test(s) failed\n", failures);
        return 1;
    }
    printf("focus_proto: ok\n");
    return 0;
}