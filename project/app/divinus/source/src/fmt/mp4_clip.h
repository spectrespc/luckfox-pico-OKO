#pragma once

/*
 * Independent fragmented-MP4 muxer for event clips.
 *
 * The recorder's mp4.c keeps one global stream state (parameter sets,
 * header, moof/mdat buffers), so it cannot mux a second file while the
 * continuous recording is running.  This module keeps the same state in a
 * caller-owned context and reuses the pure BitBuf writers from moov/moof.
 * The moof module stores its patch offsets in globals, so both muxers call
 * write_moof() under the shared mp4_mtx.
 */

#include <stdint.h>
#include <stdio.h>

#include "bitbuf.h"
#include "mp4.h"

typedef struct {
    struct BitBuf header;
    struct BitBuf moof;
    struct BitBuf mdat;
    struct Mp4State state;

    char vps[128], sps[128], pps[128];
    uint16_t vps_len, sps_len, pps_len;

    int is_h265;
    int width, height;
    int framerate;
    uint32_t timescale;
    uint32_t default_duration;
    uint64_t last_ts_ms;
    uint32_t vid_duration;

    int header_ready;
    int header_failed;
} mp4_clip_ctx;

/* Initialises the context for one video track.  No memory is allocated until
 * the first parameter set arrives. */
void mp4_clip_init(mp4_clip_ctx *ctx, int width, int height, int framerate,
    int isH265);
void mp4_clip_free(mp4_clip_ctx *ctx);

/* Feeds one Annex-B access unit (one encoder frame) and appends the header
 * (once) and one moof+mdat pair to `file`.  `ts_ms` is a monotonic capture
 * timestamp; the sample duration is derived from the delta.
 *
 * Returns 0 on success, -1 when the AU has no slice or the parameter sets are
 * still incomplete, -2 on a write error. */
int mp4_clip_feed(mp4_clip_ctx *ctx, const char *au, uint32_t len,
    uint64_t ts_ms, int isKey, FILE *file);

/* Size of the last appended fragment (moof + mdat). */
uint32_t mp4_clip_last_fragment_size(const mp4_clip_ctx *ctx);
