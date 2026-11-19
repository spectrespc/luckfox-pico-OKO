#pragma once

/*
 * Minimal MQTT 3.1.1 packet codec.
 *
 * Pure encoding/decoding: no sockets, HAL, logging or configuration, so the
 * host test (tests/mqtt_test.c) builds the same code with plain gcc.
 */

#include <stddef.h>
#include <stdint.h>

#define MQTT_PKT_CONNACK 2
#define MQTT_PKT_PUBLISH 3
#define MQTT_PKT_PUBACK 4
#define MQTT_PKT_PINGREQ 12
#define MQTT_PKT_PINGRESP 13
#define MQTT_PKT_DISCONNECT 14

typedef struct {
    uint8_t type;         /* packet type, data[0] >> 4                  */
    uint8_t flags;        /* fixed header flags, data[0] & 0x0f         */
    const uint8_t *body;  /* variable header + payload                  */
    size_t bodyLen;       /* remaining length                           */
    size_t consumed;      /* header + body, for stream advancement      */
} mqtt_packet;

/* Writes the variable-length "remaining length" encoding.
 * Returns the number of bytes (1..4), or -1 when the value or buffer is
 * invalid. */
int mqtt_encode_remaining(uint8_t *out, size_t size, size_t value);

/* CONNECT with an optional will (retained, QoS1) and username/password.
 * Empty strings disable the corresponding block.  Returns the packet size or
 * -1. */
int mqtt_encode_connect(uint8_t *out, size_t size, const char *clientId,
    const char *username, const char *password, const char *willTopic,
    const char *willPayload, unsigned int keepaliveS);

/* PUBLISH for QoS 0 (packetId ignored) or QoS 1. */
int mqtt_encode_publish(uint8_t *out, size_t size, const char *topic,
    const void *payload, size_t payloadLen, int qos, int retain,
    uint16_t packetId);

int mqtt_encode_pingreq(uint8_t *out, size_t size);
int mqtt_encode_disconnect(uint8_t *out, size_t size);

/* Parses one packet at the start of the buffer.
 * Returns 1 for a complete packet, 0 when more bytes are needed and -1 for a
 * malformed remaining length. */
int mqtt_parse_packet(const uint8_t *data, size_t length, mqtt_packet *out);

/* CONNACK body: session-present flag and the return code (0 = accepted). */
int mqtt_parse_connack(const mqtt_packet *pkt, int *sessionPresent,
    int *returnCode);

/* PUBACK body: the acknowledged packet identifier. */
int mqtt_parse_puback(const mqtt_packet *pkt, uint16_t *packetId);