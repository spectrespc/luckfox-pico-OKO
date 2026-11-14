#include "record_clip.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#include "app_config.h"
#include "fmt/mp4_clip.h"
#include "fmt/nal.h"
#include "gating.h"
#include "hal/macros.h"
#include "http_post.h"
#include "media.h"

/* A clip lasts pre+post (15 s by default) while YOLO retriggers every 5 s,
 * so two slots lose roughly every third event when the scene keeps a person
 * or an object in view.  Four slots cover three overlapping clips plus a
 * burst; the arena and the /tmp quota are shared by all slots. */
#define CLIP_JOBS 4
#define CLIP_ENTRIES 2048
#define CLIP_MAX_AU (512 * 1024)
#define CLIP_MIN_ARENA (2u * 1024 * 1024)
#define CLIP_MAX_ARENA (12u * 1024 * 1024)
#define CLIP_OUTBOX_DIR "/mnt/sdcard/oko-outbox"
#define CLIP_OUTBOX_QUOTA (256ull * 1024 * 1024)
#define CLIP_RETRY_COUNT 3
/* The receiver may be back after a long outage; drain several spooled clips
 * per pass instead of one per minute, otherwise a 2-hour backlog would take
 * hours to deliver. */
#define CLIP_OUTBOX_INTERVAL_MS 15000
#define CLIP_OUTBOX_BATCH 8
#define CLIP_OUTBOX_FILES 64

typedef struct {
    unsigned char *data;   /* one malloc per AU: no byte-ring wrap, so an
                            * active job can never lose a held AU */
    uint32_t len;
    uint64_t ts, seq;
    uint8_t key;
} clip_entry;

#define JOB_FREE 0
#define JOB_RECORDING 1
#define JOB_DELIVERING 2
#define JOB_STARTING 3

typedef struct {
    int state;
    char eventId[48];
    char cause[24];
    char model[96];
    char path[128];
    mp4_clip_ctx mux;
    FILE *file;
    uint64_t startMs, endMs, tsMs;
    uint64_t durationMs;
    uint64_t nextSeq;
    unsigned long long bytes;
    int attempts;
    uint64_t deliverAt;
} clip_job;

static const uint64_t clipRetryMs[CLIP_RETRY_COUNT] = {30000, 120000, 600000};

static pthread_mutex_t ringMtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t jobMtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t workerCond = PTHREAD_COND_INITIALIZER;
static pthread_t workerThread;
static volatile int clipRunning, workerRunning;

static size_t ringSize;
static clip_entry ringEntries[CLIP_ENTRIES];
static int ringHead, ringCount;
static unsigned long long ringBytes;
static uint64_t ringSeq;
static uint64_t ringHoldSeq;   /* oldest sequence a job still needs, 0 = none */
static uint64_t ringDropped;
static int tapH265;

static clip_job jobs[CLIP_JOBS];
static unsigned long long clipSent, clipLost, clipFailed, clipSkipped;
static char lastError[160];
static char clipStartReason[160];
static uint64_t outboxNextMs;

static uint64_t clip_now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

/* Called with jobMtx held; the ring lock protects the 64-bit hold value from
 * a torn read in the encoder tap (jobMtx -> ringMtx order is the only one). */
static void clip_outbox_update_hold(void)
{
    uint64_t hold = 0;

    for (int i = 0; i < CLIP_JOBS; i++) {
        if (jobs[i].state == JOB_RECORDING &&
            (!hold || jobs[i].nextSeq < hold))
            hold = jobs[i].nextSeq;
    }
    pthread_mutex_lock(&ringMtx);
    ringHoldSeq = hold;
    pthread_mutex_unlock(&ringMtx);
}

const char *record_clip_unavailable(void)
{
    if (clipStartReason[0]) return clipStartReason;
    if (!app_config.mp4_enable)
        return "the main encoder ([mp4] enable) is off, so there is no "
            "H.265 stream to clip";
    if (channel_by_role(HAL_CHNROLE_MAIN) < 0)
        return "the main encoder channel is not running";
    return NULL;
}

int record_clip_wanted(void)
{
    return clipRunning;
}

/* ------------------------------------------------------------------ */
/* Ring                                                                */

static int clip_scan_key(const unsigned char *au, size_t len, int isH265)
{
    unsigned char *cursor = (unsigned char *)au;
    size_t nalLen = 0;

    while (nal_split((unsigned char *)au, &cursor, &nalLen, len) == 0) {
        unsigned int type;

        if (!nalLen) continue;
        type = isH265 ? ((cursor[0] >> 1) & 0x3F) : (cursor[0] & 0x1F);
        if (isH265 ? (type == 19 || type == 20) : type == 5)
            return 1;
    }
    return 0;
}

static size_t clip_stream_length(hal_vidstream *stream)
{
    size_t total = 0;

    for (unsigned int i = 0; i < stream->count; i++) {
        hal_vidpack *pack = &stream->pack[i];

        if (pack->length <= pack->offset) continue;
        total += pack->length - pack->offset;
    }
    return total;
}

static void clip_ring_push(const unsigned char *data, size_t len, int key)
{
    uint64_t now = clip_now_ms();
    unsigned char *copy;
    clip_entry *entry;

    copy = malloc(len);
    if (!copy) {
        __atomic_add_fetch(&ringDropped, 1, __ATOMIC_RELAXED);
        return;
    }
    memcpy(copy, data, len);

    if (pthread_mutex_trylock(&ringMtx)) {
        free(copy);
        __atomic_add_fetch(&ringDropped, 1, __ATOMIC_RELAXED);
        return;
    }

    while (ringBytes + len > ringSize && ringCount > 0) {
        clip_entry *old = &ringEntries[ringHead];

        if (ringHoldSeq && old->seq >= ringHoldSeq)
            break;                 /* a job still needs this AU */
        ringBytes -= old->len;
        free(old->data);
        old->data = NULL;
        ringHead = (ringHead + 1) % CLIP_ENTRIES;
        ringCount--;
    }
    if (ringBytes + len > ringSize || ringCount >= CLIP_ENTRIES) {
        pthread_mutex_unlock(&ringMtx);
        free(copy);
        __atomic_add_fetch(&ringDropped, 1, __ATOMIC_RELAXED);
        return;
    }

    entry = &ringEntries[(ringHead + ringCount) % CLIP_ENTRIES];
    entry->data = copy;
    entry->len = (uint32_t)len;
    entry->ts = now;
    entry->seq = ringSeq++;
    entry->key = key ? 1 : 0;
    ringBytes += len;
    ringCount++;

    pthread_mutex_unlock(&ringMtx);
    pthread_cond_signal(&workerCond);
}

void record_clip_ingest_stream(hal_vidstream *stream, char isH265)
{
    unsigned char *tmp;
    size_t total, used = 0;
    int key;

    if (!clipRunning || !stream || !stream->count) return;

    total = clip_stream_length(stream);
    if (total < 16 || total > CLIP_MAX_AU) {
        __atomic_add_fetch(&ringDropped, 1, __ATOMIC_RELAXED);
        return;
    }

    tmp = malloc(total);
    if (!tmp) {
        __atomic_add_fetch(&ringDropped, 1, __ATOMIC_RELAXED);
        return;
    }
    for (unsigned int i = 0; i < stream->count; i++) {
        hal_vidpack *pack = &stream->pack[i];
        size_t len;

        if (pack->length <= pack->offset) continue;
        len = pack->length - pack->offset;
        memcpy(tmp + used, pack->data + pack->offset, len);
        used += len;
    }
    key = clip_scan_key(tmp, used, isH265);
    clip_ring_push(tmp, used, key);
    free(tmp);
}

/* Finds the newest keyframe that is not newer than `fromTs`.  Called with
 * ringMtx held. */
static int clip_ring_start_index(uint64_t fromTs)
{
    int index = -1;

    for (int i = 0; i < ringCount; i++) {
        int pos = (ringHead + i) % CLIP_ENTRIES;

        if (ringEntries[pos].ts >= fromTs) {
            index = i;
            break;
        }
    }
    if (index < 0) index = ringCount - 1;
    for (int i = index; i < ringCount; i++)
        if (ringEntries[(ringHead + i) % CLIP_ENTRIES].key) return i;
    for (int i = index; i >= 0; i--)
        if (ringEntries[(ringHead + i) % CLIP_ENTRIES].key) return i;
    return -1;
}

/* ------------------------------------------------------------------ */
/* Jobs                                                                */

static void clip_job_release(int index, int unlinkFile)
{
    clip_job *job = &jobs[index];

    if (job->file) {
        fclose(job->file);
        job->file = NULL;
    }
    if (unlinkFile && job->path[0]) unlink(job->path);
    mp4_clip_free(&job->mux);
    job->state = JOB_FREE;
    job->path[0] = 0;
    job->eventId[0] = 0;
    job->bytes = 0;
}

int record_clip_trigger(const char *eventId, const char *cause,
    const char *model, char *error, size_t errorSize)
{
    clip_job *job = NULL;
    int slot = -1, startIndex;
    uint64_t now;
    const char *why;

    if (!clipRunning) {
        snprintf(error, errorSize, "event clips are disabled");
        return -1;
    }
    why = record_clip_unavailable();
    if (why) {
        snprintf(error, errorSize, "%s", why);
        return -1;
    }
    now = clip_now_ms();

    pthread_mutex_lock(&jobMtx);
    for (int i = 0; i < CLIP_JOBS; i++) {
        if (jobs[i].state == JOB_FREE) {
            slot = i;
            jobs[i].state = JOB_STARTING;   /* reserve against a second event */
            break;
        }
    }
    pthread_mutex_unlock(&jobMtx);
    if (slot < 0) {
        __atomic_add_fetch(&clipSkipped, 1, __ATOMIC_RELAXED);
        snprintf(error, errorSize, "both clip slots are busy");
        return -1;
    }

    pthread_mutex_lock(&ringMtx);
    startIndex = clip_ring_start_index(now > (uint64_t)app_config.event_clip_pre_seconds * 1000 ?
        now - (uint64_t)app_config.event_clip_pre_seconds * 1000 : 0);
    if (startIndex < 0 || ringCount <= 0) {
        pthread_mutex_unlock(&ringMtx);
        pthread_mutex_lock(&jobMtx);
        jobs[slot].state = JOB_FREE;
        pthread_mutex_unlock(&jobMtx);
        snprintf(error, errorSize, "the ring has no keyframe yet");
        return -1;
    }
    job = &jobs[slot];
    job->nextSeq = ringEntries[(ringHead + startIndex) % CLIP_ENTRIES].seq;
    job->tsMs = ringEntries[(ringHead + startIndex) % CLIP_ENTRIES].ts;
    pthread_mutex_unlock(&ringMtx);

    mp4_clip_init(&job->mux, (int)app_config.mp4_width,
        (int)app_config.mp4_height, (int)app_config.mp4_fps,
        app_config.mp4_codecH265);
    snprintf(job->path, sizeof(job->path), "/tmp/oko-clip-%s.mp4", eventId);
    job->file = fopen(job->path, "wb");
    if (!job->file) {
        mp4_clip_free(&job->mux);
        pthread_mutex_lock(&jobMtx);
        job->state = JOB_FREE;
        pthread_mutex_unlock(&jobMtx);
        snprintf(error, errorSize, "cannot create %s", job->path);
        return -1;
    }
    setvbuf(job->file, NULL, _IOFBF, 32 * 1024);

    snprintf(job->eventId, sizeof(job->eventId), "%s", eventId);
    snprintf(job->cause, sizeof(job->cause), "%s", cause ? cause : "event");
    snprintf(job->model, sizeof(job->model), "%s", model ? model : "unknown");
    job->startMs = now;
    job->endMs = now + (uint64_t)app_config.event_clip_post_seconds * 1000;
    job->bytes = 0;
    job->attempts = 0;
    job->deliverAt = 0;
    job->durationMs = 0;

    pthread_mutex_lock(&jobMtx);
    job->state = JOB_RECORDING;
    clip_outbox_update_hold();
    pthread_mutex_unlock(&jobMtx);

    pthread_cond_signal(&workerCond);
    HAL_INFO("clip", "Event %s: clip started (pre %u s, post %u s)\n",
        job->eventId, app_config.event_clip_pre_seconds,
        app_config.event_clip_post_seconds);
    return 0;
}

/* Appends every ring AU at or after job->nextSeq to the clip file.  The ring
 * lock is held only for one memcpy per AU, so the encoder tap is never
 * blocked behind file or muxer work. */
static int clip_job_append(int index)
{
    clip_job *job = &jobs[index];
    unsigned char *scratch;
    int appended = 0;

    scratch = malloc(CLIP_MAX_AU);
    if (!scratch) return -1;

    for (;;) {
        clip_entry entry;
        int found = 0;

        pthread_mutex_lock(&ringMtx);
        for (int i = 0; i < ringCount; i++) {
            clip_entry *e = &ringEntries[(ringHead + i) % CLIP_ENTRIES];

            if (e->seq < job->nextSeq) continue;
            entry = *e;
            memcpy(scratch, entry.data, entry.len);
            found = 1;
            break;
        }
        pthread_mutex_unlock(&ringMtx);

        if (!found) break;

        if (mp4_clip_feed(&job->mux, (const char *)scratch, entry.len,
                entry.ts, entry.key, job->file)) {
            free(scratch);
            return -1;
        }
        job->nextSeq = entry.seq + 1;
        job->bytes += mp4_clip_last_fragment_size(&job->mux);
        appended++;

        if (app_config.event_clip_max_bytes &&
            job->bytes >= app_config.event_clip_max_bytes) {
            HAL_WARNING("clip", "Event %s: clip reached its %llu byte "
                "quota\n", job->eventId, app_config.event_clip_max_bytes);
            break;
        }
    }
    free(scratch);
    return appended;
}

/* Waits until ring head reaches seq, with a time limit.  Releasing the ring
 * lock between entries keeps the encoder tap non-blocking. */
static int clip_job_finish_recording(int index)
{
    clip_job *job = &jobs[index];

    if (job->file) {
        if (fflush(job->file) || fclose(job->file))
            HAL_WARNING("clip", "Event %s: closing the clip failed\n",
                job->eventId);
        job->file = NULL;
    }
    job->durationMs = clip_now_ms() > job->tsMs ?
        clip_now_ms() - job->tsMs : 0;

    pthread_mutex_lock(&jobMtx);
    if (!job->bytes) {
        job->state = JOB_FREE;
        clipLost++;
        snprintf(lastError, sizeof(lastError),
            "event %s produced an empty clip", job->eventId);
        mp4_clip_free(&job->mux);
        unlink(job->path);
        job->path[0] = 0;
        clip_outbox_update_hold();
        pthread_mutex_unlock(&jobMtx);
        HAL_WARNING("clip", "Event %s: no clip data; event lost\n",
            job->eventId);
        return -1;
    }
    job->state = JOB_DELIVERING;
    job->attempts = 0;
    job->deliverAt = clip_now_ms();
    clip_outbox_update_hold();
    pthread_mutex_unlock(&jobMtx);
    HAL_INFO("clip", "Event %s: %llu bytes, %.1f s, delivery queued\n",
        job->eventId, job->bytes, (double)job->durationMs / 1000.0);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Outbox                                                              */

static void clip_outbox_write_meta(const char *dir, const char *name,
    const clip_job *job)
{
    char path[PATH_MAX];
    FILE *file;

    snprintf(path, sizeof(path), "%s/%s.meta", dir, name);
    file = fopen(path, "w");
    if (!file) return;
    fprintf(file, "event=%s\ncause=%s\nmodel=%s\nduration_ms=%llu\nts=%llu\n",
        job->eventId, job->cause, job->model,
        (unsigned long long)job->durationMs, (unsigned long long)job->tsMs);
    fclose(file);
}

static int clip_outbox_copy(const char *src, const char *dir,
    const char *name)
{
    char dst[PATH_MAX];
    FILE *in, *out;
    char buf[16384];
    size_t got;

    snprintf(dst, sizeof(dst), "%s/%s", dir, name);
    in = fopen(src, "rb");
    if (!in) return -1;
    out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return -1;
    }
    while ((got = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, got, out) != got) {
            fclose(in);
            fclose(out);
            unlink(dst);
            return -1;
        }
    }
    fclose(in);
    if (fclose(out)) {
        unlink(dst);
        return -1;
    }
    return 0;
}

typedef struct {
    char name[128];
    unsigned long long size;
    time_t mtime;
} outbox_file;

static int clip_outbox_cmp(const void *a, const void *b)
{
    const outbox_file *fa = a, *fb = b;

    return (fa->mtime > fb->mtime) - (fa->mtime < fb->mtime);
}

/* Fills at most `max` descriptors and sums *every* .mp4 into *total, so the
 * quota and the API counters do not stop at the descriptor limit. */
static int clip_outbox_scan(outbox_file *files, int max,
    unsigned long long *total, unsigned long long *countAll)
{
    DIR *dir;
    struct dirent *entry;
    int count = 0;

    *total = 0;
    if (countAll) *countAll = 0;
    dir = opendir(CLIP_OUTBOX_DIR);
    if (!dir) {
        HAL_WARNING("clip", "Outbox opendir failed: %s\n", strerror(errno));
        return 0;
    }
    while ((entry = readdir(dir))) {
        char path[512];
        struct stat st;
        size_t len = strlen(entry->d_name);

        if (len < 5 || strcmp(entry->d_name + len - 4, ".mp4")) continue;
        snprintf(path, sizeof(path), "%s/%s", CLIP_OUTBOX_DIR, entry->d_name);
        if (stat(path, &st) || !S_ISREG(st.st_mode)) continue;
        *total += (unsigned long long)st.st_size;
        if (countAll) (*countAll)++;
        if (count >= max) continue;
        snprintf(files[count].name, sizeof(files[count].name), "%s",
            entry->d_name);
        files[count].size = (unsigned long long)st.st_size;
        files[count].mtime = st.st_mtime;
        count++;
    }
    closedir(dir);
    return count;
}

static void clip_outbox_trim(unsigned long long incoming)
{
    outbox_file files[CLIP_OUTBOX_FILES];

    /* Re-scan after each descriptor batch: with more than 256 files the
     * first batch may not be enough to get back under the quota. */
    for (int pass = 0; pass < 16; pass++) {
        unsigned long long total;
        int count = clip_outbox_scan(files, CLIP_OUTBOX_FILES, &total, NULL), evicted = 0;

        if (count < 1 || total + incoming <= CLIP_OUTBOX_QUOTA) return;
        qsort(files, count, sizeof(files[0]), clip_outbox_cmp);
        for (int i = 0; i < count && total + incoming > CLIP_OUTBOX_QUOTA;
                i++) {
            char path[PATH_MAX];

            if (strstr(files[i].name, "..")) continue;
            snprintf(path, sizeof(path), "%s/%s", CLIP_OUTBOX_DIR,
                files[i].name);
            if (!unlink(path)) {
                char meta[PATH_MAX];

                snprintf(meta, sizeof(meta), "%s.meta", path);
                unlink(meta);
                total -= files[i].size;
                evicted++;
                HAL_INFO("clip", "Outbox: evicted %s\n", files[i].name);
            }
        }
        if (!evicted) return;
    }
}

static int clip_outbox_store(const clip_job *job)
{
    char name[160];
    struct stat st;

    if (!job->bytes || !job->path[0]) return -1;
    if (stat(job->path, &st) || st.st_size <= 0) return -1;
    if (!gating_sd_mounted()) return -1;
    if (mkdir(CLIP_OUTBOX_DIR, 0755) && errno != EEXIST) return -1;

    snprintf(name, sizeof(name), "clip-%s.mp4", job->eventId);
    clip_outbox_trim(job->bytes);
    if (clip_outbox_copy(job->path, CLIP_OUTBOX_DIR, name)) return -1;
    clip_outbox_write_meta(CLIP_OUTBOX_DIR, name, job);
    return 0;
}

static void clip_meta_get(const char *path, const char *key, char *out,
    size_t size)
{
    FILE *file = fopen(path, "r");
    char line[256];
    size_t keyLen = strlen(key);

    out[0] = 0;
    if (!file) return;
    while (fgets(line, sizeof(line), file)) {
        if (!strncmp(line, key, keyLen) && line[keyLen] == '=') {
            char *value = line + keyLen + 1;
            char *nl = strchr(value, '\n');

            if (nl) *nl = 0;
            snprintf(out, size, "%s", value);
            break;
        }
    }
    fclose(file);
}

static void clip_outbox_drain(void)
{
    outbox_file files[CLIP_OUTBOX_FILES];
    unsigned long long total, all;
    int count, sent = 0;

    if (!gating_sd_mounted() || !app_config.http_post_enable) return;
    count = clip_outbox_scan(files, CLIP_OUTBOX_FILES, &total, &all);
    if (count < 1) return;

    for (int pass = 0; pass < CLIP_OUTBOX_BATCH && pass < count; pass++) {
        int oldest = -1;

        for (int i = 0; i < count; i++) {
            if (!files[i].name[0]) continue;
            if (oldest < 0 || files[i].mtime < files[oldest].mtime)
                oldest = i;
        }
        if (oldest < 0) break;

        {
            char path[PATH_MAX], meta[PATH_MAX];
            struct stat st;
            char eventId[64] = {0}, cause[32] = {0}, model[96] = {0};
            char value[64];
            unsigned long long duration = 0, ts = 0;

            snprintf(path, sizeof(path), "%s/%s", CLIP_OUTBOX_DIR,
                files[oldest].name);
            snprintf(meta, sizeof(meta), "%s.meta", path);
            if (stat(path, &st) || st.st_size <= 0) {
                unlink(path);
                unlink(meta);
                files[oldest].name[0] = 0;
                continue;
            }
            clip_meta_get(meta, "event", eventId, sizeof(eventId));
            clip_meta_get(meta, "cause", cause, sizeof(cause));
            clip_meta_get(meta, "model", model, sizeof(model));
            clip_meta_get(meta, "duration_ms", value, sizeof(value));
            duration = strtoull(value, NULL, 10);
            clip_meta_get(meta, "ts", value, sizeof(value));
            ts = strtoull(value, NULL, 10);

            if (!eventId[0]) snprintf(eventId, sizeof(eventId), "outbox");
            if (!cause[0]) snprintf(cause, sizeof(cause), "outbox");
            if (!model[0]) snprintf(model, sizeof(model), "unknown");

            if (http_post_clip(path, eventId, cause, model, ts, duration) != 0)
                break;            /* receiver still down; keep the rest */
            unlink(path);
            unlink(meta);
            files[oldest].name[0] = 0;
            sent++;
        }
    }
    if (sent)
        HAL_INFO("clip", "Outbox: delivered %d clip(s)\n", sent);
}

/* ------------------------------------------------------------------ */
/* Worker                                                              */

static int clip_deliver(int index)
{
    clip_job *job = &jobs[index];

    if (http_post_clip(job->path, job->eventId, job->cause, job->model,
            job->tsMs, job->durationMs) == 0) {
        unlink(job->path);
        pthread_mutex_lock(&jobMtx);
        clipSent++;
        pthread_mutex_unlock(&jobMtx);
        HAL_INFO("clip", "Event %s: clip delivered\n", job->eventId);
        pthread_mutex_lock(&jobMtx);
        clip_job_release(index, 0);
        clip_outbox_update_hold();
        pthread_mutex_unlock(&jobMtx);
        return 0;
    }

    pthread_mutex_lock(&jobMtx);
    job->attempts++;
    if (job->attempts >= CLIP_RETRY_COUNT) {
        if (clip_outbox_store(job) == 0) {
            HAL_WARNING("clip", "Event %s: delivery failed; spooled to the "
                "microSD outbox\n", job->eventId);
        } else {
            clipLost++;
            snprintf(lastError, sizeof(lastError),
                "event %s was lost after %d delivery attempts",
                job->eventId, job->attempts);
            HAL_WARNING("clip", "Event %s: delivery failed and no microSD "
                "outbox is available; event lost\n", job->eventId);
        }
        clip_job_release(index, 1);
        clip_outbox_update_hold();
        pthread_mutex_unlock(&jobMtx);
        return -1;
    }

    job->deliverAt = clip_now_ms() + clipRetryMs[job->attempts - 1];
    clipFailed++;
    snprintf(lastError, sizeof(lastError),
        "event %s: delivery attempt %d failed", job->eventId, job->attempts);
    pthread_mutex_unlock(&jobMtx);
    HAL_WARNING("clip", "Event %s: delivery attempt %d failed, retrying in "
        "%llu s\n", job->eventId, job->attempts,
        (unsigned long long)(clipRetryMs[job->attempts - 1] / 1000));
    return -1;
}

static void *clip_worker(void *arg)
{
    (void)arg;

    while (workerRunning) {
        struct timespec until;
        uint64_t now;
        int any = 0;

        clock_gettime(CLOCK_REALTIME, &until);
        until.tv_nsec += 100 * 1000 * 1000;
        if (until.tv_nsec >= 1000000000) {
            until.tv_sec++;
            until.tv_nsec -= 1000000000;
        }
        pthread_mutex_lock(&jobMtx);
        if (!any) pthread_cond_timedwait(&workerCond, &jobMtx, &until);
        pthread_mutex_unlock(&jobMtx);

        now = clip_now_ms();
        for (int i = 0; i < CLIP_JOBS && workerRunning; i++) {
            if (jobs[i].state == JOB_RECORDING) {
                clip_job_append(i);
                if (clip_now_ms() >= jobs[i].endMs ||
                    (app_config.event_clip_max_bytes &&
                     jobs[i].bytes >= app_config.event_clip_max_bytes))
                    clip_job_finish_recording(i);
            } else if (jobs[i].state == JOB_DELIVERING &&
                now >= jobs[i].deliverAt) {
                clip_deliver(i);
            }
        }

        if (now >= outboxNextMs) {
            outboxNextMs = now + CLIP_OUTBOX_INTERVAL_MS;
            clip_outbox_drain();
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Lifecycle and status                                                */

/* A SIGKILL or a failed startup leaves /tmp/oko-clip-*.mp4 behind.  The
 * tmpfs is only 90 MiB on the bench, so stale files from previous runs must
 * be removed before a new ring is allocated. */
static void clip_cleanup_stale(void)
{
    DIR *dir = opendir("/tmp");
    struct dirent *entry;
    int removed = 0;

    if (!dir) return;
    while ((entry = readdir(dir))) {
        char path[512];
        size_t len = strlen(entry->d_name);

        if (len < 14 || strncmp(entry->d_name, "oko-clip-", 9) ||
            strcmp(entry->d_name + len - 4, ".mp4"))
            continue;
        snprintf(path, sizeof(path), "/tmp/%s", entry->d_name);
        if (!unlink(path)) removed++;
    }
    closedir(dir);
    if (removed)
        HAL_INFO("clip", "Removed %d stale clip file(s) from /tmp\n", removed);
}

int record_clip_start(void)
{
    size_t window, bytes;
    pthread_attr_t attr;

    if (clipRunning) return 0;
    if (!app_config.event_clip_enable) return -1;
    clipStartReason[0] = 0;
    if (record_clip_unavailable()) return -1;
    clip_cleanup_stale();

    window = (size_t)app_config.event_clip_pre_seconds +
        (size_t)app_config.event_clip_post_seconds + 5;
    bytes = (size_t)((uint64_t)app_config.mp4_bitrate * 1000ull / 8ull *
        (uint64_t)window);
    bytes = bytes * 115 / 100;
    if (bytes < CLIP_MIN_ARENA) bytes = CLIP_MIN_ARENA;
    if (bytes > CLIP_MAX_ARENA) {
        snprintf(clipStartReason, sizeof(clipStartReason),
            "event clips need a %zu KiB ring at %d kbit/s for %u+%u s, "
            "but the %u KiB limit is exceeded; lower pre/post or the "
            "bitrate", bytes / 1024, app_config.mp4_bitrate,
            app_config.event_clip_pre_seconds,
            app_config.event_clip_post_seconds, CLIP_MAX_ARENA / 1024);
        HAL_WARNING("clip", "%s\n", clipStartReason);
        return -1;
    }

    clipStartReason[0] = 0;
    ringSize = bytes;
    ringHead = ringCount = 0;
    ringBytes = 0;
    ringSeq = 1;
    ringHoldSeq = 0;
    ringDropped = 0;
    tapH265 = app_config.mp4_codecH265;
    clipSent = clipLost = clipFailed = clipSkipped = 0;
    lastError[0] = 0;
    outboxNextMs = 0;
    memset(jobs, 0, sizeof(jobs));

    for (int i = 0; i < CLIP_JOBS; i++) jobs[i].state = JOB_FREE;
    workerRunning = 1;
    clipRunning = 1;

    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 256 * 1024);
    if (pthread_create(&workerThread, &attr, clip_worker, NULL)) {
        pthread_attr_destroy(&attr);
        clipRunning = 0;
        workerRunning = 0;
        snprintf(clipStartReason, sizeof(clipStartReason),
            "cannot start the clip worker thread");
        return -1;
    }
    pthread_attr_destroy(&attr);

    HAL_INFO("clip", "Event clips enabled: ring %zu KiB, pre %u s, post %u s, "
        "quota %llu KiB\n", ringSize / 1024, app_config.event_clip_pre_seconds,
        app_config.event_clip_post_seconds,
        app_config.event_clip_max_bytes / 1024);
    return 0;
}

void record_clip_stop(void)
{
    if (!clipRunning) return;

    clipRunning = 0;
    workerRunning = 0;
    pthread_cond_broadcast(&workerCond);
    pthread_join(workerThread, NULL);

    pthread_mutex_lock(&jobMtx);
    for (int i = 0; i < CLIP_JOBS; i++) {
        clip_job *job = &jobs[i];

        if (job->state == JOB_FREE) continue;
        if (job->file) {
            fflush(job->file);
            fclose(job->file);
            job->file = NULL;
        }
        /* A graceful SIGHUP/stop must not lose a clip that would retry:
         * spool it when the card is mounted, otherwise count the loss. */
        if (job->bytes && clip_outbox_store(job) == 0) {
            HAL_INFO("clip", "Event %s: spooled to the outbox on shutdown\n",
                job->eventId);
        } else if (job->bytes) {
            clipLost++;
            HAL_WARNING("clip", "Event %s: lost on shutdown\n", job->eventId);
        }
        clip_job_release(i, 1);
    }
    pthread_mutex_unlock(&jobMtx);

    pthread_mutex_lock(&ringMtx);
    for (int i = 0; i < ringCount; i++) {
        clip_entry *e = &ringEntries[(ringHead + i) % CLIP_ENTRIES];

        free(e->data);
        e->data = NULL;
    }
    ringCount = ringHead = 0;
    ringBytes = 0;
    ringSize = 0;
    pthread_mutex_unlock(&ringMtx);
}

size_t record_clip_status_json(char *out, size_t size)
{
    int recording = 0, delivering = 0;
    outbox_file *files;
    unsigned long long outboxBytes = 0;
    unsigned long long outboxCount = 0;
    char err[sizeof(lastError)];
    unsigned long long sent, lost, failed, dropped;
    uint64_t hold;

    pthread_mutex_lock(&jobMtx);
    for (int i = 0; i < CLIP_JOBS; i++)
        if (jobs[i].state == JOB_RECORDING) recording++;
        else if (jobs[i].state == JOB_DELIVERING) delivering++;
    snprintf(err, sizeof(err), "%s", lastError);
    sent = clipSent;
    lost = clipLost;
    failed = clipFailed;
    pthread_mutex_unlock(&jobMtx);

    pthread_mutex_lock(&ringMtx);
    hold = ringHoldSeq;
    pthread_mutex_unlock(&ringMtx);
    dropped = ringDropped;

    files = malloc(sizeof(*files) * CLIP_OUTBOX_FILES);
    if (files) {
        clip_outbox_scan(files, CLIP_OUTBOX_FILES, &outboxBytes, &outboxCount);
        free(files);
    }

    return (size_t)snprintf(out, size,
        "\"enable\":%s,\"running\":%s,\"available\":%s,\"reason\":\"%s\","
        "\"codec\":\"%s\",\"ring_kib\":%u,\"ring_bytes\":%llu,"
        "\"ring_entries\":%d,\"ring_dropped\":%llu,\"hold\":%llu,"
        "\"recording\":%d,\"delivering\":%d,\"sent\":%llu,\"lost\":%llu,"
        "\"retries_failed\":%llu,\"skipped\":%llu,\"outbox_count\":%llu,"
        "\"outbox_bytes\":%llu,"
        "\"pre_seconds\":%u,\"post_seconds\":%u,\"max_bytes\":%llu,"
        "\"last_error\":\"%s\"",
        app_config.event_clip_enable ? "true" : "false",
        clipRunning ? "true" : "false",
        (app_config.event_clip_enable && record_clip_unavailable() == NULL) ?
            "true" : "false",
        record_clip_unavailable() ? record_clip_unavailable() : "",
        tapH265 ? "H.265" : "H.264",
        (unsigned)(ringSize / 1024), (unsigned long long)ringBytes,
        ringCount, dropped, (unsigned long long)hold, recording, delivering,
        sent, lost, failed, (unsigned long long)clipSkipped, outboxCount,
        outboxBytes,
        app_config.event_clip_pre_seconds, app_config.event_clip_post_seconds,
        app_config.event_clip_max_bytes, err);
}
