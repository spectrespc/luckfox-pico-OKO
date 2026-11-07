#include "focus.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "app_config.h"
#include "focus_af.h"
#include "focus_geom.h"
#include "focus_home.h"
#include "focus_motor.h"
#include "hal/globals.h"
#include "hal/plus/rk_af_stats.h"

enum {
    FOCUS_STATUS_IDLE = 0,
    FOCUS_STATUS_RUNNING,
    FOCUS_STATUS_NO_STATS,
    FOCUS_STATUS_UNSUPPORTED,
    FOCUS_STATUS_ERROR
};

static struct {
    pthread_mutex_t lock;
    pthread_t thread;
    int running;
    int status;
    int mode;                       /* 0 full, 1 roi */
    unsigned int roi_x, roi_y, roi_w, roi_h;
    unsigned int fps;
    int pending;
    unsigned int frame_id;
    int af_hw_ver;
    uint64_t metric;
    uint64_t metric_avg;
    uint64_t metric_smooth;
    int cells_total;
    int cells_used;
    int highlit_cells;
    rk_af_map map;
} fs = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .status = FOCUS_STATUS_IDLE,
    .mode = 0,
    .roi_w = FOCUS_BP_FULL,
    .roi_h = FOCUS_BP_FULL,
    .fps = 5,
    .cells_total = RK_AF_CELLS
};

static void focus_frame_size(int *width, int *height)
{
    int w = app_config.mp4_width > app_config.mjpeg_width ?
        (int)app_config.mp4_width : (int)app_config.mjpeg_width;
    int h = app_config.mp4_height > app_config.mjpeg_height ?
        (int)app_config.mp4_height : (int)app_config.mjpeg_height;

    if (w <= 0) w = 1920;
    if (h <= 0) h = 1080;
    *width = w;
    *height = h;
}

static const char *focus_status_name(int status)
{
    switch (status)
    {
        case FOCUS_STATUS_RUNNING:     return "running";
        case FOCUS_STATUS_NO_STATS:    return "no_stats";
        case FOCUS_STATUS_UNSUPPORTED: return "unsupported";
        case FOCUS_STATUS_ERROR:       return "error";
        default:                       return "idle";
    }
}

static int focus_apply_window(int mode, unsigned int rx, unsigned int ry,
                              unsigned int rw, unsigned int rh)
{
    int imgw, imgh, px, py, pw, ph;
    int result;

    focus_frame_size(&imgw, &imgh);
    if (mode)
    {
        int bx = (int)rx, by = (int)ry, bw = (int)rw, bh = (int)rh;
        focus_geom_bp_to_px(bx, by, bw, bh, imgw, imgh, &px, &py, &pw, &ph);
    }
    else
    {
        px = 0;
        py = 0;
        pw = imgw;
        ph = imgh;
    }

    result = rk_af_stats_set_window((unsigned)px, (unsigned)py,
        (unsigned)pw, (unsigned)ph);
    return result;
}

/* The AFM statistics map on this RV1106 build always covers the full frame,
 * so the ROI is applied by selecting the cells whose centre lies inside it. */
static int focus_cell_in_roi(int index, unsigned rx, unsigned ry,
                             unsigned rw, unsigned rh)
{
    int col = index % RK_AF_GRID;
    int row = index / RK_AF_GRID;
    unsigned cx = (unsigned)(((2 * col + 1) * FOCUS_BP_FULL) /
                             (2 * RK_AF_GRID));
    unsigned cy = (unsigned)(((2 * row + 1) * FOCUS_BP_FULL) /
                             (2 * RK_AF_GRID));

    return cx >= rx && cx < rx + rw && cy >= ry && cy < ry + rh;
}

static int focus_aggregate(const rk_af_map *map, int mode,
                           unsigned rx, unsigned ry,
                           unsigned rw, unsigned rh)
{
    uint64_t total = 0;
    int used = 0, highlit = 0;
    int i;

    for (i = 0; i < RK_AF_CELLS; i++)
    {
        uint64_t cell = (uint64_t)map->fv_h1[i] + map->fv_h2[i] +
                        map->fv_v1[i] + map->fv_v2[i];

        if (mode && !focus_cell_in_roi(i, rx, ry, rw, rh))
            continue;

        if (map->highlit[i] > 0)
        {
            highlit++;
            if (app_config.focus_skip_highlit)
                continue;
        }
        if (map->luma[i] == 0)
            continue;
        total += cell;
        used++;
    }

    pthread_mutex_lock(&fs.lock);
    fs.frame_id = map->frame_id;
    fs.af_hw_ver = map->af_hw_ver;
    fs.metric = total;
    fs.cells_total = RK_AF_CELLS;
    fs.cells_used = used;
    fs.highlit_cells = highlit;
    if (used > 0)
    {
        uint64_t avg = total / (uint64_t)used;
        fs.metric_avg = avg;
        if (fs.metric_smooth == 0)
            fs.metric_smooth = avg;
        else
            fs.metric_smooth = (fs.metric_smooth * 7 + avg) / 8;
        fs.status = FOCUS_STATUS_RUNNING;
    }
    else
    {
        /* A frame with no valid cells must not leave a stale average. */
        fs.metric_avg = 0;
        fs.metric_smooth = 0;
    }
    memcpy(&fs.map, map, sizeof(fs.map));
    pthread_mutex_unlock(&fs.lock);
    return used;
}

static void *focus_thread(void *arg)
{
    rk_af_map map;
    int no_stats = 0;
    int contrast_tried = 0;
    int window_pending = 1;
    int apply_failures = 0;
    int mode;
    unsigned int rx, ry, rw, rh, fps;

    (void)arg;

    while (1)
    {
        int apply;
        int ret;

        pthread_mutex_lock(&fs.lock);
        if (!fs.running)
        {
            pthread_mutex_unlock(&fs.lock);
            break;
        }
        apply = fs.pending || window_pending;
        fs.pending = 0;
        window_pending = 0;
        mode = fs.mode;
        rx = fs.roi_x;
        ry = fs.roi_y;
        rw = fs.roi_w;
        rh = fs.roi_h;
        fps = fs.fps;
        pthread_mutex_unlock(&fs.lock);

        if (apply)
        {
            ret = focus_apply_window(mode, rx, ry, rw, rh);
            if (ret == -2)
            {
                /* AIQ is not ready yet: retry the window on the next pass. */
                window_pending = 1;
                usleep(200000);
                continue;
            }
            if (ret == -1)
            {
                /* SetAttrib failed: retry a few times, then report an error. */
                if (++apply_failures < 5)
                {
                    window_pending = 1;
                    usleep(200000);
                    continue;
                }
                apply_failures = 0;
                pthread_mutex_lock(&fs.lock);
                fs.status = FOCUS_STATUS_ERROR;
                pthread_mutex_unlock(&fs.lock);
                usleep(1000000);
                continue;
            }
            if (ret == -3)
            {
                /* The AFM hardware is not V31: retrying cannot help. */
                pthread_mutex_lock(&fs.lock);
                fs.status = FOCUS_STATUS_UNSUPPORTED;
                pthread_mutex_unlock(&fs.lock);
                usleep(1000000);
                continue;
            }
            apply_failures = 0;
        }

        {
            int used = 0;

            ret = rk_af_stats_read(&map, 500);
            if (ret == 0)
                used = focus_aggregate(&map, mode, rx, ry, rw, rh);
            else if (ret == -3)
            {
                pthread_mutex_lock(&fs.lock);
                fs.status = FOCUS_STATUS_UNSUPPORTED;
                pthread_mutex_unlock(&fs.lock);
                usleep(1000000);
                continue;
            }
            if (ret == 0 && used > 0)
                no_stats = 0;
            else
            {
                no_stats++;
                /* Decision gate A6 fallback: try the IQ contrast_af flag once
                 * before declaring the AFM path unusable. */
                if (!contrast_tried && no_stats >= 20)
                {
                    contrast_tried = 1;
                    rk_af_stats_force_contrast();
                    no_stats = 0;
                }
                if (no_stats >= 50)
                {
                    pthread_mutex_lock(&fs.lock);
                    fs.status = FOCUS_STATUS_NO_STATS;
                    fs.metric_avg = 0;
                    fs.metric_smooth = 0;
                    fs.cells_used = 0;
                    pthread_mutex_unlock(&fs.lock);
                    no_stats = 0;
                    usleep(1000000);
                    continue;
                }
            }
        }

        if (fps < 1) fps = 1;
        if (fps > 10) fps = 10;
        usleep(1000000u / fps);
    }
    return NULL;
}

int focus_available(void)
{
    return (halCapabilities & HAL_CAP_FOCUS_STATS) != 0;
}

int focus_running(void)
{
    int running;
    pthread_mutex_lock(&fs.lock);
    running = fs.running;
    pthread_mutex_unlock(&fs.lock);
    return running;
}

int focus_start(void)
{
    pthread_attr_t attr;
    int ret;

    if (!focus_available())
        return -1;
    if (focus_running())
        return 0;

    {
        int bx = (int)app_config.focus_roi_x;
        int by = (int)app_config.focus_roi_y;
        int bw = (int)app_config.focus_roi_w;
        int bh = (int)app_config.focus_roi_h;

        focus_geom_clamp(&bx, &by, &bw, &bh);
        pthread_mutex_lock(&fs.lock);
        fs.mode = (int)app_config.focus_mode;
        fs.roi_x = (unsigned int)bx;
        fs.roi_y = (unsigned int)by;
        fs.roi_w = (unsigned int)bw;
        fs.roi_h = (unsigned int)bh;
    }
    fs.fps = app_config.focus_fps;
    fs.status = FOCUS_STATUS_IDLE;
    fs.metric = fs.metric_avg = fs.metric_smooth = 0;
    fs.cells_used = 0;
    fs.pending = 1;
    fs.running = 1;
    pthread_mutex_unlock(&fs.lock);

    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 64 * 1024);
    ret = pthread_create(&fs.thread, &attr, focus_thread, NULL);
    pthread_attr_destroy(&attr);
    if (ret)
    {
        pthread_mutex_lock(&fs.lock);
        fs.running = 0;
        fs.status = FOCUS_STATUS_ERROR;
        pthread_mutex_unlock(&fs.lock);
        return -1;
    }
    return 0;
}

void focus_stop(void)
{
    if (!focus_running())
        return;

    pthread_mutex_lock(&fs.lock);
    fs.running = 0;
    pthread_mutex_unlock(&fs.lock);

    pthread_join(fs.thread, NULL);
    pthread_mutex_lock(&fs.lock);
    fs.status = FOCUS_STATUS_IDLE;
    fs.pending = 0;
    pthread_mutex_unlock(&fs.lock);
}

void focus_configure(int mode, unsigned int x, unsigned int y,
                     unsigned int w, unsigned int h, unsigned int fps)
{
    int bx = (int)x, by = (int)y, bw = (int)w, bh = (int)h;

    focus_geom_clamp(&bx, &by, &bw, &bh);
    if (fps < 1) fps = 1;
    if (fps > 10) fps = 10;

    pthread_mutex_lock(&fs.lock);
    fs.mode = mode ? 1 : 0;
    fs.roi_x = (unsigned int)bx;
    fs.roi_y = (unsigned int)by;
    fs.roi_w = (unsigned int)bw;
    fs.roi_h = (unsigned int)bh;
    fs.fps = fps;
    fs.pending = 1;
    pthread_mutex_unlock(&fs.lock);

    app_config.focus_mode = (unsigned int)(mode ? 1 : 0);
    app_config.focus_roi_x = (unsigned int)bx;
    app_config.focus_roi_y = (unsigned int)by;
    app_config.focus_roi_w = (unsigned int)bw;
    app_config.focus_roi_h = (unsigned int)bh;
    app_config.focus_fps = fps;
}

void focus_get_config(int *mode, unsigned int *x, unsigned int *y,
                      unsigned int *w, unsigned int *h, unsigned int *fps)
{
    pthread_mutex_lock(&fs.lock);
    if (mode) *mode = fs.mode;
    if (x) *x = fs.roi_x;
    if (y) *y = fs.roi_y;
    if (w) *w = fs.roi_w;
    if (h) *h = fs.roi_h;
    if (fps) *fps = fs.fps;
    pthread_mutex_unlock(&fs.lock);
}

int focus_metric_get(unsigned long long *metric_avg,
                     unsigned long long *metric_smooth,
                     unsigned int *frame_id)
{
    int available;

    pthread_mutex_lock(&fs.lock);
    if (metric_avg) *metric_avg = fs.metric_avg;
    if (metric_smooth) *metric_smooth = fs.metric_smooth;
    if (frame_id) *frame_id = fs.frame_id;
    available = fs.running && fs.status == FOCUS_STATUS_RUNNING &&
        fs.frame_id != 0;
    pthread_mutex_unlock(&fs.lock);
    return available ? 0 : -1;
}

static int append(char **p, size_t *left, const char *fmt, ...)
{
    va_list args;
    int n;

    va_start(args, fmt);
    n = vsnprintf(*p, *left, fmt, args);
    va_end(args);
    if (n < 0 || (size_t)n >= *left)
        return -1;
    *p += n;
    *left -= (size_t)n;
    return 0;
}

static int append_array(char **p, size_t *left, const char *name,
                        const uint32_t *values, int count, int last)
{
    int i;

    if (append(p, left, "\"%s\":[", name))
        return -1;
    for (i = 0; i < count; i++)
        if (append(p, left, "%s%u", i ? "," : "", (unsigned)values[i]))
            return -1;
    return append(p, left, last ? "]" : "],");
}

static const char *motor_state_name(int status)
{
    switch (status)
    {
        case 0: return "idle";
        case 1: return "running";
        case 2: return "error";
        default: return "unknown";
    }
}

char *focus_json(int with_map)
{
    char *buffer = malloc(24576);
    char *p = buffer;
    size_t left = 24576;
    int mode, running, status;
    unsigned int x, y, w, h, fps;
    uint64_t metric, avg, smooth;
    int cells_used, highlit, af_hw_ver, pending;
    unsigned int frame_id;
    rk_af_map *map = NULL;
    fm_snapshot motor;
    focus_home_status home;
    focus_af_status af;

    if (!buffer)
        return NULL;
    if (with_map)
    {
        map = malloc(sizeof(*map));
        if (!map)
        {
            free(buffer);
            return NULL;
        }
    }

    pthread_mutex_lock(&fs.lock);
    running = fs.running;
    status = fs.status;
    mode = fs.mode;
    x = fs.roi_x;
    y = fs.roi_y;
    w = fs.roi_w;
    h = fs.roi_h;
    fps = fs.fps;
    frame_id = fs.frame_id;
    af_hw_ver = fs.af_hw_ver;
    metric = fs.metric;
    avg = fs.metric_avg;
    smooth = fs.metric_smooth;
    cells_used = fs.cells_used;
    highlit = fs.highlit_cells;
    pending = fs.pending;
    if (map)
        memcpy(map, &fs.map, sizeof(*map));
    pthread_mutex_unlock(&fs.lock);

    /* Refresh the cached motor scales for the WebUI poll.  The probe is cheap
       (a few I2C reads), keeps the last snapshot on a bus error and never
       blocks the caller when another process owns the bus.  It also runs when
       the driver is not armed yet: the WebUI needs "present" to offer the
       controls that arm it on the first command. */
    if (!focus_motor_suspended() && !focus_home_running() &&
        !focus_af_running())
        focus_motor_probe();

    focus_motor_snapshot(&motor);
    focus_home_status_get(&home);
    focus_af_status_get(&af);

    if (append(&p, &left,
        "{\"enable\":%s,\"available\":%s,\"status\":\"%s\","
        "\"mode\":\"%s\",\"roi\":{\"x\":%u,\"y\":%u,\"w\":%u,\"h\":%u},"
        "\"pending\":%s,\"fps\":%u,\"frame_id\":%u,"
        "\"metric\":%llu,\"metric_avg\":%llu,\"metric_smooth\":%llu,"
        "\"cells_total\":%d,\"cells_used\":%d,\"highlit_cells\":%d,"
        "\"af_hw_ver\":%d,\"motor\":{",
        app_config.focus_enable ? "true" : "false",
        focus_available() ? "true" : "false",
        focus_status_name(status), mode ? "roi" : "full",
        x, y, w, h,
        (running && pending) ? "true" : "false",
        fps, frame_id,
        (unsigned long long)metric, (unsigned long long)avg,
        (unsigned long long)smooth, RK_AF_CELLS, cells_used, highlit,
        af_hw_ver))
        goto fail;

    if (append(&p, &left,
        "\"enabled\":%s,\"present\":%s,\"device\":\"%s\",\"address\":%u,"
        "\"bootloader\":%s,\"status\":\"%s\"",
        motor.enabled ? "true" : "false",
        motor.present ? "true" : "false",
        app_config.focus_device, app_config.focus_address,
        motor.bootloader ? "true" : "false",
        !motor.present ? "absent" : (motor.bootloader ? "boot" : "app")))
        goto fail;

    if (motor.has_app_version)
    {
        if (append(&p, &left, ",\"version\":\"%u.%u.%u\"",
            motor.app_version[0], motor.app_version[1],
            motor.app_version[2]))
            goto fail;
    }
    else if (append(&p, &left, ",\"version\":null"))
        goto fail;

    if (motor.has_boot_version)
    {
        if (append(&p, &left, ",\"boot_version\":\"%u.%u.%u\"",
            motor.boot_version[0], motor.boot_version[1],
            motor.boot_version[2]))
            goto fail;
    }
    else if (append(&p, &left, ",\"boot_version\":null"))
        goto fail;

    if (motor.has_position)
    {
        if (append(&p, &left, ",\"position\":%ld", (long)motor.position))
            goto fail;
    }
    else if (append(&p, &left, ",\"position\":null"))
        goto fail;

    /* Per-motor scales for the manual controls.  The legacy top-level
       "position"/"status" above stay for older WebUI builds. */
    if (append(&p, &left, ",\"focus\":{\"position\":"))
        goto fail;
    if (motor.has_position)
    {
        if (append(&p, &left, "%ld", (long)motor.position))
            goto fail;
    }
    else if (append(&p, &left, "null"))
        goto fail;
    if (append(&p, &left, ",\"status\":\"%s\",\"speed\":%u}",
        motor_state_name(motor.status), app_config.focus_speed))
        goto fail;

    if (append(&p, &left, ",\"zoom\":{\"position\":"))
        goto fail;
    if (motor.has_zoom_position)
    {
        if (append(&p, &left, "%ld", (long)motor.zoom_position))
            goto fail;
    }
    else if (append(&p, &left, "null"))
        goto fail;
    if (append(&p, &left, ",\"status\":\"%s\",\"speed\":%u}",
        motor_state_name(motor.zoom_status), app_config.focus_zoom_speed))
        goto fail;

    if (append(&p, &left, "}"))
        goto fail;

    if (append(&p, &left,
        ",\"home\":{\"state\":\"%s\",\"motor\":\"%s\",\"travel\":%d,"
        "\"done\":%s,\"error\":\"%s\"}",
        home.state, home.motor, home.travel,
        home.done ? "true" : "false", home.error))
        goto fail;

    if (append(&p, &left,
        ",\"af\":{\"state\":\"%s\",\"position\":%d,\"best_position\":%d,"
        "\"metric\":%llu,\"best_metric\":%llu,\"progress\":%d,"
        "\"done\":%s,\"error\":\"%s\"}",
        af.state, af.position, af.best_position,
        af.metric, af.best_metric, af.progress,
        af.done ? "true" : "false", af.error))
        goto fail;

    if (with_map)
    {
        if (append(&p, &left, ",\"map\":{"))
            goto fail;
        if (append_array(&p, &left, "fv_h1", map->fv_h1, RK_AF_CELLS, 0) ||
            append_array(&p, &left, "fv_h2", map->fv_h2, RK_AF_CELLS, 0) ||
            append_array(&p, &left, "fv_v1", map->fv_v1, RK_AF_CELLS, 0) ||
            append_array(&p, &left, "fv_v2", map->fv_v2, RK_AF_CELLS, 0) ||
            append_array(&p, &left, "luma", map->luma, RK_AF_CELLS, 0) ||
            append_array(&p, &left, "highlit", map->highlit, RK_AF_CELLS, 1))
            goto fail;
        if (append(&p, &left, "}"))
            goto fail;
    }

    if (append(&p, &left, "}"))
        goto fail;
    free(map);
    return buffer;

fail:
    free(map);
    free(buffer);
    return NULL;
}