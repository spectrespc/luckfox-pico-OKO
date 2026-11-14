#pragma once

#include <stdint.h>

/*
 * History of the latest YOLO inferences, exposed to /api/yolo?detections=1.
 *
 * The module is free of RKNN/HAL headers so a host test can exercise it.
 * Coordinates keep the 0..10000 normalisation of yolo_postprocess.h, so a
 * record is independent of the source frame size.
 */

#define YOLO_HISTORY_RECORDS 64
#define YOLO_HISTORY_DETS 8

typedef struct {
    int16_t cls;
    uint16_t score;         /* 0..65535 maps to 0.0..1.0 */
    uint16_t x, y, w, h;    /* normalised to 0..10000 */
} yolo_history_det;

typedef struct {
    uint32_t seq;           /* starts at 1 after reset, never 0 */
    uint64_t ts;            /* CLOCK_MONOTONIC milliseconds of the frame */
    uint8_t source;         /* 1 = vpss3, 2 = vi0 */
    uint8_t aiisp;          /* hal_aiisp value used for the inference */
    uint8_t count;          /* stored detections, <= YOLO_HISTORY_DETS */
    uint8_t pad;
    yolo_history_det det[YOLO_HISTORY_DETS];
} yolo_history_record;

/* Drop every record and restart the sequence.  Called when the detector
 * starts, so a stale record from a previous run is never served. */
void yolo_history_reset(void);

/* Store one inference.  `det` may hold more than YOLO_HISTORY_DETS entries:
 * only the strongest ones (the post-processor sorts descending) are kept.
 * Returns the new sequence number, or 0 when the input is invalid. */
uint32_t yolo_history_push(uint64_t ts, uint8_t source, uint8_t aiisp,
    const yolo_history_det *det, int count);

/* Copy records newer than `since`, oldest first, at most `max` entries.
 * `since == 0` asks for everything still held in the ring.  *lastSeq is the
 * sequence of the last copied record; with nothing copied it is the newest
 * sequence held (0 when empty), so a caller can keep its cursor. */
int yolo_history_copy(uint32_t since, yolo_history_record *out, int max,
    uint32_t *lastSeq);