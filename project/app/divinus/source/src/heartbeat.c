/* heartbeat.c — pure helpers of the sleep heartbeat snapshot (OKO).
 * See heartbeat.h; no HAL/media/network code belongs here. */
#include "heartbeat.h"

#include <stdio.h>
#include <string.h>

#define HEARTBEAT_SEGMENT "heartbeat"
#define FRAME_SEGMENT "frame"

void heartbeat_derive_url(char *out, size_t size, const char *frame_url)
{
    size_t len;

    if (!out || !size) return;
    out[0] = 0;
    if (!frame_url || !*frame_url) return;

    snprintf(out, size, "%s", frame_url);
    len = strlen(out);

    /* Only a whole trailing segment counts: "/ingest/frame" is the event
     * endpoint, "/ingest/myframe" is not. */
    if (len >= sizeof(FRAME_SEGMENT) - 1 &&
        !strcmp(out + len - (sizeof(FRAME_SEGMENT) - 1), FRAME_SEGMENT) &&
        (len == sizeof(FRAME_SEGMENT) - 1 ||
         out[len - sizeof(FRAME_SEGMENT)] == '/')) {
        size_t prefix = len - (sizeof(FRAME_SEGMENT) - 1);

        if (prefix + sizeof(HEARTBEAT_SEGMENT) > size) {
            out[0] = 0;
            return;
        }
        memcpy(out + prefix, HEARTBEAT_SEGMENT, sizeof(HEARTBEAT_SEGMENT));
        return;
    }

    if (len && out[len - 1] == '/')
        snprintf(out + len, size - len, HEARTBEAT_SEGMENT);
    else
        snprintf(out + len, size - len, "/" HEARTBEAT_SEGMENT);
}

size_t heartbeat_build_headers(char *out, size_t size,
    unsigned long long ts_ms, unsigned int idle_s, int area_permille,
    unsigned int wake_cycle)
{
    int written;

    if (!out || !size) return 0;
    written = snprintf(out, size,
        "X-Heartbeat: 1\r\n"
        "X-Heartbeat-Ts: %llu\r\n"
        "X-OKO-Idle-S: %u\r\n"
        "X-OKO-Motion-Area: %d\r\n"
        "X-OKO-Sleep: quiet\r\n"
        "X-OKO-Wake-Cycle: %u\r\n",
        ts_ms, idle_s, area_permille, wake_cycle);
    if (written < 0) {
        out[0] = 0;
        return 0;
    }
    if ((size_t)written >= size) return size - 1;
    return (size_t)written;
}

int heartbeat_interval_due(long now_s, long gate_s, unsigned int interval_s)
{
    if (interval_s == 0) return 1;
    if (gate_s <= 0) return 1;
    if (now_s <= gate_s) return 0;
    return (now_s - gate_s) >= (long)interval_s;
}

int heartbeat_streak_next(int streak, int ok)
{
    if (ok) return 0;
    if (streak < 0) return 1;
    if (streak >= 1000000) return streak;
    return streak + 1;
}