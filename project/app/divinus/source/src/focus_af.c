#include "focus_af.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "app_config.h"
#include "focus.h"
#include "focus_af_algo.h"
#include "focus_motor.h"

#define AF_MOTOR_FOCUS   0
#define AF_POLL_MS       20
#define AF_FRAME_TRIES   100      /* 2 s at the lowest configured rate   */
#define AF_SAMPLES       3        /* median of three frames              */
#define AF_MOVE_CHUNK    32000

/* Local wait_idle() results. */
#define AF_WAIT_OK       0
#define AF_WAIT_FAULT    1
#define AF_WAIT_TIMEOUT  2
#define AF_WAIT_IO      -1

static pthread_mutex_t af_lock = PTHREAD_MUTEX_INITIALIZER;
static focus_af_status job;
static int busy;
static int job_scope;             /* 0 full search, 1 micro-fine sweep */
static int thread_valid;
static pthread_t worker_thread;
static volatile int cancel_flag;
static long long deadline_ms;

static long long now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static void job_set_error(const char *fmt, ...)
{
    va_list args;

    pthread_mutex_lock(&af_lock);
    snprintf(job.state, sizeof(job.state), "error");
    job.done = 0;
    va_start(args, fmt);
    vsnprintf(job.error, sizeof(job.error), fmt, args);
    va_end(args);
    pthread_mutex_unlock(&af_lock);
}

static void job_set_state(const char *state, int done, int progress)
{
    pthread_mutex_lock(&af_lock);
    snprintf(job.state, sizeof(job.state), "%s", state);
    job.done = done;
    if (progress >= 0)
        job.progress = progress;
    pthread_mutex_unlock(&af_lock);
}

static void job_update(int position, unsigned long long metric,
                       int best_position, unsigned long long best_metric,
                       int progress)
{
    pthread_mutex_lock(&af_lock);
    job.position = position;
    job.metric = metric;
    job.best_position = best_position;
    job.best_metric = best_metric;
    if (progress > 99)
        progress = 99;
    if (progress > job.progress)
        job.progress = progress;
    pthread_mutex_unlock(&af_lock);
}

/* Returns 1 when the worker must stop (cancel or timeout) and sets the final
 * error text. */
static int af_aborted(void)
{
    if (cancel_flag)
    {
        job_set_error("cancelled");
        return 1;
    }
    if (deadline_ms && now_ms() > deadline_ms)
    {
        job_set_error("timed out");
        return 1;
    }
    return 0;
}

/* Sleeps in short slices so Cancel is honoured during the settle time. */
static int af_pause(int ms)
{
    long long until = now_ms() + ms;

    while (now_ms() < until)
    {
        if (af_aborted())
            return -1;
        usleep(AF_POLL_MS * 1000);
    }
    return 0;
}

static int wait_idle(uint8_t motor, int timeout_ms)
{
    int waited = 0;

    while (waited <= timeout_ms)
    {
        uint8_t status = 0xFF;

        if (focus_motor_get_status(motor, &status) != FM_OK)
            return AF_WAIT_IO;
        if (status == 0)
            return AF_WAIT_OK;
        if (status == 2)
            return AF_WAIT_FAULT;
        usleep(AF_POLL_MS * 1000);
        waited += AF_POLL_MS;
        if (af_aborted())
            return AF_WAIT_IO;
    }
    return AF_WAIT_TIMEOUT;
}

/* Reads a position that is valid for the AF search.  A single bus glitch used
   to look like a corrupted scale, so retry briefly: 0 is a valid position,
   anything outside [0, max_pos] is rejected (KI-042).  Returns 0 on success,
   1 when the position is out of range and -1 on an I/O error. */
static int af_read_position(int32_t *position)
{
    int32_t value = 0;
    int last_rc = FM_ERR_IO;
    int tries;

    for (tries = 0; tries < 3; tries++)
    {
        last_rc = focus_motor_get_position(AF_MOTOR_FOCUS, &value);
        if (last_rc == FM_OK && value >= 0 &&
            value <= app_config.focus_max_pos)
        {
            *position = value;
            return 0;
        }
        usleep(10000);
    }
    *position = value;
    return last_rc == FM_OK ? 1 : -1;
}

static int af_move_to(int32_t target)
{
    int32_t current = 0;
    long delta;
    int timeout_ms;
    int rc;

    if (af_read_position(&current) != 0)
    {
        job_set_error("cannot read the focus position");
        return -1;
    }
    delta = (long)target - current;
    if (delta == 0)
        return 0;
    if (delta > AF_MOVE_CHUNK || delta < -AF_MOVE_CHUNK)
    {
        job_set_error("move of %ld steps is too large", delta);
        return -1;
    }
    if (focus_motor_move(AF_MOTOR_FOCUS, (int16_t)delta) != FM_OK)
    {
        job_set_error("MOVE failed");
        return -1;
    }
    timeout_ms = (int)((delta < 0 ? -delta : delta) * 1000 /
                       (int)app_config.focus_speed) + 3000;
    rc = wait_idle(AF_MOTOR_FOCUS, timeout_ms);
    if (rc != AF_WAIT_OK)
    {
        job_set_error("%s", rc == AF_WAIT_FAULT ? "driver fault during the AF move" :
                          (rc == AF_WAIT_TIMEOUT ? "AF move timed out" :
                                                   "I2C error during the AF move"));
        return -1;
    }
    return 0;
}

/* Waits until the statistics thread publishes a frame. */
static int af_wait_stats(int timeout_ms)
{
    int waited = 0;

    while (waited <= timeout_ms)
    {
        unsigned long long avg, smooth;
        unsigned int frame;

        if (focus_metric_get(&avg, &smooth, &frame) == 0)
            return 0;
        if (af_aborted())
            return -1;
        usleep(AF_POLL_MS * 1000);
        waited += AF_POLL_MS;
    }
    job_set_error("no AF statistics; enable Focus assist first");
    return -1;
}

static void median3(unsigned long long a, unsigned long long b,
                    unsigned long long c, unsigned long long *out)
{
    if (a > b)
    {
        unsigned long long t = a;
        a = b;
        b = t;
    }
    if (b > c)
    {
        unsigned long long t = b;
        b = c;
        c = t;
    }
    if (a > b)
    {
        unsigned long long t = a;
        a = b;
        b = t;
    }
    *out = b;
}

/* Settles after a move and takes the median of three fresh frames.  The raw
 * per-frame average is used: metric_smooth is an EMA that lags by several
 * frames, which would compare the new position with the previous one. */
static int af_sample(unsigned long long *metric)
{
    unsigned long long values[AF_SAMPLES];
    unsigned long long avg = 0, smooth = 0;
    unsigned int last = 0;
    int i;

    if (af_pause((int)app_config.af_settle_ms))
        return -1;
    if (focus_metric_get(&avg, &smooth, &last) != 0)
    {
        job_set_error("the AF statistics stopped updating");
        return -1;
    }

    for (i = 0; i < AF_SAMPLES; i++)
    {
        int tries;
        int got = 0;

        for (tries = 0; tries < AF_FRAME_TRIES; tries++)
        {
            unsigned int frame;

            if (focus_metric_get(&avg, &smooth, &frame) != 0)
            {
                job_set_error("the AF statistics stopped updating");
                return -1;
            }
            if (frame != last)
            {
                last = frame;
                got = 1;
                break;
            }
            if (af_aborted())
                return -1;
            usleep(AF_POLL_MS * 1000);
        }
        if (!got)
        {
            job_set_error("the AF statistics stopped updating");
            return -1;
        }
        values[i] = avg;
    }
    median3(values[0], values[1], values[2], metric);
    return 0;
}

static int af_bounds(int *lo, int *hi)
{
    int margin = (int)app_config.focus_home_margin;

    /* KI-042: never command a negative position, even if focus.min_pos is
       negative.  Keep the homing margin off both mechanical stops. */
    *lo = app_config.focus_min_pos < 0 ? 0 : app_config.focus_min_pos;
    *hi = app_config.focus_max_pos;
    if (*hi <= *lo || *hi - *lo < 2 * margin + 2)
        return -1;
    *lo += margin;
    *hi -= margin;
    return 0;
}

/* Samples [bl, bh] on a `fine` grid and keeps the best point in the tracker.
 * The first and last positions are always sampled, even when the interval is
 * not an exact multiple of the step.  Returns 0 or -1 when the job failed and
 * the worker must finish. */
static int af_fine_sweep(int32_t bl, int32_t bh, int fine,
                         af_tracker *tracker, int *points_done,
                         int points_total)
{
    unsigned long long metric;
    int32_t p;

    if (fine < 1)
        fine = 1;
    if (af_move_to(bl) != 0)
        return -1;
    if (af_sample(&metric) != 0)
        return -1;
    (*points_done)++;
    af_tracker_update(tracker, metric, bl);
    job_update(bl, metric, tracker->best_position, tracker->best_metric,
               *points_done * 100 / points_total);

    for (p = bl + fine; p <= bh; p += fine)
    {
        if (af_move_to(p) != 0)
            return -1;
        if (af_sample(&metric) != 0)
            return -1;
        (*points_done)++;
        af_tracker_update(tracker, metric, p);
        job_update(p, metric, tracker->best_position, tracker->best_metric,
                   *points_done * 100 / points_total);
    }
    if ((bh - bl) % fine != 0)
    {
        if (af_move_to(bh) != 0)
            return -1;
        if (af_sample(&metric) != 0)
            return -1;
        (*points_done)++;
        af_tracker_update(tracker, metric, bh);
        job_update(bh, metric, tracker->best_position, tracker->best_metric,
                   *points_done * 100 / points_total);
    }
    return 0;
}

static void *af_worker(void *arg)
{
    int32_t start_pos = 0, scan_pos, current_pos, best_pos;
    unsigned long long start_metric = 0, metric = 0, best_metric = 0;
    unsigned long long plus_metric, minus_metric;
    af_tracker tracker;
    int lo, hi, coarse, fine, patience, probe, dir;
    int points_total, points_done = 0;
    int fine_mode = job_scope;

    (void)arg;
    coarse = (int)app_config.af_coarse;
    fine = (int)app_config.af_fine;
    patience = (int)app_config.af_patience;
    probe = coarse / 2;
    if (probe < 1)
        probe = 1;

    if (af_bounds(&lo, &hi) != 0)
    {
        job_set_error("invalid focus limits");
        goto finished;
    }
    if (af_wait_stats(5000) != 0)
        goto finished;
    {
        int pos_rc = af_read_position(&start_pos);

        if (pos_rc == 1)
        {
            job_set_error("position %ld is outside 0..%d; use Home or Zero "
                          "first", (long)start_pos, app_config.focus_max_pos);
            goto finished;
        }
        if (pos_rc != 0)
        {
            job_set_error("cannot read the focus position");
            goto finished;
        }
    }
    if (focus_motor_set_speed(AF_MOTOR_FOCUS,
                              (uint16_t)app_config.focus_speed) != FM_OK)
    {
        job_set_error("cannot set the focus speed");
        goto finished;
    }

    points_total = 3 + (hi - lo) / coarse + (2 * coarse) / fine;
    job_update(start_pos, 0, start_pos, 0, 0);

    if (af_sample(&start_metric) != 0)
        goto finished;
    points_done++;
    best_pos = start_pos;
    best_metric = start_metric;

    /* Micro-fine mode: no direction probe and no coarse pass, just a local
       sweep around the current position.  It always reports "done": the lens
       is nudged to the best point in the window even when the scene is flat,
       because the caller asked for a small correction, not a full search. */
    if (fine_mode)
    {
        int32_t bl, bh;

        af_window(start_pos, lo, hi, coarse, &bl, &bh);
        points_total = 2 + (int)((bh - bl) / fine);
        af_tracker_reset(&tracker, (unsigned int)patience, start_metric,
                         start_pos);
        if (af_fine_sweep(bl, bh, fine, &tracker, &points_done,
                          points_total) != 0)
            goto finished;
        best_metric = tracker.best_metric;
        best_pos = tracker.best_position;
        if (af_move_to(best_pos) != 0)
            goto finished;
        job_update(best_pos, best_metric, best_pos, best_metric, 100);
        job_set_state("done", 1, 100);
        goto finished;
    }

    if (start_metric == 0)
    {
        job_set_error("the AF metric is zero; point the lens at a scene with "
                      "contrast or use the full frame");
        goto finished;
    }

    /* Direction probe: +probe first; -probe only when +probe did not improve.
       Both values are still passed through the tested decision helper. */
    plus_metric = start_metric;
    minus_metric = start_metric;
    current_pos = start_pos;
    if (start_pos + probe <= hi)
    {
        if (af_move_to(start_pos + probe) != 0)
            goto finished;
        current_pos = start_pos + probe;
        if (af_sample(&plus_metric) != 0)
            goto finished;
        points_done++;
        job_update(current_pos, plus_metric, best_pos, best_metric,
                   points_done * 100 / points_total);
        if (plus_metric > best_metric)
        {
            best_metric = plus_metric;
            best_pos = current_pos;
        }
    }
    if (plus_metric <= start_metric && start_pos - probe >= lo)
    {
        if (current_pos != start_pos)
        {
            if (af_move_to(start_pos) != 0)
                goto finished;
            current_pos = start_pos;
        }
        if (af_move_to(start_pos - probe) != 0)
            goto finished;
        current_pos = start_pos - probe;
        if (af_sample(&minus_metric) != 0)
            goto finished;
        points_done++;
        job_update(current_pos, minus_metric, best_pos, best_metric,
                   points_done * 100 / points_total);
        if (minus_metric > best_metric)
        {
            best_metric = minus_metric;
            best_pos = current_pos;
        }
    }

    dir = af_pick_direction(start_metric, plus_metric, minus_metric);
    if (dir == 0)
    {
        /* Neither probe improved: the start point is a local peak, skip the
           coarse pass and refine around it. */
        best_metric = start_metric;
        best_pos = start_pos;
        if (current_pos != start_pos)
        {
            if (af_move_to(start_pos) != 0)
                goto finished;
            current_pos = start_pos;
        }
    }

    af_tracker_reset(&tracker, (unsigned int)patience, best_metric, best_pos);

    /* Coarse pass. */
    scan_pos = best_pos;
    while (dir != 0)
    {
        int32_t next = scan_pos + dir * coarse;

        if (next > hi)
            next = hi;
        if (next < lo)
            next = lo;
        if (next == scan_pos)
            break;
        if (af_move_to(next) != 0)
            goto finished;
        if (af_sample(&metric) != 0)
            goto finished;
        scan_pos = next;
        points_done++;
        if (af_tracker_update(&tracker, metric, scan_pos))
        {
            best_metric = tracker.best_metric;
            best_pos = tracker.best_position;
            job_update(scan_pos, metric, best_pos, best_metric,
                       points_done * 100 / points_total);
            break;
        }
        best_metric = tracker.best_metric;
        best_pos = tracker.best_position;
        job_update(scan_pos, metric, best_pos, best_metric,
                   points_done * 100 / points_total);
    }

    /* Fine pass between the best coarse neighbours. */
    {
        int32_t bl = best_pos - coarse;
        int32_t bh = best_pos + coarse;

        if (dir == 0)
        {
            bl = start_pos - coarse;
            bh = start_pos + coarse;
        }
        if (bl < lo)
            bl = lo;
        if (bh > hi)
            bh = hi;

        if (af_fine_sweep(bl, bh, fine, &tracker, &points_done,
                          points_total) != 0)
            goto finished;
    }

    best_metric = tracker.best_metric;
    best_pos = tracker.best_position;

    /* A flat scene has no peak: report it instead of pretending success. */
    if (dir != 0 &&
        (best_metric == 0 ||
         (best_metric - start_metric) * 100 <
             start_metric * app_config.af_min_gain_pct))
    {
        job_set_error("peak not found (metric %llu -> %llu at position %ld)",
                      start_metric, best_metric, (long)best_pos);
        goto finished;
    }

    if (af_move_to(best_pos) != 0)
        goto finished;
    job_update(best_pos, best_metric, best_pos, best_metric, 100);
    job_set_state("done", 1, 100);
    goto finished;

finished:
    /* A failed/cancelled search must not leave the lens moving. */
    focus_motor_stop(AF_MOTOR_FOCUS);
    pthread_mutex_lock(&af_lock);
    busy = 0;
    pthread_mutex_unlock(&af_lock);
    return NULL;
}

int focus_af_init(void)
{
    pthread_mutex_lock(&af_lock);
    memset(&job, 0, sizeof(job));
    snprintf(job.state, sizeof(job.state), "idle");
    snprintf(job.scope, sizeof(job.scope), "full");
    busy = 0;
    job_scope = 0;
    cancel_flag = 0;
    deadline_ms = 0;
    pthread_mutex_unlock(&af_lock);
    return 0;
}

void focus_af_shutdown(void)
{
    cancel_flag = 1;
    if (thread_valid)
    {
        pthread_join(worker_thread, NULL);
        thread_valid = 0;
    }
}

int focus_af_running(void)
{
    int value;

    pthread_mutex_lock(&af_lock);
    value = busy;
    pthread_mutex_unlock(&af_lock);
    return value;
}

int focus_af_start(int motor, int scope)
{
    if (motor != AF_MOTOR_FOCUS)
        return -2;

    pthread_mutex_lock(&af_lock);
    if (busy)
    {
        pthread_mutex_unlock(&af_lock);
        return -1;
    }
    busy = 1;
    cancel_flag = 0;
    job_scope = scope ? 1 : 0;
    deadline_ms = now_ms() + (long long)app_config.af_timeout_s * 1000;
    snprintf(job.state, sizeof(job.state), "running");
    snprintf(job.scope, sizeof(job.scope), "%s", job_scope ? "fine" : "full");
    job.position = 0;
    job.best_position = 0;
    job.metric = 0;
    job.best_metric = 0;
    job.progress = 0;
    job.done = 0;
    job.error[0] = '\0';
    pthread_mutex_unlock(&af_lock);

    if (thread_valid)
    {
        pthread_join(worker_thread, NULL);
        thread_valid = 0;
    }
    if (pthread_create(&worker_thread, NULL, af_worker, NULL) != 0)
    {
        job_set_error("cannot start the AF worker");
        pthread_mutex_lock(&af_lock);
        busy = 0;
        pthread_mutex_unlock(&af_lock);
        return -2;
    }
    thread_valid = 1;
    return 0;
}

void focus_af_cancel(void)
{
    cancel_flag = 1;
}

void focus_af_status_get(focus_af_status *out)
{
    if (!out)
        return;
    pthread_mutex_lock(&af_lock);
    *out = job;
    pthread_mutex_unlock(&af_lock);
}