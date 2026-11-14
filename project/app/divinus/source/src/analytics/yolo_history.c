#include "yolo_history.h"

#include <pthread.h>
#include <string.h>

/*
 * Static ring of the last YOLO_HISTORY_RECORDS inferences.  A record is
 * 120 B, so the whole ring is 7680 B (7.5 KiB) allocated once; the inference
 * path only pays one memcpy per frame.
 */

static pthread_mutex_t historyMtx = PTHREAD_MUTEX_INITIALIZER;
static yolo_history_record history[YOLO_HISTORY_RECORDS];
static int historyNext;     /* slot receiving the next record */
static int historyCount;    /* valid records, 0..YOLO_HISTORY_RECORDS */
static uint32_t historySeq;

void yolo_history_reset(void)
{
    pthread_mutex_lock(&historyMtx);
    memset(history, 0, sizeof(history));
    historyNext = 0;
    historyCount = 0;
    historySeq = 0;
    pthread_mutex_unlock(&historyMtx);
}

uint32_t yolo_history_push(uint64_t ts, uint8_t source, uint8_t aiisp,
    const yolo_history_det *det, int count)
{
    yolo_history_record *record;
    uint32_t seq;

    if (count < 0 || (count > 0 && !det)) return 0;
    if (count > YOLO_HISTORY_DETS) count = YOLO_HISTORY_DETS;

    pthread_mutex_lock(&historyMtx);
    record = &history[historyNext];

    historySeq++;
    /* 0 stays the "from the beginning" cursor of yolo_history_copy(). */
    if (!historySeq) historySeq = 1;
    seq = historySeq;

    record->seq = seq;
    record->ts = ts;
    record->source = source;
    record->aiisp = aiisp;
    record->count = (uint8_t)count;
    record->pad = 0;
    if (count) memcpy(record->det, det, (size_t)count * sizeof(det[0]));

    historyNext = (historyNext + 1) % YOLO_HISTORY_RECORDS;
    if (historyCount < YOLO_HISTORY_RECORDS) historyCount++;

    pthread_mutex_unlock(&historyMtx);
    return seq;
}

int yolo_history_copy(uint32_t since, yolo_history_record *out, int max,
    uint32_t *lastSeq)
{
    int newer, take, start, i;

    if (!out || max <= 0) {
        if (lastSeq) *lastSeq = 0;
        return 0;
    }

    pthread_mutex_lock(&historyMtx);

    if (!historyCount) {
        if (lastSeq) *lastSeq = 0;
        pthread_mutex_unlock(&historyMtx);
        return 0;
    }

    if (!since) {
        newer = historyCount;
    } else {
        /* Signed difference tolerates wrap and a cursor from before a reset. */
        int32_t diff = (int32_t)(historySeq - since);

        if (diff <= 0) newer = 0;
        else newer = diff < historyCount ? diff : historyCount;
    }

    take = newer < max ? newer : max;
    start = (historyNext - newer + YOLO_HISTORY_RECORDS) % YOLO_HISTORY_RECORDS;
    for (i = 0; i < take; i++)
        out[i] = history[(start + i) % YOLO_HISTORY_RECORDS];

    /* A truncated answer still returns a usable cursor, so the caller keeps
     * polling from the last record it actually received. */
    if (lastSeq) *lastSeq = take ? out[take - 1].seq : historySeq;

    pthread_mutex_unlock(&historyMtx);
    return take;
}