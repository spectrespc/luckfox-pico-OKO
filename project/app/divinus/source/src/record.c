#include "record.h"

#include <dirent.h>
#include <limits.h>
#include <strings.h>
#include <sys/statvfs.h>

#include "gating.h"

/* Guards the recording state: the encoder thread feeds fragments while the
 * server thread starts and stops sessions. */
static pthread_mutex_t recordMtx = PTHREAD_MUTEX_INITIALIZER;

volatile int storageBusy;

static FILE *recordFile;
static struct Mp4State recordState;
static unsigned long long recordSize;
static time_t recordStartTime;
static time_t recordLastFlush;
static bool recordActive;
static bool recordEventActive;
static bool recordEventSub;
static unsigned int recordEventGeneration;
static char recordPath[RECORD_PATH_MAX];

#define RECORD_MAX_SEGMENTS 512

typedef struct {
    char name[256];
    unsigned long long size;
    time_t mtime;
} record_seg;

static int record_seg_cmp(const void *a, const void *b)
{
    const record_seg *ra = a, *rb = b;
    return (ra->mtime > rb->mtime) - (ra->mtime < rb->mtime);
}

/* Ring quota: keep the directory within record.max_bytes and free at least
 * record.min_free_mb, deleting the oldest segments first.  Called with
 * recordMtx held. */
static void record_prune_locked(void)
{
    DIR *dir;
    struct dirent *entry;
    record_seg *segs = NULL;
    int count = 0, capacity = 0;
    unsigned long long total = 0;
    unsigned long long freeBytes = ULLONG_MAX;
    struct statvfs vfs;

    if (app_config.record_max_bytes == 0 && app_config.record_min_free_mb == 0)
        return;
    if (statvfs(app_config.record_path, &vfs) == 0)
        freeBytes = (unsigned long long)vfs.f_bavail * vfs.f_frsize;

    dir = opendir(app_config.record_path);
    if (!dir) return;

    while ((entry = readdir(dir))) {
        char path[RECORD_PATH_MAX + 256];
        struct stat st;
        size_t len = strlen(entry->d_name);

        if (entry->d_name[0] == '.') continue;
        if (len < 4 || strcasecmp(entry->d_name + len - 4, ".mp4")) continue;

        snprintf(path, sizeof(path), "%s/%s", app_config.record_path, entry->d_name);
        if (stat(path, &st) || !S_ISREG(st.st_mode)) continue;

        if (count == capacity) {
            int next = capacity ? capacity * 2 : 32;
            record_seg *grown;
            if (next > RECORD_MAX_SEGMENTS) next = RECORD_MAX_SEGMENTS;
            if (next == capacity) break;
            grown = realloc(segs, sizeof(*segs) * next);
            if (!grown) break;
            segs = grown;
            capacity = next;
        }
        snprintf(segs[count].name, sizeof(segs[count].name), "%s", entry->d_name);
        segs[count].size = st.st_size;
        segs[count].mtime = st.st_mtime;
        total += st.st_size;
        count++;
    }
    closedir(dir);

    if (count < 1) { free(segs); return; }

    qsort(segs, count, sizeof(*segs), record_seg_cmp);
    for (int i = 0; i < count; i++) {
        char path[RECORD_PATH_MAX + 256];
        bool overQuota = app_config.record_max_bytes &&
            total > app_config.record_max_bytes;
        bool lowSpace = app_config.record_min_free_mb &&
            freeBytes / (1024 * 1024) < app_config.record_min_free_mb;

        if (!overQuota && !lowSpace) break;

        snprintf(path, sizeof(path), "%s/%s", app_config.record_path, segs[i].name);
        if (recordFile && !strcmp(path, recordPath)) continue;
        if (!unlink(path)) {
            HAL_INFO("record", "Removed old segment '%s' (%llu bytes)\n",
                path, segs[i].size);
            if (total >= segs[i].size) total -= segs[i].size;
            freeBytes += segs[i].size;
        }
    }

    free(segs);
}

static bool record_ensure_dir(const char *path) {
    char tmp[RECORD_PATH_MAX];
    size_t len = strlen(path);

    if (len == 0 || len >= sizeof(tmp)) return false;
    memcpy(tmp, path, len + 1);
    if (tmp[len - 1] == '/') tmp[len - 1] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(tmp, 0755) && errno != EEXIST) {
            HAL_DANGER("record", "Cannot create directory '%s': %s\n", tmp, strerror(errno));
            return false;
        }
        *p = '/';
    }

    if (mkdir(tmp, 0755) && errno != EEXIST) {
        HAL_DANGER("record", "Cannot create directory '%s': %s\n", tmp, strerror(errno));
        return false;
    }
    return true;
}

/* Builds "<path>/<stem>_<timestamp><ext>", appending a suffix on collision. */
static bool record_build_path(char *out, size_t outSize) {
    char dir[sizeof(app_config.record_path)];
    char stem[sizeof(app_config.record_filename)];
    char ext[16] = ".mp4";
    char stamp[32];
    struct tm tm_buf, *tm_info;
    time_t now = recordStartTime ? recordStartTime : time(NULL);

    strncpy(dir, app_config.record_path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    if (EMPTY(dir)) {
        HAL_DANGER("record", "Destination path is not set!\n");
        return false;
    }

    if (!EMPTY(app_config.record_filename)) {
        char *dot;
        const char *name = strrchr(app_config.record_filename, '/');
        name = name ? name + 1 : app_config.record_filename;

        strncpy(stem, name, sizeof(stem) - 1);
        stem[sizeof(stem) - 1] = '\0';

        dot = strrchr(stem, '.');
        if (dot && dot != stem) {
            strncpy(ext, dot, sizeof(ext) - 1);
            ext[sizeof(ext) - 1] = '\0';
            *dot = '\0';
        }
    } else
        strncpy(stem, "recording", sizeof(stem) - 1);

    if (EMPTY(stem)) strncpy(stem, "recording", sizeof(stem) - 1);

    tm_info = localtime_r(&now, &tm_buf);
    if (!tm_info || !strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", tm_info))
        strncpy(stamp, "unknown", sizeof(stamp) - 1);

    snprintf(out, outSize, "%s/%s_%s%s", dir, stem, stamp, ext);
    if (access(out, F_OK)) return true;

    for (unsigned int i = 1; i < 1000; i++) {
        snprintf(out, outSize, "%s/%s_%s-%u%s", dir, stem, stamp, i, ext);
        if (access(out, F_OK)) return true;
    }

    HAL_DANGER("record", "Could not find a free name for a '%s' segment!\n", stem);
    return false;
}

static void record_close_locked(void) {
    if (!recordFile) return;

    if (fflush(recordFile))
        HAL_DANGER("record", "Flushing '%s' failed: %s\n", recordPath, strerror(errno));
    /* A card pulled during recording must not lose the last segment. */
    if (fsync(fileno(recordFile)))
        HAL_DANGER("record", "Fsync of '%s' failed: %s\n", recordPath, strerror(errno));
    if (fclose(recordFile))
        HAL_DANGER("record", "Closing '%s' failed: %s\n", recordPath, strerror(errno));
    else
        HAL_INFO("record", "Finalized segment '%s' (%llu bytes)\n", recordPath, recordSize);

    recordFile = NULL;
    recordSize = 0;
    recordState.header_sent = false;
}

/* True only for paths below the real microSD mount point.  A bare prefix
 * check is not enough: /mnt/sdcard-backup must be rejected as well. */
static bool record_path_is_sdcard(const char *path) {
    static const char base[] = "/mnt/sdcard";
    size_t len = sizeof(base) - 1;

    if (!path || strstr(path, "..") || strncmp(path, base, len))
        return false;
    return path[len] == '/' || path[len] == '\0';
}

static bool record_open_locked(void) {
    if (recordFile) return true;

    if (EMPTY(app_config.record_path)) {
        HAL_DANGER("record", "Destination path is not set!\n");
        return false;
    }
    /* Never write to SPI NAND.  Only /mnt/sdcard is allowed, and only while a
     * card is really mounted there (the web server maps /oem/usr/www/sdcard
     * to /mnt/sdcard, so a path alone does not prove that a card exists).
     * storageBusy closes the race with /api/storage?action=format. */
    if (storageBusy) {
        HAL_DANGER("record", "Refusing to record while the microSD is busy!\n");
        return false;
    }
    if (!record_path_is_sdcard(app_config.record_path) || !gating_sd_mounted()) {
        HAL_DANGER("record", "Refusing to record to '%s': "
            "write only to a mounted microSD at /mnt/sdcard!\n",
            app_config.record_path);
        return false;
    }
    if (!record_ensure_dir(app_config.record_path)) return false;
    record_prune_locked();
    if (!record_build_path(recordPath, sizeof(recordPath))) return false;

    if (!(recordFile = fopen(recordPath, "wb"))) {
        HAL_DANGER("record", "Failed to open '%s': %s\n", recordPath, strerror(errno));
        return false;
    }
    setvbuf(recordFile, NULL, _IOFBF, 64 * 1024);

    recordSize = 0;
    recordState.header_sent = false;
    recordState.sequence_number = 0;
    recordState.base_data_offset = 0;
    recordState.base_media_decode_time = 0;
    recordState.nals_count = 0;
    recordState.default_sample_duration = default_sample_size;
    recordLastFlush = recordStartTime;

    HAL_INFO("record", "Recording to '%s'\n", recordPath);
    return true;
}

static bool record_rotate_locked(void) {
    if (!gating_sd_mounted()) {
        HAL_DANGER("record", "microSD removed; stopping recording '%s'\n", recordPath);
        record_close_locked();
        recordActive = false;
        recordStartTime = 0;
        return false;
    }
    record_close_locked();
    recordStartTime = time(NULL);
    return record_open_locked();
}

static bool record_write_locked(const void *data, size_t len) {
    if (len && fwrite(data, 1, len, recordFile) != len) {
        HAL_DANGER("record", "Writing to '%s' failed: %s\n", recordPath, strerror(errno));
        return false;
    }
    return true;
}

/* Feeds one Annex B NAL payload to the MP4 muxer.  The H.265 branch is
 * explicit because the vendor pack metadata exposes the whole access unit as
 * a single NAL for HEVC (see rk_video_thread), so the caller splits it first. */
static void record_feed_payload(const char *payload, unsigned int payload_len,
    unsigned int type, char isH265) {
    if (!payload || !payload_len) return;

    if (isH265) {
        switch (type) {
            case NalUnitType_VPS_HEVC:
                mp4_set_vps(payload, payload_len);
                break;
            case NalUnitType_SPS_HEVC:
                mp4_set_sps(payload, payload_len, 1);
                break;
            case NalUnitType_PPS_HEVC:
                mp4_set_pps(payload, payload_len, 1);
                break;
            default:
                if (type == 19 || type == 20)       /* IDR_W_RADL / IDR_N_LP */
                    mp4_set_slice(payload, payload_len, 1);
                else if (type <= 9)                 /* TRAIL/TSA/STSA... */
                    mp4_set_slice(payload, payload_len, 0);
                break;
        }
        return;
    }

    switch (type) {
        case NalUnitType_SPS:
            mp4_set_sps(payload, payload_len, 0);
            break;
        case NalUnitType_PPS:
            mp4_set_pps(payload, payload_len, 0);
            break;
        case NalUnitType_CodedSliceIdr:
            mp4_set_slice(payload, payload_len, 1);
            break;
        case NalUnitType_CodedSliceNonIdr:
            mp4_set_slice(payload, payload_len, 0);
            break;
        default:
            break;
    }
}

static bool record_emit_fragment_locked(void) {
    struct BitBuf buf;

    pthread_mutex_lock(&mp4_mtx);

    if (!recordState.header_sent) {
        mp4_get_header(&buf);
        /* No moov until the parameter sets arrive; nothing decodable to keep. */
        if (buf.offset == 0) {
            pthread_mutex_unlock(&mp4_mtx);
            return true;
        }

        if (!record_write_locked(buf.buf, buf.offset)) {
            pthread_mutex_unlock(&mp4_mtx);
            return false;
        }
        recordSize += buf.offset;

        recordState.sequence_number = 0;
        recordState.base_data_offset = buf.offset;
        recordState.base_media_decode_time = 0;
        recordState.nals_count = 0;
        recordState.default_sample_duration = default_sample_size;
        recordState.header_sent = true;
    }

    if (mp4_set_state(&recordState) != BUF_OK) {
        pthread_mutex_unlock(&mp4_mtx);
        return false;
    }

    if (mp4_get_moof(&buf) != BUF_OK) {
        pthread_mutex_unlock(&mp4_mtx);
        return false;
    }
    if (!record_write_locked(buf.buf, buf.offset)) {
        pthread_mutex_unlock(&mp4_mtx);
        return false;
    }
    recordSize += buf.offset;

    if (mp4_get_mdat(&buf) != BUF_OK) {
        pthread_mutex_unlock(&mp4_mtx);
        return false;
    }
    if (!record_write_locked(buf.buf, buf.offset)) {
        pthread_mutex_unlock(&mp4_mtx);
        return false;
    }
    recordSize += buf.offset;

    pthread_mutex_unlock(&mp4_mtx);
    return true;
}

static bool record_should_rotate_locked(void) {
    time_t now;

    if (app_config.record_segment_size > 0 &&
        recordSize >= (unsigned long long)app_config.record_segment_size)
        return true;

    if (app_config.record_segment_duration <= 0 || !recordStartTime) return false;

    now = time(NULL);
    if (now == (time_t)-1) return false;
    return now - recordStartTime >= app_config.record_segment_duration;
}

static void record_flush_locked(void) {
    time_t now;

    if (!recordFile) return;

    now = time(NULL);
    if (now == (time_t)-1 || now == recordLastFlush) return;
    recordLastFlush = now;

    /* A removed card must close the segment instead of letting writes fall
     * through to the SPI NAND.  The check runs once per second. */
    if (!gating_sd_mounted()) {
        HAL_DANGER("record", "microSD removed; closing '%s' and stopping\n",
            recordPath);
        record_close_locked();
        recordActive = false;
        recordStartTime = 0;
        return;
    }

    if (fflush(recordFile))
        HAL_DANGER("record", "Flushing '%s' failed: %s\n", recordPath, strerror(errno));
}

/**
 * Starts a new recording session
 * @return true once recording, false if the output could not be opened
 */
bool record_start(void) {
    pthread_mutex_lock(&recordMtx);

    if (recordActive) {
        pthread_mutex_unlock(&recordMtx);
        return true;
    }

    recordStartTime = time(NULL);
    if (recordStartTime == (time_t)-1) recordStartTime = 0;

    if (!record_open_locked()) {
        pthread_mutex_unlock(&recordMtx);
        return false;
    }

    recordActive = true;
    pthread_mutex_unlock(&recordMtx);
    return true;
}

/**
 * Stops the current recording session and finalizes its file
 */
void record_stop(void) {
    pthread_mutex_lock(&recordMtx);

    record_close_locked();
    recordActive = false;
    recordStartTime = 0;

    pthread_mutex_unlock(&recordMtx);
}

/**
 * Reports whether a recording session is running
 * @return true if recording, false otherwise
 */
bool record_active(void) {
    bool active;

    pthread_mutex_lock(&recordMtx);
    active = recordActive;
    pthread_mutex_unlock(&recordMtx);

    return active;
}

/**
 * Reports when the current recording session started
 * @return the start time, or 0 when not recording
 */
time_t record_start_time(void) {
    time_t start;

    pthread_mutex_lock(&recordMtx);
    start = recordActive ? recordStartTime : 0;
    pthread_mutex_unlock(&recordMtx);

    return start;
}

/**
 * Ingests a video stream into the current recording
 * @param stream Pointer to the hal_vidstream carrying the NALU packs
 * @param isH265 1 if H.265 (HEVC), 0 for H.264 (AVC)
 */
void record_ingest_stream(hal_vidstream *stream, char isH265) {
    if (!stream) return;

    pthread_mutex_lock(&recordMtx);
    if (!recordActive || !recordFile) {
        pthread_mutex_unlock(&recordMtx);
        return;
    }

    for (unsigned int i = 0; i < stream->count; ++i) {
        hal_vidpack *pack = &stream->pack[i];
        unsigned char *pack_data;
        unsigned int pack_len;

        if (pack->length <= pack->offset) continue;
        pack_len = pack->length - pack->offset;
        pack_data = pack->data + pack->offset;

        /* The H.265 pack metadata is a single entry for the whole access
         * unit, so split the Annex B buffer here for both codecs. */
        unsigned char *nalptr = pack_data;
        size_t single_len = 0;
        while (nal_split(pack_data, &nalptr, &single_len, pack_len) == 0) {
            if (!single_len) continue;
            unsigned int type = isH265 ?
                ((nalptr[0] >> 1) & 0x3F) : (nalptr[0] & 0x1F);
            record_feed_payload((const char *)nalptr, single_len, type, isH265);
        }

        if (!record_emit_fragment_locked()) {
            HAL_DANGER("record", "Stopping recording after a write failure!\n");
            record_close_locked();
            recordActive = false;
            recordStartTime = 0;
            pthread_mutex_unlock(&recordMtx);
            return;
        }

        /* Rotate between fragments so a segment never splits a moof from its
         * mdat, and the next one starts with a fresh header. */
        if (record_should_rotate_locked() && !record_rotate_locked()) {
            recordActive = false;
            recordStartTime = 0;
            pthread_mutex_unlock(&recordMtx);
            return;
        }
    }

    record_flush_locked();
    pthread_mutex_unlock(&recordMtx);
}

bool record_wants_source(bool sub) {
    bool wants;

    pthread_mutex_lock(&recordMtx);
    wants = recordActive &&
        (recordEventActive ? recordEventSub : app_config.record_sub) == sub;
    pthread_mutex_unlock(&recordMtx);
    return wants;
}

static void *record_event_thread(void *arg) {
    unsigned int seconds = (unsigned int)(uintptr_t)arg;
    unsigned int generation;

    pthread_mutex_lock(&recordMtx);
    generation = recordEventGeneration;
    pthread_mutex_unlock(&recordMtx);

    for (unsigned int i = 0; i < seconds * 10; i++) {
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 100 * 1000 * 1000 };
        nanosleep(&ts, NULL);
        pthread_mutex_lock(&recordMtx);
        if (!recordEventActive || generation != recordEventGeneration) {
            pthread_mutex_unlock(&recordMtx);
            return NULL;
        }
        pthread_mutex_unlock(&recordMtx);
    }

    pthread_mutex_lock(&recordMtx);
    if (recordEventActive && generation == recordEventGeneration) {
        recordEventActive = false;
        record_close_locked();
        recordActive = false;
        recordStartTime = 0;
        HAL_INFO("record", "Motion clip finished\n");
    }
    pthread_mutex_unlock(&recordMtx);
    return NULL;
}

bool record_start_event(unsigned int seconds, bool sub) {
    pthread_t thread;
    pthread_attr_t attr;

    if (storageBusy || !gating_sd_mounted()) {
        HAL_WARNING("record", "Motion recording needs an idle, mounted microSD\n");
        return false;
    }
    if (seconds < 1) seconds = 1;

    pthread_mutex_lock(&recordMtx);
    if (recordActive) {
        pthread_mutex_unlock(&recordMtx);
        return true;
    }
    recordEventActive = true;
    recordEventSub = sub;
    recordEventGeneration++;
    recordStartTime = time(NULL);
    if (!record_open_locked()) {
        recordEventActive = false;
        recordStartTime = 0;
        pthread_mutex_unlock(&recordMtx);
        return false;
    }
    recordActive = true;
    pthread_mutex_unlock(&recordMtx);

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&thread, &attr, record_event_thread,
            (void *)(uintptr_t)seconds)) {
        HAL_DANGER("record", "Starting the motion clip timer failed!\n");
        record_stop_event();
    }
    pthread_attr_destroy(&attr);
    return true;
}

void record_stop_event(void) {
    pthread_mutex_lock(&recordMtx);
    if (!recordEventActive) {
        pthread_mutex_unlock(&recordMtx);
        return;
    }
    recordEventActive = false;
    recordEventGeneration++;
    record_close_locked();
    recordActive = false;
    recordStartTime = 0;
    pthread_mutex_unlock(&recordMtx);
}
