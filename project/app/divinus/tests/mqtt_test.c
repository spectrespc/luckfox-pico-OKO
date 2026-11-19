/*
 * Host test for the MQTT 3.1.1 codec (mqtt.c).
 *
 * Covers the varint remaining-length boundaries, the CONNECT flag matrix,
 * PUBLISH QoS0/QoS1 encoding, stream parsing of CONNACK/PUBACK/PINGRESP and
 * truncated or malformed input.  No HAL, network or configuration code is
 * needed.
 *
 * Run from the repository root:
 *   gcc -std=c99 -Wall -Wextra -O2 \
 *       -Iproject/app/divinus/source/src \
 *       project/app/divinus/tests/mqtt_test.c \
 *       project/app/divinus/source/src/mqtt.c \
 *       -o /tmp/mqtt_test && /tmp/mqtt_test
 */
#include <stdio.h>
#include <string.h>

#include "mqtt.h"

static int failures;

static void check(int condition, const char *name)
{
    if (!condition) {
        fprintf(stderr, "FAIL %s\n", name);
        failures++;
    } else {
        printf("ok %s\n", name);
    }
}

static void test_remaining(void)
{
    static const struct {
        size_t value;
        int length;
        uint8_t bytes[4];
    } cases[] = {
        { 0, 1, { 0x00 } },
        { 127, 1, { 0x7f } },
        { 128, 2, { 0x80, 0x01 } },
        { 16383, 2, { 0xff, 0x7f } },
        { 16384, 3, { 0x80, 0x80, 0x01 } },
        { 2097151, 3, { 0xff, 0xff, 0x7f } },
        { 2097152, 4, { 0x80, 0x80, 0x80, 0x01 } },
        { 268435455, 4, { 0xff, 0xff, 0xff, 0x7f } },
    };
    uint8_t buffer[8];

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char name[64];
        int n = mqtt_encode_remaining(buffer, sizeof(buffer), cases[i].value);

        snprintf(name, sizeof(name), "remaining %zu length", cases[i].value);
        check(n == cases[i].length, name);
        snprintf(name, sizeof(name), "remaining %zu bytes", cases[i].value);
        check(n == cases[i].length &&
            !memcmp(buffer, cases[i].bytes, (size_t)n), name);
    }

    check(mqtt_encode_remaining(buffer, sizeof(buffer), 268435456u) < 0,
        "remaining rejects > 28 bits");
    check(mqtt_encode_remaining(buffer, 1, 128) < 0,
        "remaining rejects a short buffer");
    check(mqtt_encode_remaining(NULL, 4, 0) < 0, "remaining rejects NULL");
}

static void test_connect(void)
{
    uint8_t packet[256];
    int n = mqtt_encode_connect(packet, sizeof(packet), "oko-1a2b", NULL,
        NULL, NULL, NULL, 60);

    check(n > 0, "connect encodes");
    check(n > 0 && packet[0] == 0x10, "connect header");
    check(n > 0 && packet[2] == 0x00 && packet[3] == 0x04 &&
        !memcmp(packet + 4, "MQTT", 4), "connect protocol name");
    check(n > 0 && packet[8] == 4, "connect level 3.1.1");
    check(n > 0 && packet[9] == 0x02, "connect clean session only");
    check(n > 0 && packet[10] == 0x00 && packet[11] == 60,
        "connect keepalive");
    check(n > 0 && packet[12] == 0x00 && packet[13] == 8 &&
        !memcmp(packet + 14, "oko-1a2b", 8), "connect client id");

    n = mqtt_encode_connect(packet, sizeof(packet), "oko-1a2b", "oko", "oko",
        "oko/oko-1a2b/status", "{\"online\":false}", 30);
    check(n > 0 && packet[9] == 0xee, "connect will+auth flags");
    check(n > 0 && packet[10] == 0x00 && packet[11] == 30,
        "connect keepalive 30");

    check(mqtt_encode_connect(packet, sizeof(packet), "id", NULL, "pass",
        NULL, NULL, 60) < 0, "connect rejects a password without a user");
    check(mqtt_encode_connect(packet, sizeof(packet), "", NULL, NULL, NULL,
        NULL, 60) < 0, "connect rejects an empty client id");
    check(mqtt_encode_connect(packet, 8, "oko-1a2b", NULL, NULL, NULL, NULL,
        60) < 0, "connect rejects a short buffer");
}

static void test_publish(void)
{
    uint8_t packet[128];
    mqtt_packet parsed;
    int n = mqtt_encode_publish(packet, sizeof(packet), "oko/id/event",
        "{\"a\":1}", 7, 1, 0, 0x1234);

    check(n > 0, "publish qos1 encodes");
    check(n > 0 && packet[0] == 0x32, "publish qos1 header");
    check(mqtt_parse_packet(packet, (size_t)n, &parsed) == 1,
        "publish parses back");
    check(parsed.type == MQTT_PKT_PUBLISH && parsed.consumed == (size_t)n,
        "publish type and length");
    check(parsed.bodyLen == 2 + 12 + 2 + 7, "publish remaining length");
    check(parsed.body[2 + 12] == 0x12 && parsed.body[2 + 12 + 1] == 0x34,
        "publish packet id");
    check(!memcmp(parsed.body + 2 + 12 + 2, "{\"a\":1}", 7),
        "publish payload");

    n = mqtt_encode_publish(packet, sizeof(packet), "oko/id/telemetry",
        "{}", 2, 0, 1, 0);
    check(n > 0 && packet[0] == 0x31, "publish qos0 retain header");
    check(mqtt_parse_packet(packet, (size_t)n, &parsed) == 1 &&
        parsed.bodyLen == 2 + 16 + 2, "publish qos0 has no packet id");

    check(mqtt_encode_publish(packet, 4, "oko/id/event", "{}", 2, 0, 0, 0) < 0,
        "publish rejects a short buffer");
    check(mqtt_encode_publish(packet, sizeof(packet), "", "{}", 2, 0, 0, 0) < 0,
        "publish rejects an empty topic");
    check(mqtt_encode_publish(packet, sizeof(packet), "t", "{}", 2, 2, 0, 1) < 0,
        "publish rejects qos2");
}

static void test_parse(void)
{
    static const uint8_t connack_ok[] = { 0x20, 0x02, 0x00, 0x00 };
    static const uint8_t connack_bad[] = { 0x20, 0x02, 0x01, 0x05 };
    static const uint8_t puback[] = { 0x40, 0x02, 0xbe, 0xef };
    static const uint8_t pingresp[] = { 0xd0, 0x00 };
    static const uint8_t malformed[] = { 0x30, 0xff, 0xff, 0xff, 0xff, 0x01 };
    uint8_t stream[32];
    mqtt_packet packet;
    int session = -1, code = -1;
    uint16_t id = 0;

    check(mqtt_parse_packet(connack_ok, sizeof(connack_ok), &packet) == 1,
        "connack parses");
    check(mqtt_parse_connack(&packet, &session, &code) == 0 && session == 0 &&
        code == 0, "connack accepted");

    check(mqtt_parse_packet(connack_bad, sizeof(connack_bad), &packet) == 1,
        "connack refusal parses");
    check(mqtt_parse_connack(&packet, &session, &code) == 0 && session == 1 &&
        code == 5, "connack refusal code");

    check(mqtt_parse_packet(puback, sizeof(puback), &packet) == 1,
        "puback parses");
    check(mqtt_parse_puback(&packet, &id) == 0 && id == 0xbeef,
        "puback id");

    check(mqtt_parse_packet(pingresp, sizeof(pingresp), &packet) == 1 &&
        packet.type == MQTT_PKT_PINGRESP && packet.bodyLen == 0,
        "pingresp parses");

    check(mqtt_parse_packet(connack_ok, 2, &packet) == 0,
        "truncated body needs more data");
    check(mqtt_parse_packet(connack_ok, 3, &packet) == 0,
        "truncated packet needs more data");
    check(mqtt_parse_packet(NULL, 4, &packet) == -1, "parse rejects NULL");
    check(mqtt_parse_packet(malformed, sizeof(malformed), &packet) == -1,
        "parse rejects an overlong remaining length");

    /* Two packets back to back: the first consumed length advances the
     * buffer, the second is parsed from the tail. */
    memcpy(stream, puback, sizeof(puback));
    memcpy(stream + sizeof(puback), pingresp, sizeof(pingresp));
    check(mqtt_parse_packet(stream, sizeof(puback) + sizeof(pingresp),
            &packet) == 1 && packet.consumed == sizeof(puback),
        "stream first packet");
    check(mqtt_parse_packet(stream + packet.consumed, sizeof(pingresp),
            &packet) == 1 && packet.type == MQTT_PKT_PINGRESP,
        "stream second packet");

    check(mqtt_parse_connack(&packet, NULL, NULL) == -1,
        "connack rejects another type");
}

static void test_control(void)
{
    uint8_t packet[4];

    check(mqtt_encode_pingreq(packet, sizeof(packet)) == 2 &&
        packet[0] == 0xc0 && packet[1] == 0x00, "pingreq bytes");
    check(mqtt_encode_disconnect(packet, sizeof(packet)) == 2 &&
        packet[0] == 0xe0 && packet[1] == 0x00, "disconnect bytes");
    check(mqtt_encode_pingreq(packet, 1) < 0, "pingreq short buffer");
}

int main(void)
{
    test_remaining();
    test_connect();
    test_publish();
    test_parse();
    test_control();

    if (failures) {
        fprintf(stderr, "%d mqtt codec check(s) failed\n", failures);
        return 1;
    }
    printf("all mqtt codec checks passed\n");
    return 0;
}