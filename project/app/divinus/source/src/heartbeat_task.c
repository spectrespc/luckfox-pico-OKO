/* heartbeat_task.c — runtime side of the sleep heartbeat snapshot (OKO).
 *
 * One JPEG per quiet wake cycle proves that the camera is alive and that the
 * scene has no motion: it is taken at the end of wake_check_s, right before
 * the pipeline is torn down and the AP suspends again.  The receiver sees a
 * gap in the heartbeat stream when the camera dies.
 *
 * The interval gate, the last successful send and the failure streak are
 * persisted in /tmp (tmpfs survives the exec restart of the wake path but not
 * a power cycle).  Failure never postpones the suspend: a dead network must
 * not drain the battery.
 */
#include "heartbeat_task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "app_config.h"
#include "hal/macros.h"
#include "heartbeat.h"
#include "http_post.h"
#include "ivs_events.h"
#include "jpeg.h"
#include "sleep_policy.h"

#define HEARTBEAT_STATE_PATH "/tmp/oko-heartbeat-state"

static int wake_start;          /* this process was started by a wake probe */
static unsigned int wake_cycle; /* cycles since boot, survives exec */
static long gate_s;             /* previous send or gate rebase (monotonic) */
static long last_ok_s;          /* last successful send (monotonic), 0 = none */
static int fail_streak;
static long init_s;             /* process start: idle base for the header */

static long mono_s(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec;
}

static unsigned long long realtime_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_REALTIME, &ts);
    return (unsigned long long)ts.tv_sec * 1000 +
        (unsigned long long)(ts.tv_nsec / 1000000);
}

/* "" when a heartbeat may be sent; otherwise the reason shown by the API and
 * the init log. */
static const char *gate_reason(void)
{
    if (!app_config.sleep_heartbeat) return "disabled";
    if (!app_config.sleep_enable) return "sleep_disabled";
    if (!app_config.http_post_enable || !app_config.http_post_host[0])
        return "no_receiver";
    return "";
}

static int gate_ready(void)
{
    return gate_reason()[0] == 0;
}

static void state_load(void)
{
    FILE *file = fopen(HEARTBEAT_STATE_PATH, "r");
    unsigned int cycle = 0;
    long gate = 0, last_ok = 0;
    int streak = 0;

    if (!file) return;
    if (fscanf(file, "%u %ld %ld %d", &cycle, &gate, &last_ok, &streak) == 4) {
        wake_cycle = cycle;
        gate_s = gate;
        last_ok_s = last_ok;
        fail_streak = streak < 0 ? 0 : streak;
    }
    fclose(file);
}

static void state_save(void)
{
    FILE *file = fopen(HEARTBEAT_STATE_PATH, "w");

    if (!file) return;
    fprintf(file, "%u %ld %ld %d\n", wake_cycle, gate_s, last_ok_s,
        fail_streak);
    fclose(file);
}

void heartbeat_init(void)
{
    const char *wake = getenv("OKO_SLEEP_WAKE");
    int started = (wake && *wake) ||
        access(SLEEP_PROBE_MARKER_PATH, F_OK) == 0;

    state_load();
    init_s = mono_s();
    wake_start = started;
    if (started) {
        wake_cycle++;
        state_save();
    }

    if (!app_config.sleep_heartbeat) return;
    if (gate_ready()) {
        HAL_INFO("heartbeat", "armed: interval %u s, retries %u, wake cycle %u\n",
            app_config.sleep_heartbeat_interval_s,
            app_config.sleep_heartbeat_retries, wake_cycle);
    } else {
        HAL_WARNING("heartbeat", "requested but ignored: %s\n",
            gate_reason());
    }
}

int heartbeat_ready(void)
{
    return gate_ready();
}

int heartbeat_wake_start(void)
{
    return wake_start;
}

unsigned int heartbeat_wake_cycle(void)
{
    return wake_cycle;
}

int heartbeat_due(void)
{
    if (!gate_ready()) return 0;
    return heartbeat_interval_due(mono_s(), gate_s,
        app_config.sleep_heartbeat_interval_s);
}

void heartbeat_set_due(void)
{
    /* A confirmed motion means an event frame was (or will be) delivered.
     * Rebase the interval so the first quiet cycle afterwards does not send a
     * heartbeat that is only seconds younger than the last one.  The write to
     * the state file is deferred to the next send: accepted IVS results arrive
     * several times a second. */
    long now;

    if (!app_config.sleep_heartbeat) return;
    now = mono_s();
    if (now != gate_s) gate_s = now;
}

int heartbeat_before_suspend(void)
{
    hal_jpegdata jpeg = {0};
    char headers[320];
    unsigned int attempts, i;
    long now;
    int sent;

    if (!gate_ready()) return -1;
    if (!heartbeat_due()) {
        HAL_INFO("heartbeat", "interval gate: cycle skipped\n");
        return -1;
    }

    if (jpeg_get((short)app_config.http_post_width,
            (short)app_config.http_post_height,
            (char)app_config.http_post_qfactor, 3, &jpeg)) {
        HAL_WARNING("heartbeat", "snapshot failed; sleeping without it\n");
        gate_s = mono_s();
        fail_streak = heartbeat_streak_next(fail_streak, 0);
        state_save();
        return -1;
    }

    heartbeat_build_headers(headers, sizeof(headers), realtime_ms(),
        (unsigned int)(mono_s() - init_s), ivs_last_area_permille(),
        wake_cycle);

    attempts = app_config.sleep_heartbeat_retries;
    if (attempts == 0) attempts = 1;
    sent = 0;
    for (i = 0; i < attempts; i++) {
        if (http_post_heartbeat(&jpeg, headers) == EXIT_SUCCESS) {
            sent = 1;
            break;
        }
        if (i + 1 < attempts) sleep(1);
    }

    free(jpeg.data);

    now = mono_s();
    gate_s = now;
    if (sent) {
        last_ok_s = now;
        fail_streak = 0;
        HAL_INFO("heartbeat", "sent (wake cycle %u, %u attempt(s))\n",
            wake_cycle, i + 1);
    } else {
        fail_streak = heartbeat_streak_next(fail_streak, 0);
        HAL_WARNING("heartbeat", "send failed after %u attempt(s), %d in a "
            "row; sleeping anyway\n", attempts, fail_streak);
    }
    state_save();
    return sent ? 0 : -1;
}

void heartbeat_status_json(char *out, size_t size)
{
    const char *reason = gate_reason();
    long now = mono_s();
    long age = last_ok_s > 0 && now >= last_ok_s ? now - last_ok_s : -1;

    if (!out || !size) return;
    snprintf(out, size,
        "{\"enable\":%s,\"ready\":%s,\"last_sent_s\":%ld,"
        "\"fail_streak\":%d,\"wake_cycle\":%u,\"interval_s\":%u,"
        "\"retries\":%u,\"reason\":\"%s\"}",
        app_config.sleep_heartbeat ? "true" : "false",
        reason[0] ? "false" : "true", age, fail_streak, wake_cycle,
        app_config.sleep_heartbeat_interval_s,
        app_config.sleep_heartbeat_retries, reason);
}

void heartbeat_status_fields(char *out, size_t size)
{
    const char *reason = gate_reason();

    if (!out || !size) return;
    snprintf(out, size, "\"heartbeat_ok\":%s,\"heartbeat_fail_streak\":%d",
        reason[0] ? "false" : "true", fail_streak);
}