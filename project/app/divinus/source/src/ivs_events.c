#include "ivs_events.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "app_config.h"
#include "hal/globals.h"
#include "hal/support.h"
#include "http_post.h"
#include "jpeg.h"
#include "record.h"

#define IVS_EVENT_RING 16
#define IVS_TRIGGER_COOLDOWN 5

typedef struct {
    time_t when;
    unsigned int frameId;
    unsigned int rectCount;
    unsigned short x, y, w, h;
} ivs_event;

static pthread_t ivsThread;
static volatile int ivsRunning;
static int ivsThreadStarted;
static pthread_mutex_t ivsMtx = PTHREAD_MUTEX_INITIALIZER;
static ivs_event ivsEvents[IVS_EVENT_RING];
static unsigned int ivsEventCount;
static time_t ivsLastTrigger;

static time_t ivsLastShown;

static void ivs_osd_indicator(int active, time_t when)
{
    FILE *file = fopen("/tmp/osd-ivs.txt", "w");

    if (!file) return;
    if (active) {
        struct tm tm_buf, *tm_info = localtime(&when);
        fprintf(file, "MOTION %02d:%02d:%02d\n",
            tm_info ? tm_info->tm_hour : 0,
            tm_info ? tm_info->tm_min : 0,
            tm_info ? tm_info->tm_sec : 0);
        ivsLastShown = when;
    }
    fclose(file);
    chmod("/tmp/osd-ivs.txt", 0644);
}

static void ivs_trigger_actions(time_t when)
{
    if (when - ivsLastTrigger < IVS_TRIGGER_COOLDOWN) return;
    ivsLastTrigger = when;

    if (app_config.ivs_act_osd)
        ivs_osd_indicator(1, when);

    if (app_config.ivs_act_http_post && *app_config.http_post_host) {
        hal_jpegdata jpeg = {0};
        if (!jpeg_get(app_config.jpeg_width, app_config.jpeg_height,
                app_config.jpeg_qfactor, 0, &jpeg)) {
            http_post_send(&jpeg);
            free(jpeg.data);
        } else {
            HAL_WARNING("ivs", "Snapshot for the event failed\n");
        }
    }

    if (app_config.ivs_act_record)
        record_start_event(app_config.ivs_record_seconds,
            app_config.ivs_record_sub);
}

static void ivs_record_event(time_t when, const hal_ivs_result *result)
{
    pthread_mutex_lock(&ivsMtx);
    ivs_event *event = &ivsEvents[ivsEventCount % IVS_EVENT_RING];
    memset(event, 0, sizeof(*event));
    event->when = when;
    event->frameId = result->frameId;
    event->rectCount = result->rectCount;
    if (result->rectCount) {
        event->x = result->rect[0].x;
        event->y = result->rect[0].y;
        event->w = result->rect[0].width;
        event->h = result->rect[0].height;
    }
    ivsEventCount++;
    pthread_mutex_unlock(&ivsMtx);

    HAL_INFO("ivs", "Motion event: %u rect(s)\n", result->rectCount);
    ivs_trigger_actions(when);
}

static void *ivs_thread(void *arg)
{
    hal_ivs_config config;

    (void)arg;
    memset(&config, 0, sizeof(config));
    config.mode = (hal_ivs_mode)app_config.ivs_mode;
    config.width = app_config.ivs_width;
    config.height = app_config.ivs_height;
    config.sensitivity = app_config.ivs_sensitivity;
    config.mdInterval = app_config.ivs_md_interval;
    config.odInterval = app_config.ivs_od_interval;
    config.odPercent = app_config.ivs_od_percent;

#if defined(__ARM_PCS_VFP)
    if (plat == HAL_PLATFORM_RK && rk_ivs_create(&config)) {
        HAL_WARNING("ivs", "Could not start analytics\n");
        return NULL;
    }
#else
    (void)config;
    HAL_WARNING("ivs", "Analytics is only implemented for RK\n");
    return NULL;
#endif

    /* ivsRunning is cleared by ivs_events_stop() before the join, so the thread
     * leaves even when the configuration still says enable. */
    while (keepRunning && ivsRunning) {
        hal_ivs_result result;
        time_t now = time(NULL);

        if (app_config.ivs_act_osd && ivsLastShown &&
            now - ivsLastShown > 10) {
            ivs_osd_indicator(0, now);
            ivsLastShown = 0;
        }

#if defined(__ARM_PCS_VFP)
        if (!rk_ivs_get_results(&result, 500)) {
            ivs_record_event(now, &result);
            rk_ivs_release_results();
        }
#endif
    }

#if defined(__ARM_PCS_VFP)
    if (plat == HAL_PLATFORM_RK) rk_ivs_destroy();
#endif
    if (app_config.ivs_act_osd) ivs_osd_indicator(0, time(NULL));
    return NULL;
}

void ivs_events_start(void)
{
    pthread_attr_t attr;

    if (ivsThreadStarted) return;
    ivsRunning = 1;
    pthread_attr_init(&attr);
    /* librockit's IVS path is stack-hungry (result buffers and vendor
     * wrappers); 32 KiB overflowed and took the process down. */
    pthread_attr_setstacksize(&attr, 256 * 1024);
    if (pthread_create(&ivsThread, &attr, ivs_thread, NULL)) {
        HAL_DANGER("ivs", "Starting the analytics thread failed!\n");
        ivsRunning = 0;
    } else {
        ivsThreadStarted = 1;
    }
    pthread_attr_destroy(&attr);
}

void ivs_events_stop(void)
{
    if (!ivsThreadStarted) {
        ivsRunning = 0;
        return;
    }
    ivsRunning = 0;
    /* The polling call has a 500 ms timeout; the loop exits afterwards. */
    pthread_join(ivsThread, NULL);
    ivsThreadStarted = 0;
}

size_t ivs_events_json(char *out, size_t size)
{
    size_t used = 0;
    unsigned int first = 0;

    if (!out || !size) return 0;
    out[0] = 0;

    pthread_mutex_lock(&ivsMtx);
    unsigned int total = ivsEventCount;
    if (total > IVS_EVENT_RING) first = total - IVS_EVENT_RING;

    used += snprintf(out + used, size > used ? size - used : 0, "[");
    for (unsigned int i = first; i < total && used + 96 < size; i++) {
        ivs_event *event = &ivsEvents[i % IVS_EVENT_RING];
        char stamp[32] = "";
        struct tm tm_buf, *tm_info = localtime_r(&event->when, &tm_buf);
        if (tm_info)
            strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", tm_info);

        used += snprintf(out + used, size - used,
            "%s{\"time\":\"%s\",\"frame\":%u,\"rects\":%u,"
            "\"bbox\":[%u,%u,%u,%u]}",
            i == first ? "" : ",", stamp, event->frameId, event->rectCount,
            event->x, event->y, event->w, event->h);
    }
    pthread_mutex_unlock(&ivsMtx);

    if (used + 2 < size) used += snprintf(out + used, size - used, "]");
    return used;
}