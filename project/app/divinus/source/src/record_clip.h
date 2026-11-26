#pragma once

#include <stddef.h>

#include "hal/types.h"

/* Event clips: a RAM ring of encoded main-stream access units, trimmed to the
 * last keyframe before the event, then written to a fragmented MP4 in /tmp and
 * delivered with PUT /ingest/clip.  Failed deliveries spool to the microSD
 * outbox when a card is mounted. */
int record_clip_start(void);
void record_clip_stop(void);

/* True when the ring exists and the main encoder tap should feed it. */
int record_clip_wanted(void);

/* Called from the encoder thread for the main channel; never blocks. */
void record_clip_ingest_stream(hal_vidstream *stream, char isH265);

/* Starts an asynchronous clip for an event.  Returns 0 when a job slot was
 * allocated, -1 with `error` filled otherwise. */
int record_clip_trigger(const char *eventId, const char *cause,
    const char *model, char *error, size_t errorSize);

/* JSON object for GET /api/clip (without the surrounding braces). */
size_t record_clip_status_json(char *out, size_t size);

/* 1 while an event clip is still being cut or delivered.  [sleep] sd_off
 * refuses to detach the card in that window. */
int record_clip_busy(void);

/* Human-readable reason when the feature cannot run, or NULL when it can. */
const char *record_clip_unavailable(void);
