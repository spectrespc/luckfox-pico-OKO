#include "ivs_events.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "app_config.h"
#include "analytics/box_draw.h"
#include "analytics/nn_geom.h"
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
    unsigned int area;              /* motion area, permille of the frame */
    char cause[4];                  /* "md" or "od" */
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

static void ivs_record_event(time_t when, const hal_ivs_result *result,
    unsigned int area, const char *cause)
{
    pthread_mutex_lock(&ivsMtx);
    ivs_event *event = &ivsEvents[ivsEventCount % IVS_EVENT_RING];
    memset(event, 0, sizeof(*event));
    event->when = when;
    event->frameId = result->frameId;
    event->rectCount = result->rectCount;
    event->area = area;
    snprintf(event->cause, sizeof(event->cause), "%s", cause);
    if (result->rectCount) {
        event->x = result->rect[0].x;
        event->y = result->rect[0].y;
        event->w = result->rect[0].width;
        event->h = result->rect[0].height;
    }
    ivsEventCount++;
    pthread_mutex_unlock(&ivsMtx);

    /* Both numbers are logged so a field report can validate the metric even
     * when the vendor never fills md.u32Square. */
    HAL_INFO("ivs", "Motion event: %u rect(s), cause %s, area %u permille, "
        "mdSquare %u of %ux%u\n", result->rectCount, cause, area,
        result->mdSquare, result->width, result->height);
    ivs_trigger_actions(when);
}

/* Motion area of a result as a share of the analytic frame in permille.
 * The vendor reports it in pixels of the IVS frame; when librockit leaves
 * u32Square at zero, fall back to the largest MD rectangle (the HAL mirrors
 * only the first 8 rectangles). */
static unsigned int ivs_area_permille(const hal_ivs_result *result)
{
    unsigned long long square = result->mdSquare;
    unsigned int frameW = result->width ? result->width : app_config.ivs_width;
    unsigned int frameH = result->height ? result->height : app_config.ivs_height;
    unsigned long long frame = (unsigned long long)frameW * frameH;

    if (!square) {
        for (unsigned int i = 0; i < result->rectCount; i++) {
            unsigned long long rect =
                (unsigned long long)result->rect[i].width *
                result->rect[i].height;
            if (rect > square) square = rect;
        }
    }
    if (!frame) return 0;
    if (square > frame) square = frame;
    return (unsigned int)(1000ull * square / frame);
}

/* -------------------------------------------------------------------------
 * IVS rectangles on the main stream
 *
 * The rectangles arrive in pixels of the analytical frame (ivs.width x
 * ivs.height); they are normalized to 0..10000 and mapped through the same
 * ePTZ/rotation geometry the detector boxes use.  The region has its own RGN
 * handle and palette so it can be toggled without touching the YOLO overlay.
 * ---------------------------------------------------------------------- */

#if defined(__ARM_PCS_VFP)
static volatile int boxesWanted;
static int boxesReady;
static int boxesWarned;
static int boxesDrawn;
static int boxesWidth, boxesHeight;
static unsigned long long boxesRetryMs;

static unsigned long long ivs_now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000ull +
        (unsigned long long)ts.tv_nsec / 1000000ull;
}

/* The rectangles cover the encoded main stream.  A 90/270 rotation of the main
 * stream swaps the encoded picture size, like the detector overlay. */
static void ivs_boxes_geometry(int *width, int *height)
{
    int w = (int)app_config.mp4_width;
    int h = (int)app_config.mp4_height;

    if (app_config.rotate_enable && !app_config.rotate_stream &&
        (app_config.rotate_angle == 90 || app_config.rotate_angle == 270)) {
        int swap = w;

        w = h;
        h = swap;
    }
    *width = w;
    *height = h;
}

static void ivs_boxes_mapping(nn_geom *g)
{
    memset(g, 0, sizeof(*g));
    g->src_w = app_config.mp4_width ? (int)app_config.mp4_width : 1920;
    g->src_h = app_config.mp4_height ? (int)app_config.mp4_height : 1080;

    if (app_config.eptz_enable && !app_config.eptz_stream) {
        g->crop_enable = 1;
        g->cx = app_config.eptz_x;
        g->cy = app_config.eptz_y;
        g->cw = app_config.eptz_w;
        g->ch = app_config.eptz_h;
    }
    if (app_config.rotate_enable && !app_config.rotate_stream)
        g->angle = app_config.rotate_angle;
}

static void ivs_boxes_sync(void)
{
    int width, height;

    if (!boxesWanted || !boxesReady) return;

    ivs_boxes_geometry(&width, &height);
    if (width == boxesWidth && height == boxesHeight) return;

    /* The stream was recreated with another size; the canvas no longer
     * matches and has to be rebuilt. */
    rk_region_ivs_destroy();
    boxesReady = 0;
    boxesDrawn = 0;
}

/* Creates the region lazily on the first result: the IVS pipeline only emits
 * results when motion/OD fires, so an idle camera with the switch on does not
 * reserve the extra 2BPP canvas. */
static void ivs_boxes_ensure(void)
{
    int width, height;

    if (!boxesWanted || boxesReady) return;
    if (!rk_region_ivs_supported()) {
        /* One warning is emitted by the HAL; the setting is kept so a restart
         * with a fixed library enables the boxes again. */
        HAL_WARNING("ivs", "Motion boxes unsupported; switch cleared\n");
        boxesWanted = 0;
        return;
    }
    if (ivs_now_ms() < boxesRetryMs) return;

    ivs_boxes_geometry(&width, &height);
    if (width < 16 || height < 16) return;

    if (rk_region_ivs_create(width, height, RK_RGN_IVS_LAYER,
            RK_RGN_IVS_LUT_MOTION, RK_RGN_IVS_LUT_OD)) {
        boxesRetryMs = ivs_now_ms() + 10000;
        if (!boxesWarned) {
            HAL_WARNING("ivs", "Motion boxes could not be enabled\n");
            boxesWarned = 1;
        }
        return;
    }

    boxesReady = 1;
    boxesWidth = width;
    boxesHeight = height;
    boxesDrawn = 0;
}

/* Aligns a mapped rectangle to the 2BPP line granularity and clamps it to the
 * canvas.  Mirrors the detector overlay so both regions behave the same. */
static int ivs_boxes_fit(int *x, int *y, int *w, int *h)
{
    *x &= ~3;
    *y &= ~3;
    *w = (*w + 3) & ~3;
    *h = (*h + 3) & ~3;
    if (*x < 0) *x = 0;
    if (*y < 0) *y = 0;
    if (*x >= boxesWidth || *y >= boxesHeight) return 0;
    /* The right edge byte sits just past the rectangle, so leave room for
     * it (the vendor requires x + w + line < width). */
    if (*x + *w + 4 > boxesWidth) *w = (boxesWidth - *x - 4) & ~3;
    if (*y + *h > boxesHeight) *h = (boxesHeight - *y) & ~3;
    return *w >= 4 && *h >= 4;
}

static void ivs_boxes_render(const hal_ivs_result *result)
{
    void *addr;
    unsigned int stride, canvasW, canvasH;
    nn_geom geom;
    static int commitWarned;
    int drawn = 0;

    if (!boxesReady) return;

    /* Shared with the detector overlay: the vendor RGN calls are not
     * reentrant, so the lock spans begin..paint..commit. */
    rk_region_overlay_lock();
    if (rk_region_ivs_begin(&addr, &stride, &canvasW, &canvasH)) {
        rk_region_overlay_unlock();
        /* The canvas no longer matches (stream recreated): rebuild next tick. */
        rk_region_ivs_destroy();
        boxesReady = 0;
        boxesDrawn = 0;
        return;
    }

    /* Always clear the full canvas: without it the last rectangles stay
     * visible after the motion stops. */
    memset(addr, 0, (size_t)stride * canvasH / 4);
    ivs_boxes_mapping(&geom);

    for (unsigned int i = 0; i < result->rectCount; i++) {
        const hal_ivs_rect *r = &result->rect[i];
        int nx, ny, nw, nh, x, y, w, h;

        box_normalize_rect(r->x, r->y, r->width, r->height,
            result->width ? (int)result->width : (int)app_config.ivs_width,
            result->height ? (int)result->height : (int)app_config.ivs_height,
            &nx, &ny, &nw, &nh);
        if (nw <= 0 || nh <= 0) continue;
        if (!nn_map_rect(&geom, nx, ny, nw, nh, &x, &y, &w, &h)) continue;
        if (!ivs_boxes_fit(&x, &y, &w, &h)) continue;

        box_draw_rect_2bpp(addr, stride, canvasH, x, y, w, h, 2, 0);
        drawn++;
    }

    /* The vendor OD API has no object rectangle, only a flag; on a pure
     * OD hit draw a fixed marker in the centre instead of nothing. */
    if (result->rectCount == 0) {
        int x, y, w, h;

        if (nn_map_rect(&geom, 4000, 4000, 2000, 2000, &x, &y, &w, &h) &&
            ivs_boxes_fit(&x, &y, &w, &h)) {
            box_draw_rect_2bpp(addr, stride, canvasH, x, y, w, h, 3, 1);
            drawn++;
        }
    }

    {
        int failed = rk_region_ivs_commit();

        rk_region_overlay_unlock();
        if (failed && !commitWarned) {
            HAL_WARNING("ivs", "Updating the motion-box canvas failed\n");
            commitWarned = 1;
        }
    }
    boxesDrawn = drawn > 0 ? 1 : 0;
}

/* Clears the canvas once after the last result so the rectangles do not stay
 * frozen on the video until the next motion event. */
static void ivs_boxes_clear(void)
{
    void *addr;
    unsigned int stride, canvasW, canvasH;

    if (!boxesReady || !boxesDrawn) return;

    rk_region_overlay_lock();
    if (rk_region_ivs_begin(&addr, &stride, &canvasW, &canvasH)) {
        rk_region_overlay_unlock();
        rk_region_ivs_destroy();
        boxesReady = 0;
        boxesDrawn = 0;
        return;
    }
    memset(addr, 0, (size_t)stride * canvasH / 4);
    rk_region_ivs_commit();
    rk_region_overlay_unlock();
    boxesDrawn = 0;
}
#endif /* __ARM_PCS_VFP */

void ivs_boxes_request(int enable)
{
#if defined(__ARM_PCS_VFP)
    boxesWanted = enable ? 1 : 0;
    if (!enable) boxesRetryMs = 0;
    HAL_INFO("ivs", "Motion boxes requested: %s\n", enable ? "on" : "off");
#else
    (void)enable;
#endif
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
    /* KI-063: the 2BPP overlay is created once per analytics run and only
     * destroyed on shutdown, so a live switch cannot rebuild it under the
     * running encoder. */
    ivs_boxes_request(app_config.ivs_boxes);
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
        /* The switch may change while the thread runs; the region is torn
         * down here instead of waiting for the next motion event. */
        if (!boxesWanted && boxesReady) {
            rk_region_ivs_destroy();
            boxesReady = 0;
            boxesDrawn = 0;
        }

        if (!rk_ivs_get_results(&result, 500)) {
            unsigned int area = ivs_area_permille(&result);
            const char *cause = result.rectCount > 0 ? "md" : "od";
            /* min_area is a percentage, area is permille.  OD hits are never
             * hidden by the threshold: they have no rectangle to measure. */
            int below = app_config.ivs_min_area > 0 &&
                area < app_config.ivs_min_area * 10;

            if (below && result.odFlag) cause = "od";
            if (below && !result.odFlag) {
                /* Noise-sized motion: no journal entry and no actions. */
                ivs_boxes_clear();
            } else {
                if (boxesWanted) {
                    ivs_boxes_sync();
                    ivs_boxes_ensure();
                    ivs_boxes_render(&result);
                }
                ivs_record_event(now, &result, area, cause);
            }
            rk_ivs_release_results();
        } else {
            ivs_boxes_clear();
        }
#endif
    }

#if defined(__ARM_PCS_VFP)
    if (plat == HAL_PLATFORM_RK) {
        rk_region_ivs_destroy();
        boxesReady = 0;
        boxesDrawn = 0;
        rk_ivs_destroy();
    }
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
#if defined(__ARM_PCS_VFP)
    /* The overlay has to be detached and destroyed before sdk_stop() tears the
     * MPP channels down: a 2BPP canvas still attached to VENC when the process
     * exits makes the kernel oops in dma_buf_release (KI-063 history). */
    if (boxesReady) {
        rk_region_ivs_destroy();
        boxesReady = 0;
        boxesDrawn = 0;
    }
#endif
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
    for (unsigned int i = first; i < total && used + 128 < size; i++) {
        ivs_event *event = &ivsEvents[i % IVS_EVENT_RING];
        char stamp[32] = "";
        struct tm tm_buf, *tm_info = localtime_r(&event->when, &tm_buf);
        if (tm_info)
            strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", tm_info);

        used += snprintf(out + used, size - used,
            "%s{\"time\":\"%s\",\"frame\":%u,\"rects\":%u,"
            "\"area\":%u,\"cause\":\"%s\","
            "\"bbox\":[%u,%u,%u,%u]}",
            i == first ? "" : ",", stamp, event->frameId, event->rectCount,
            event->area, event->cause[0] ? event->cause : "md",
            event->x, event->y, event->w, event->h);
    }
    pthread_mutex_unlock(&ivsMtx);

    if (used + 2 < size) used += snprintf(out + used, size - used, "]");
    return used;
}