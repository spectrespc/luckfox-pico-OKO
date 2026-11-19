#include "mqtt_post.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "app_config.h"
#include "hal/globals.h"
#include "hal/macros.h"
#include "mqtt.h"
#include "network.h"

#ifndef GIT_REV
#define GIT_REV "unknown"
#endif

/* A small RAM queue: the camera may lose the broker for minutes, but an
 * unbounded backlog would eat the ~64 MiB headroom the analytics threads use.
 * On overflow the oldest message is dropped and counted. */
#define MQTT_QUEUE_MAX 64
/* Largest outgoing packet: a 64 KiB oko-ec snapshot plus topic and header. */
#define MQTT_BUFFER 70000
#define MQTT_RETRY_COUNT 3
#define MQTT_CONNECT_TIMEOUT_MS 5000
#define MQTT_ACK_TIMEOUT_MS 5000

static const int mqttRetryMs[MQTT_RETRY_COUNT] = { 30000, 120000, 600000 };

static pthread_t mqttPid;
/* The loop must observe a local stop flag: pthread_join() alone deadlocks when
 * the loop only checks the global keepRunning (same as http_post). */
static volatile int mqttRun;
static volatile int mqttReload;
static volatile int mqttConnected;
static int mqttStarted;

static pthread_mutex_t mqttMtx = PTHREAD_MUTEX_INITIALIZER;
static char *mqttQueue[MQTT_QUEUE_MAX];
static int mqttQueueHead;
static int mqttQueueCount;
static unsigned long mqttSent;
static unsigned long mqttFailed;
static unsigned long mqttDropped;
static char mqttLastError[160];

static uint64_t mqtt_now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

static void mqtt_set_error(const char *text)
{
    pthread_mutex_lock(&mqttMtx);
    snprintf(mqttLastError, sizeof(mqttLastError), "%s", text ? text : "");
    pthread_mutex_unlock(&mqttMtx);
}

static void mqtt_json_escape(char *out, size_t size, const char *text)
{
    size_t used = 0;

    if (!out || !size) return;
    out[0] = 0;
    if (!text) return;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        char add[8];
        size_t addLen;

        if (*p == '"' || *p == '\\') {
            add[0] = '\\';
            add[1] = (char)*p;
            addLen = 2;
        } else if (*p < 0x20) {
            snprintf(add, sizeof(add), "\\u%04x", *p);
            addLen = 6;
        } else {
            add[0] = (char)*p;
            addLen = 1;
        }
        if (used + addLen + 1 >= size) break;
        memcpy(out + used, add, addLen);
        used += addLen;
    }
    out[used] = 0;
}

/* "none" is the factory value of the quoted yaml keys; treat it as unset so
 * the bench defaults do not send a literal "none:none" credential. */
static const char *mqtt_cred(const char *value)
{
    return value && *value && strcasecmp(value, "none") ? value : NULL;
}

/* ------------------------------------------------------------------ */
/* Topics                                                             */

int mqtt_post_client_id(char *out, size_t size)
{
    if (!out || size < 2) return EXIT_FAILURE;
    if (app_config.mqtt_client_id[0]) {
        snprintf(out, size, "%s", app_config.mqtt_client_id);
        return EXIT_SUCCESS;
    }
    if (mdns_hostname(out, size) == 0) return EXIT_SUCCESS;
    snprintf(out, size, "%s", "oko-cam");
    return EXIT_SUCCESS;
}

static int mqtt_base_topic(char *out, size_t size)
{
    char id[64];
    const char *prefix = app_config.mqtt_topic_prefix[0] ?
        app_config.mqtt_topic_prefix : "oko";

    if (!out || !size) return EXIT_FAILURE;
    mqtt_post_client_id(id, sizeof(id));
    if (snprintf(out, size, "%s/%s", prefix, id) >= (int)size)
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}

int mqtt_post_topic(char *out, size_t size, const char *suffix)
{
    char base[128];

    if (!out || !size || !suffix) return EXIT_FAILURE;
    if (mqtt_base_topic(base, sizeof(base))) return EXIT_FAILURE;
    if (snprintf(out, size, "%s/%s", base, suffix) >= (int)size)
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* Queue                                                              */

static int mqtt_enqueue(const char *json)
{
    char *copy;

    if (!json || !*json) return EXIT_FAILURE;
    copy = strdup(json);
    if (!copy) return EXIT_FAILURE;

    pthread_mutex_lock(&mqttMtx);
    if (mqttQueueCount == MQTT_QUEUE_MAX) {
        int oldest = mqttQueueHead;

        free(mqttQueue[oldest]);
        mqttQueue[oldest] = NULL;
        mqttQueueHead = (mqttQueueHead + 1) % MQTT_QUEUE_MAX;
        mqttQueueCount--;
        mqttDropped++;
    }
    mqttQueue[(mqttQueueHead + mqttQueueCount) % MQTT_QUEUE_MAX] = copy;
    mqttQueueCount++;
    pthread_mutex_unlock(&mqttMtx);
    return EXIT_SUCCESS;
}

int mqtt_post_event_json(const char *json)
{
    if (!app_config.mqtt_enable || !app_config.mqtt_publish_events)
        return EXIT_FAILURE;
    return mqtt_enqueue(json);
}

int mqtt_post_heartbeat_json(const char *json)
{
    return mqtt_post_event_json(json);
}

int mqtt_post_motion(unsigned long long tsMs, unsigned int areaPermille,
    const char *cause)
{
    char json[256];
    char safeCause[32];

    mqtt_json_escape(safeCause, sizeof(safeCause), cause ? cause : "md");
    snprintf(json, sizeof(json),
        "{\"type\":\"motion\",\"event_id\":\"\",\"cause\":\"%s\","
        "\"ts_ms\":%llu,\"area_permille\":%u}",
        safeCause, tsMs, areaPermille);
    return mqtt_post_event_json(json);
}

int mqtt_post_event(const char *type, const char *eventId, const char *cause,
    const char *model, unsigned long long tsMs, unsigned int frames)
{
    char json[768];
    char safeType[32], safeId[64], safeCause[32], safeModel[160];

    mqtt_json_escape(safeType, sizeof(safeType), type ? type : "event");
    mqtt_json_escape(safeId, sizeof(safeId), eventId ? eventId : "");
    mqtt_json_escape(safeCause, sizeof(safeCause), cause ? cause : "");
    mqtt_json_escape(safeModel, sizeof(safeModel), model ? model : "");
    snprintf(json, sizeof(json),
        "{\"type\":\"%s\",\"event_id\":\"%s\",\"cause\":\"%s\","
        "\"model\":\"%s\",\"ts_ms\":%llu,\"frames\":%u}",
        safeType, safeId, safeCause, safeModel, tsMs, frames);
    return mqtt_post_event_json(json);
}

int mqtt_post_clip(const char *eventId, const char *cause, const char *model,
    unsigned long long tsMs, unsigned long long durationMs,
    unsigned long long bytes)
{
    char json[768];
    char safeId[64], safeCause[32], safeModel[160];

    mqtt_json_escape(safeId, sizeof(safeId), eventId ? eventId : "");
    mqtt_json_escape(safeCause, sizeof(safeCause), cause ? cause : "");
    mqtt_json_escape(safeModel, sizeof(safeModel), model ? model : "");
    snprintf(json, sizeof(json),
        "{\"type\":\"clip\",\"event_id\":\"%s\",\"cause\":\"%s\","
        "\"model\":\"%s\",\"ts_ms\":%llu,\"duration_ms\":%llu,"
        "\"bytes\":%llu}",
        safeId, safeCause, safeModel, tsMs, durationMs, bytes);
    return mqtt_post_event_json(json);
}

int mqtt_post_heartbeat(unsigned long long tsMs, unsigned int idleS,
    int motionAreaPermille, int sleep, unsigned int wakeCycle)
{
    char json[320];

    snprintf(json, sizeof(json),
        "{\"type\":\"heartbeat\",\"event_id\":\"\",\"cause\":\"heartbeat\","
        "\"ts_ms\":%llu,\"idle_s\":%u,\"motion_area_permille\":%d,"
        "\"sleep\":%d,\"wake_cycle\":%u}",
        tsMs, idleS, motionAreaPermille, sleep ? 1 : 0, wakeCycle);
    return mqtt_post_event_json(json);
}

/* ------------------------------------------------------------------ */
/* Socket helpers                                                     */

static int mqtt_write_all(int fd, const void *data, size_t length)
{
    const char *cursor = data;

    while (length) {
        ssize_t written = write(fd, cursor, length);

        if (written < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        cursor += written;
        length -= (size_t)written;
    }
    return 0;
}

/* Accepts "host" and "host:port", default 1883 (same shape as the HTTP
 * sender, where the bench receiver uses a high port). */
static int mqtt_connect_socket(const char *host, int timeoutMs)
{
    struct addrinfo hints, *server = NULL, *addr;
    char address[128];
    char port[8] = "1883";
    char *colon;
    int fd = -1;

    if (!host || !*host) return -1;
    snprintf(address, sizeof(address), "%s", host);
    colon = strrchr(address, ':');
    if (colon && colon[1] && !strchr(colon + 1, ':')) {
        *colon = 0;
        snprintf(port, sizeof(port), "%s", colon + 1);
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(address, port, &hints, &server)) return -1;

    for (addr = server; addr; addr = addr->ai_next) {
        int flags;
        fd_set writeFds;
        struct timeval tv = { .tv_sec = timeoutMs / 1000,
            .tv_usec = (timeoutMs % 1000) * 1000 };

        fd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
        if (fd < 0) continue;

        flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        if (connect(fd, addr->ai_addr, addr->ai_addrlen) == 0) {
            fcntl(fd, F_SETFL, flags);
            break;
        }
        if (errno == EINPROGRESS) {
            int error = 0;
            socklen_t len = sizeof(error);

            FD_ZERO(&writeFds);
            FD_SET(fd, &writeFds);
            if (select(fd + 1, NULL, &writeFds, NULL, &tv) > 0 &&
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) == 0 &&
                error == 0) {
                fcntl(fd, F_SETFL, flags);
                break;
            }
        }
        close(fd);
        fd = -1;
    }

    freeaddrinfo(server);
    return fd;
}

/* Reads one packet, preserving a partial tail in `have`.  Returns 1 for a
 * packet, 0 when the timeout expired and -1 on a closed or broken socket. */
static int mqtt_read_packet(int fd, mqtt_packet *packet, uint8_t *buffer,
    size_t size, size_t *have, int timeoutMs)
{
    uint64_t deadline = mqtt_now_ms() + (uint64_t)timeoutMs;

    for (;;) {
        int parsed = mqtt_parse_packet(buffer, *have, packet);

        if (parsed == 1) {
            memmove(buffer, buffer + packet->consumed,
                *have - packet->consumed);
            *have -= packet->consumed;
            return 1;
        }
        if (parsed < 0) return -1;
        if (*have >= size) return -1;

        {
            fd_set readFds;
            struct timeval tv;
            uint64_t now = mqtt_now_ms();
            int waitMs = now >= deadline ? 0 : (int)(deadline - now);
            int ready;

            FD_ZERO(&readFds);
            FD_SET(fd, &readFds);
            tv.tv_sec = waitMs / 1000;
            tv.tv_usec = (waitMs % 1000) * 1000;
            ready = select(fd + 1, &readFds, NULL, NULL, &tv);
            if (ready == 0) return 0;
            if (ready < 0) {
                if (errno == EINTR) continue;
                return -1;
            }
        }

        {
            ssize_t got = read(fd, buffer + *have, size - *have);

            if (got <= 0) {
                if (got < 0 && errno == EINTR) continue;
                return -1;
            }
            *have += (size_t)got;
        }
    }
}

/* QoS0 or QoS1 publish; a QoS1 message waits for its PUBACK here, inside the
 * sender thread, so the callers stay non-blocking. */
static int mqtt_publish(int fd, uint8_t *out, size_t outSize, uint8_t *rx,
    size_t rxSize, size_t *have, const char *topic, const char *payload,
    size_t payloadLen, int qos, int retain, uint16_t packetId)
{
    int length = mqtt_encode_publish(out, outSize, topic, payload, payloadLen,
        qos, retain, packetId);

    if (length < 0) return -1;
    if (mqtt_write_all(fd, out, (size_t)length)) return -1;
    if (!qos) return 0;

    {
        uint64_t deadline = mqtt_now_ms() + MQTT_ACK_TIMEOUT_MS;

        while (mqttRun && keepRunning && mqtt_now_ms() < deadline) {
            mqtt_packet packet;
            uint64_t left = deadline - mqtt_now_ms();
            int result = mqtt_read_packet(fd, &packet, rx, rxSize, have,
                (int)left);

            if (result < 0) return -1;
            if (result == 0) return -1;
            if (packet.type == MQTT_PKT_PUBACK) {
                uint16_t ackId = 0;

                if (!mqtt_parse_puback(&packet, &ackId) && ackId == packetId)
                    return 0;
            }
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Session                                                            */

static int mqtt_publish_status(int fd, uint8_t *out, size_t outSize,
    uint8_t *rx, size_t rxSize, size_t *have, int online)
{
    char topic[160], payload[96];

    if (mqtt_post_topic(topic, sizeof(topic), "status")) return -1;
    if (online)
        snprintf(payload, sizeof(payload),
            "{\"online\":true,\"fw\":\"%s\"}", GIT_REV);
    else
        snprintf(payload, sizeof(payload), "{\"online\":false}");
    return mqtt_publish(fd, out, outSize, rx, rxSize, have, topic, payload,
        strlen(payload), 0, 1, 0);
}

static int mqtt_open(uint8_t *out, size_t outSize, uint8_t *rx, size_t rxSize,
    size_t *have)
{
    char clientId[64], base[128], willTopic[160];
    const char *username = mqtt_cred(app_config.mqtt_login);
    const char *password = mqtt_cred(app_config.mqtt_password);
    const char *host = mqtt_cred(app_config.mqtt_host);
    uint8_t connectPacket[512];
    char error[160];
    mqtt_packet packet;
    int length, fd, result;
    int session = 0, code = -1;

    if (!host) return -1;
    if (mqtt_base_topic(base, sizeof(base))) return -1;
    snprintf(willTopic, sizeof(willTopic), "%s/status", base);
    mqtt_post_client_id(clientId, sizeof(clientId));

    length = mqtt_encode_connect(connectPacket, sizeof(connectPacket),
        clientId, username, password, willTopic, "{\"online\":false}",
        app_config.mqtt_keepalive_s ? app_config.mqtt_keepalive_s : 60);
    if (length < 0) {
        mqtt_set_error("CONNECT encoding failed");
        return -1;
    }

    fd = mqtt_connect_socket(host, MQTT_CONNECT_TIMEOUT_MS);
    if (fd < 0) {
        snprintf(error, sizeof(error), "connect to %s failed", host);
        mqtt_set_error(error);
        return -1;
    }
    {
        struct timeval timeout = { .tv_sec = 5, .tv_usec = 0 };

        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    }

    *have = 0;
    if (mqtt_write_all(fd, connectPacket, (size_t)length)) {
        mqtt_set_error("CONNECT write failed");
        close(fd);
        return -1;
    }

    result = mqtt_read_packet(fd, &packet, rx, rxSize, have,
        MQTT_CONNECT_TIMEOUT_MS);
    if (result <= 0 || mqtt_parse_connack(&packet, &session, &code)) {
        mqtt_set_error("no CONNACK from the broker");
        close(fd);
        return -1;
    }
    if (code != 0) {
        snprintf(error, sizeof(error), "broker refused the connection (code %d)",
            code);
        mqtt_set_error(error);
        close(fd);
        return -1;
    }

    mqttConnected = 1;
    mqtt_set_error("");
    (void)mqtt_publish_status(fd, out, outSize, rx, rxSize, have, 1);
    HAL_INFO("mqtt", "Connected to %s as %s\n", host, clientId);
    return fd;
}

/* ------------------------------------------------------------------ */
/* Telemetry                                                          */

/* Mirrors GET /api/telemetry (server.c): the oko-ec daemon writes the file
 * atomically, but a body without the trailing '}' is treated as stale rather
 * than as a broken sample. */
static int mqtt_build_telemetry(char *out, size_t size)
{
    const char *path = "/tmp/oko-ec.json";
    struct stat st;
    time_t now = time(NULL);
    long age = -1;
    FILE *file;
    size_t used;

    if (!out || size < 256) return -1;
    if (stat(path, &st) || !S_ISREG(st.st_mode)) {
        int len = snprintf(out, size,
            "{\"ts\":%lld,\"available\":false,\"age\":-1}",
            (long long)now);
        return len > 0 ? len : -1;
    }
    age = (long)(now - st.st_mtime);
    if (age < 0) age = 0;
    if (age > 30 || st.st_size <= 0 || st.st_size > 65536) {
        int len = snprintf(out, size,
            "{\"ts\":%lld,\"available\":false,\"age\":%ld}",
            (long long)now, age);
        return len > 0 ? len : -1;
    }

    file = fopen(path, "rb");
    if (!file) {
        int len = snprintf(out, size,
            "{\"ts\":%lld,\"available\":false,\"age\":%ld}",
            (long long)now, age);
        return len > 0 ? len : -1;
    }

    used = (size_t)snprintf(out, size,
        "{\"ts\":%lld,\"available\":true,\"data\":", (long long)now);
    if (used + (size_t)st.st_size + 2 > size) {
        fclose(file);
        return -1;
    }
    {
        size_t got = fread(out + used, 1, (size_t)st.st_size, file);

        fclose(file);
        if (got != (size_t)st.st_size) return -1;
        used += got;
        while (used > 0 && (out[used - 1] == '\n' || out[used - 1] == '\r' ||
                out[used - 1] == ' '))
            used--;
        if (!used || out[used - 1] != '}') {
            int len = snprintf(out, size,
                "{\"ts\":%lld,\"available\":false,\"age\":%ld}",
                (long long)now, age);
            return len > 0 ? len : -1;
        }
    }
    out[used++] = '}';
    return used > size ? -1 : (int)used;
}

/* ------------------------------------------------------------------ */
/* Worker                                                             */

static void mqtt_pause(int ms)
{
    int elapsed = 0;

    while (elapsed < ms && mqttRun && keepRunning) {
        usleep(100 * 1000);
        elapsed += 100;
    }
}

static void mqtt_close_session(int *fd, size_t *have, uint8_t *out,
    size_t outSize, uint8_t *rx, size_t rxSize)
{
    char base[128];

    if (*fd >= 0) {
        if (!mqtt_base_topic(base, sizeof(base))) {
            mqtt_publish_status(*fd, out, outSize, rx, rxSize, have, 0);
            {
                uint8_t disconnect[4];
                int length = mqtt_encode_disconnect(disconnect,
                    sizeof(disconnect));

                if (length > 0)
                    (void)mqtt_write_all(*fd, disconnect, (size_t)length);
            }
        }
        close(*fd);
        *fd = -1;
    }
    *have = 0;
    mqttConnected = 0;
}

static void *mqtt_thread(void *arg)
{
    uint8_t *out = malloc(MQTT_BUFFER);
    char *telemetry = malloc(MQTT_BUFFER);
    char base[128], eventTopic[160], telemetryTopic[160];
    uint8_t rxBuffer[256];
    size_t have = 0;
    uint16_t packetId = 1;
    int fd = -1;
    int retryIndex = 0;
    uint64_t nextConnect = 0, telemetryNext = 0;
    uint64_t lastRx = 0, lastPing = 0;

    (void)arg;
    if (!out || !telemetry) {
        HAL_WARNING("mqtt", "Out of memory for the sender buffers\n");
        free(out);
        free(telemetry);
        return NULL;
    }

    while (mqttRun && keepRunning) {
        uint64_t now;
        uint32_t keepalive;

        if (!app_config.mqtt_enable) break;
        now = mqtt_now_ms();

        if (fd >= 0 && mqttReload) {
            mqttReload = 0;
            mqtt_close_session(&fd, &have, out, MQTT_BUFFER, rxBuffer,
                sizeof(rxBuffer));
            nextConnect = 0;
            retryIndex = 0;
            continue;
        }

        if (fd < 0) {
            if (now < nextConnect) {
                mqtt_pause(200);
                continue;
            }
            fd = mqtt_open(out, MQTT_BUFFER, rxBuffer, sizeof(rxBuffer),
                &have);
            if (fd < 0) {
                int delay = mqttRetryMs[retryIndex < MQTT_RETRY_COUNT ?
                    retryIndex : MQTT_RETRY_COUNT - 1];

                if (retryIndex < MQTT_RETRY_COUNT - 1) retryIndex++;
                nextConnect = mqtt_now_ms() + (uint64_t)delay;
                HAL_WARNING("mqtt", "Retrying in %d s\n", delay / 1000);
                continue;
            }
            retryIndex = 0;
            telemetryNext = 0;
            lastRx = mqtt_now_ms();
            lastPing = lastRx;
            continue;
        }

        if (mqtt_base_topic(base, sizeof(base))) {
            mqtt_close_session(&fd, &have, out, MQTT_BUFFER, rxBuffer,
                sizeof(rxBuffer));
            continue;
        }
        snprintf(eventTopic, sizeof(eventTopic), "%s/event", base);
        snprintf(telemetryTopic, sizeof(telemetryTopic), "%s/telemetry", base);

        /* One queued event per iteration keeps the read path responsive. */
        {
            char *json = NULL;

            pthread_mutex_lock(&mqttMtx);
            if (mqttQueueCount) {
                json = mqttQueue[mqttQueueHead];
                mqttQueue[mqttQueueHead] = NULL;
                mqttQueueHead = (mqttQueueHead + 1) % MQTT_QUEUE_MAX;
                mqttQueueCount--;
            }
            pthread_mutex_unlock(&mqttMtx);

            if (json) {
                int result;

                if (packetId == 0) packetId = 1;
                result = mqtt_publish(fd, out, MQTT_BUFFER, rxBuffer,
                    sizeof(rxBuffer), &have, eventTopic, json, strlen(json),
                    1, 0, packetId);
                packetId++;
                if (result) {
                    mqttFailed++;
                    mqtt_set_error("event publish failed");
                    free(json);
                    mqtt_close_session(&fd, &have, out, MQTT_BUFFER, rxBuffer,
                        sizeof(rxBuffer));
                    nextConnect = mqtt_now_ms() + mqttRetryMs[0];
                    continue;
                }
                mqttSent++;
                free(json);
                continue;
            }
        }

        now = mqtt_now_ms();
        if (app_config.mqtt_publish_telemetry &&
            now >= telemetryNext) {
            int length = mqtt_build_telemetry(telemetry, MQTT_BUFFER);
            int result = EXIT_FAILURE;

            if (length > 0)
                result = mqtt_publish(fd, out, MQTT_BUFFER, rxBuffer,
                    sizeof(rxBuffer), &have, telemetryTopic, telemetry,
                    (size_t)length, 0, 1, 0);
            if (result) {
                mqtt_set_error("telemetry publish failed");
                mqtt_close_session(&fd, &have, out, MQTT_BUFFER, rxBuffer,
                    sizeof(rxBuffer));
                nextConnect = mqtt_now_ms() + mqttRetryMs[0];
                continue;
            }
            mqttSent++;
            telemetryNext = mqtt_now_ms() +
                (uint64_t)(app_config.mqtt_telemetry_interval_s ?
                    app_config.mqtt_telemetry_interval_s : 30) * 1000ull;
            continue;
        }

        keepalive = app_config.mqtt_keepalive_s ?
            app_config.mqtt_keepalive_s : 60;
        now = mqtt_now_ms();
        if (now - lastRx > (uint64_t)keepalive * 1500) {
            mqtt_set_error("keepalive timeout");
            mqtt_close_session(&fd, &have, out, MQTT_BUFFER, rxBuffer,
                sizeof(rxBuffer));
            nextConnect = mqtt_now_ms() + mqttRetryMs[0];
            continue;
        }
        if (now - lastPing >= (uint64_t)keepalive * 500) {
            uint8_t ping[4];
            int length = mqtt_encode_pingreq(ping, sizeof(ping));

            if (length > 0 && mqtt_write_all(fd, ping, (size_t)length)) {
                mqtt_set_error("PINGREQ write failed");
                mqtt_close_session(&fd, &have, out, MQTT_BUFFER, rxBuffer,
                    sizeof(rxBuffer));
                nextConnect = mqtt_now_ms() + mqttRetryMs[0];
                continue;
            }
            lastPing = now;
        }

        {
            mqtt_packet packet;
            int result = mqtt_read_packet(fd, &packet, rxBuffer,
                sizeof(rxBuffer), &have, 200);

            if (result < 0) {
                mqtt_set_error("broker disconnected");
                mqtt_close_session(&fd, &have, out, MQTT_BUFFER, rxBuffer,
                    sizeof(rxBuffer));
                nextConnect = mqtt_now_ms() + mqttRetryMs[0];
                continue;
            }
            if (result > 0) lastRx = mqtt_now_ms();
        }
    }

    mqtt_close_session(&fd, &have, out, MQTT_BUFFER, rxBuffer,
        sizeof(rxBuffer));

    pthread_mutex_lock(&mqttMtx);
    for (int i = 0; i < MQTT_QUEUE_MAX; i++) {
        free(mqttQueue[i]);
        mqttQueue[i] = NULL;
    }
    mqttQueueHead = 0;
    mqttQueueCount = 0;
    pthread_mutex_unlock(&mqttMtx);

    free(out);
    free(telemetry);
    return NULL;
}

void mqtt_post_start(void)
{
    pthread_attr_t thread_attr;
    size_t stacksize;

    pthread_attr_init(&thread_attr);
    pthread_attr_getstacksize(&thread_attr, &stacksize);
    mqttRun = 1;
    mqttReload = 0;
    if (mqttStarted) {
        pthread_attr_destroy(&thread_attr);
        return;
    }
    if (pthread_attr_setstacksize(&thread_attr, 32 * 1024))
        HAL_DANGER("mqtt", "Can't set stack size\n");
    if (pthread_create(&mqttPid, &thread_attr, mqtt_thread, NULL))
        HAL_DANGER("mqtt", "Starting the sender thread failed!\n");
    else
        mqttStarted = 1;
    if (pthread_attr_setstacksize(&thread_attr, stacksize))
        HAL_DANGER("mqtt", "Can't restore stack size\n");
    pthread_attr_destroy(&thread_attr);
}

void mqtt_post_stop(void)
{
    mqttRun = 0;
    if (!mqttStarted) return;
    /* The loop wakes at least twice a second and aborts a PUBACK wait when
     * mqttRun drops, so the join finishes quickly. */
    pthread_join(mqttPid, NULL);
    mqttStarted = 0;
    mqttConnected = 0;
}

void mqtt_post_reload(void)
{
    mqttReload = 1;
}

/* ------------------------------------------------------------------ */
/* Status                                                             */

void mqtt_post_status_json(char *out, size_t size)
{
    char id[64], eventTopic[160], telemetryTopic[160], statusTopic[160];
    char error[320];
    int connected, queued;
    unsigned long sent, failed, dropped;

    if (!out || !size) return;
    mqtt_post_client_id(id, sizeof(id));
    mqtt_post_topic(eventTopic, sizeof(eventTopic), "event");
    mqtt_post_topic(telemetryTopic, sizeof(telemetryTopic), "telemetry");
    mqtt_post_topic(statusTopic, sizeof(statusTopic), "status");

    pthread_mutex_lock(&mqttMtx);
    connected = mqttConnected;
    queued = mqttQueueCount;
    sent = mqttSent;
    failed = mqttFailed;
    dropped = mqttDropped;
    mqtt_json_escape(error, sizeof(error), mqttLastError);
    pthread_mutex_unlock(&mqttMtx);

    snprintf(out, size,
        "\"connected\":%s,\"sent\":%lu,\"failed\":%lu,\"queued\":%d,"
        "\"dropped\":%lu,\"last_error\":\"%s\","
        "\"resolved_client_id\":\"%s\",\"event_topic\":\"%s\","
        "\"telemetry_topic\":\"%s\",\"status_topic\":\"%s\"",
        connected ? "true" : "false", sent, failed, queued, dropped, error,
        id, eventTopic, telemetryTopic, statusTopic);
}