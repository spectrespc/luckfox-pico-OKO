#include "mqtt.h"

#include <string.h>

int mqtt_encode_remaining(uint8_t *out, size_t size, size_t value)
{
    int n = 0;

    if (!out || value > 268435455u) return -1;
    do {
        uint8_t byte = (uint8_t)(value % 128);

        value /= 128;
        if (value) byte |= 0x80;
        if ((size_t)n >= size) return -1;
        out[n++] = byte;
    } while (value);
    return n;
}

static int mqtt_put_u16(uint8_t *out, size_t size, size_t pos, size_t value)
{
    if (pos + 2 > size || value > 0xffff) return -1;
    out[pos] = (uint8_t)(value >> 8);
    out[pos + 1] = (uint8_t)(value & 0xff);
    return 0;
}

static int mqtt_string_size(const char *text)
{
    return text ? (int)strlen(text) : 0;
}

int mqtt_encode_connect(uint8_t *out, size_t size, const char *clientId,
    const char *username, const char *password, const char *willTopic,
    const char *willPayload, unsigned int keepaliveS)
{
    int clientLen = mqtt_string_size(clientId);
    int userLen = mqtt_string_size(username);
    int passLen = mqtt_string_size(password);
    int willTopicLen = mqtt_string_size(willTopic);
    int willPayloadLen = mqtt_string_size(willPayload);
    int hasWill = willTopicLen > 0;
    size_t remaining;
    size_t pos;
    size_t headerLen;
    int remainingLen;
    uint8_t flags = 0x02;   /* clean session */

    if (!out || !clientLen || clientLen > 0xffff ||
        userLen > 0xffff || passLen > 0xffff ||
        willTopicLen > 0xffff || willPayloadLen > 0xffff)
        return -1;

    if (hasWill) flags |= 0x04 | (1 << 3) | 0x20;  /* will, QoS1, retain */
    if (userLen) flags |= 0x80;
    if (passLen) flags |= 0x40;
    if (passLen && !userLen) return -1;            /* 3.1.1 requires a user */

    remaining = 2 + 4 + 1 + 1 + 2;                 /* protocol name .. ka */
    remaining += 2 + (size_t)clientLen;
    if (hasWill) remaining += 2 + (size_t)willTopicLen;
    if (hasWill) remaining += 2 + (size_t)willPayloadLen;
    if (userLen) remaining += 2 + (size_t)userLen;
    if (passLen) remaining += 2 + (size_t)passLen;
    if (remaining > 268435455u) return -1;

    if (size < 2) return -1;
    out[0] = 0x10;
    remainingLen = mqtt_encode_remaining(out + 1, size - 1, remaining);
    if (remainingLen < 1) return -1;
    headerLen = 1 + (size_t)remainingLen;
    pos = headerLen;

    /* Protocol name "MQTT", level 4. */
    if (mqtt_put_u16(out, size, pos, 4) || pos + 2 + 4 > size) return -1;
    memcpy(out + pos + 2, "MQTT", 4);
    pos += 6;
    if (pos + 4 > size) return -1;
    out[pos++] = 4;
    out[pos++] = flags;
    if (mqtt_put_u16(out, size, pos, keepaliveS)) return -1;
    pos += 2;

    if (mqtt_put_u16(out, size, pos, (size_t)clientLen) ||
        pos + 2 + (size_t)clientLen > size)
        return -1;
    memcpy(out + pos + 2, clientId, (size_t)clientLen);
    pos += 2 + (size_t)clientLen;

    if (hasWill) {
        if (mqtt_put_u16(out, size, pos, (size_t)willTopicLen) ||
            pos + 2 + (size_t)willTopicLen > size)
            return -1;
        memcpy(out + pos + 2, willTopic, (size_t)willTopicLen);
        pos += 2 + (size_t)willTopicLen;
        if (mqtt_put_u16(out, size, pos, (size_t)willPayloadLen) ||
            pos + 2 + (size_t)willPayloadLen > size)
            return -1;
        if (willPayloadLen)
            memcpy(out + pos + 2, willPayload, (size_t)willPayloadLen);
        pos += 2 + (size_t)willPayloadLen;
    }

    if (userLen) {
        if (mqtt_put_u16(out, size, pos, (size_t)userLen) ||
            pos + 2 + (size_t)userLen > size)
            return -1;
        memcpy(out + pos + 2, username, (size_t)userLen);
        pos += 2 + (size_t)userLen;
    }
    if (passLen) {
        if (mqtt_put_u16(out, size, pos, (size_t)passLen) ||
            pos + 2 + (size_t)passLen > size)
            return -1;
        memcpy(out + pos + 2, password, (size_t)passLen);
        pos += 2 + (size_t)passLen;
    }

    return pos == headerLen + remaining ? (int)pos : -1;
}

int mqtt_encode_publish(uint8_t *out, size_t size, const char *topic,
    const void *payload, size_t payloadLen, int qos, int retain,
    uint16_t packetId)
{
    int topicLen = mqtt_string_size(topic);
    size_t remaining;
    size_t headerLen;
    size_t pos;
    int remainingLen;

    if (!out || !topicLen || topicLen > 0xffff) return -1;
    if (qos < 0 || qos > 1) return -1;
    if (!payload && payloadLen) return -1;

    remaining = 2 + (size_t)topicLen + (qos ? 2 : 0) + payloadLen;
    if (remaining > 268435455u) return -1;
    if (size < 2) return -1;

    out[0] = (uint8_t)(0x30 | (qos << 1) | (retain ? 1 : 0));
    remainingLen = mqtt_encode_remaining(out + 1, size - 1, remaining);
    if (remainingLen < 1) return -1;
    headerLen = 1 + (size_t)remainingLen;
    if (size < headerLen) return -1;
    pos = headerLen;

    if (mqtt_put_u16(out, size, pos, (size_t)topicLen) ||
        pos + 2 + (size_t)topicLen > size)
        return -1;
    memcpy(out + pos + 2, topic, (size_t)topicLen);
    pos += 2 + (size_t)topicLen;

    if (qos) {
        if (mqtt_put_u16(out, size, pos, packetId)) return -1;
        pos += 2;
    }
    if (payloadLen) {
        if (pos + payloadLen > size) return -1;
        memcpy(out + pos, payload, payloadLen);
        pos += payloadLen;
    }

    return pos == headerLen + remaining ? (int)pos : -1;
}

int mqtt_encode_pingreq(uint8_t *out, size_t size)
{
    if (!out || size < 2) return -1;
    out[0] = 0xc0;
    out[1] = 0x00;
    return 2;
}

int mqtt_encode_disconnect(uint8_t *out, size_t size)
{
    if (!out || size < 2) return -1;
    out[0] = 0xe0;
    out[1] = 0x00;
    return 2;
}

int mqtt_parse_packet(const uint8_t *data, size_t length, mqtt_packet *out)
{
    size_t pos = 1;
    size_t value = 0;
    size_t multiplier = 1;
    int complete = 0;

    if (!data || !out) return -1;
    if (!length) return 0;

    while (pos < length) {
        uint8_t byte = data[pos++];

        value += (size_t)(byte & 0x7f) * multiplier;
        if (multiplier > 128u * 128u * 128u) return -1;
        multiplier *= 128;
        if (!(byte & 0x80)) {
            complete = 1;
            break;
        }
    }
    if (!complete) {
        /* Four length bytes without a terminator is malformed, anything
         * shorter simply needs more data. */
        return pos > 4 ? -1 : 0;
    }
    if (value > 268435455u) return -1;
    if (length - pos < value) return 0;

    out->type = data[0] >> 4;
    out->flags = data[0] & 0x0f;
    out->body = data + pos;
    out->bodyLen = value;
    out->consumed = pos + value;
    return 1;
}

int mqtt_parse_connack(const mqtt_packet *pkt, int *sessionPresent,
    int *returnCode)
{
    if (!pkt || !pkt->body || pkt->bodyLen != 2) return -1;
    if (pkt->type != MQTT_PKT_CONNACK) return -1;
    if (sessionPresent) *sessionPresent = pkt->body[0] & 0x01;
    if (returnCode) *returnCode = pkt->body[1];
    return 0;
}

int mqtt_parse_puback(const mqtt_packet *pkt, uint16_t *packetId)
{
    if (!pkt || !pkt->body || pkt->bodyLen != 2) return -1;
    if (pkt->type != MQTT_PKT_PUBACK) return -1;
    if (packetId)
        *packetId = (uint16_t)((pkt->body[0] << 8) | pkt->body[1]);
    return 0;
}