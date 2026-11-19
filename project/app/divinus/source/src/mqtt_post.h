#pragma once

#include <stddef.h>

/*
 * MQTT sender: one thread owns the socket and publishes JSON events from a
 * small RAM queue; telemetry is polled by the thread itself.  Callers only
 * enqueue (mutex + strdup), so analytics threads never wait for the network.
 */

void mqtt_post_start(void);
void mqtt_post_stop(void);
/* Drops the current session so the thread reconnects with the configuration
 * that is already in app_config; used by POST /api/mqtt. */
void mqtt_post_reload(void);

/* Non-blocking enqueue of one ready JSON document (one MQTT message). */
int mqtt_post_event_json(const char *json);
int mqtt_post_heartbeat_json(const char *json);

/* Typed builders used by the analytics, clip and heartbeat hooks.  Every
 * function returns EXIT_FAILURE when [mqtt] is disabled or the queue is full
 * for a non-recoverable reason; a full queue drops the oldest message. */
int mqtt_post_motion(unsigned long long tsMs, unsigned int areaPermille,
    const char *cause);
int mqtt_post_event(const char *type, const char *eventId, const char *cause,
    const char *model, unsigned long long tsMs, unsigned int frames);
int mqtt_post_clip(const char *eventId, const char *cause, const char *model,
    unsigned long long tsMs, unsigned long long durationMs,
    unsigned long long bytes);
int mqtt_post_heartbeat(unsigned long long tsMs, unsigned int idleS,
    int motionAreaPermille, int sleep, unsigned int wakeCycle);

/* Status fields for GET/POST /api/mqtt, without the outer braces. */
void mqtt_post_status_json(char *out, size_t size);
/* Resolved client id (configured or derived from the eth0 MAC). */
int mqtt_post_client_id(char *out, size_t size);
/* "oko/<id>/<suffix>" with the configured prefix. */
int mqtt_post_topic(char *out, size_t size, const char *suffix);