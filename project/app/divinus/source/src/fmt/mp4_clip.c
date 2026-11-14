#include "mp4_clip.h"

#include <string.h>

#include "moof.h"
#include "moov.h"
#include "nal.h"

void mp4_clip_init(mp4_clip_ctx *ctx, int width, int height, int framerate,
    int isH265)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->width = width;
    ctx->height = height;
    ctx->framerate = framerate > 0 ? framerate : 30;
    ctx->is_h265 = isH265 ? 1 : 0;
    ctx->default_duration = 40000;
    ctx->timescale = (uint32_t)ctx->default_duration * (uint32_t)ctx->framerate;
    ctx->vid_duration = ctx->default_duration;
    ctx->state.default_sample_duration = ctx->default_duration;
}

void mp4_clip_free(mp4_clip_ctx *ctx)
{
    free(ctx->header.buf);
    free(ctx->moof.buf);
    free(ctx->mdat.buf);
    memset(ctx, 0, sizeof(*ctx));
}

static int clip_params_ready(const mp4_clip_ctx *ctx)
{
    if (ctx->sps_len == 0 || ctx->pps_len == 0) return 0;
    if (ctx->is_h265 && ctx->vps_len == 0) return 0;
    return 1;
}

static void clip_set_param(char *dst, uint16_t *dstLen, const char *data,
    uint32_t len)
{
    *dstLen = (uint16_t)(len < 128 ? len : 128);
    memcpy(dst, data, *dstLen);
}

/* Builds the moov header once all parameter sets are known. */
static int clip_build_header(mp4_clip_ctx *ctx)
{
    struct MoovInfo info;

    if (ctx->header_ready) return 0;
    if (!clip_params_ready(ctx)) return -1;

    memset(&info, 0, sizeof(info));
    info.is_h265 = ctx->is_h265 & 1;
    info.profile_idc = 100;
    info.level_idc = 41;
    info.width = (uint16_t)ctx->width;
    info.height = (uint16_t)ctx->height;
    info.horizontal_resolution = 0x00480000;
    info.vertical_resolution = 0x00480000;
    info.timescale = ctx->timescale;
    info.vps = ctx->vps;
    info.vps_length = ctx->vps_len;
    info.sps = ctx->sps;
    info.sps_length = ctx->sps_len;
    info.pps = ctx->pps;
    info.pps_length = ctx->pps_len;

    pthread_mutex_lock(&mp4_mtx);
    ctx->header.offset = 0;
    if (write_header(&ctx->header, &info) != BUF_OK) {
        pthread_mutex_unlock(&mp4_mtx);
        ctx->header_failed = 1;
        return -1;
    }
    pthread_mutex_unlock(&mp4_mtx);

    ctx->header_ready = 1;
    return 0;
}

/* One sample per slice NAL, mirroring record.c's mp4 usage. */
static int clip_write_sample(mp4_clip_ctx *ctx, const char *data, uint32_t len,
    uint32_t duration, int isKey, FILE *file)
{
    struct SampleInfo sample;
    enum BufError err;

    memset(&sample, 0, sizeof(sample));
    sample.size = len + 4;
    sample.duration = duration;
    sample.flags = isKey ? 0 : 65536;

    pthread_mutex_lock(&mp4_mtx);

    ctx->moof.offset = 0;
    err = write_moof(&ctx->moof, 0, 0, 0, ctx->default_duration, &sample, 1,
        NULL, 0);
    if (err == BUF_OK) {
        ctx->mdat.offset = 0;
        err = write_mdat(&ctx->mdat, data, len, NULL, 0);
    }
    if (err == BUF_OK) {
        /* Same patch-up as mp4_set_state(), but for this context. */
        if (pos_sequence_number > 0)
            err = put_u32_be_to_offset(&ctx->moof, pos_sequence_number,
                ctx->state.sequence_number);
        if (err == BUF_OK && pos_base_data_offset > 0)
            err = put_u64_be_to_offset(&ctx->moof, pos_base_data_offset,
                ctx->state.base_data_offset);
        if (err == BUF_OK && pos_video_media_decode_time > 0)
            err = put_u64_be_to_offset(&ctx->moof,
                pos_video_media_decode_time,
                ctx->state.base_media_decode_time);
    }
    pthread_mutex_unlock(&mp4_mtx);

    if (err != BUF_OK) return -2;

    ctx->state.sequence_number++;
    ctx->state.base_data_offset += ctx->moof.offset + ctx->mdat.offset;
    ctx->state.base_media_decode_time += duration;

    if (fwrite(ctx->moof.buf, 1, ctx->moof.offset, file) != ctx->moof.offset)
        return -2;
    if (fwrite(ctx->mdat.buf, 1, ctx->mdat.offset, file) != ctx->mdat.offset)
        return -2;
    return 0;
}

int mp4_clip_feed(mp4_clip_ctx *ctx, const char *au, uint32_t len,
    uint64_t ts_ms, int isKey, FILE *file)
{
    unsigned char *cursor = (unsigned char *)au;
    size_t nalLen = 0;
    uint32_t duration = ctx->default_duration;
    int slices = 0;
    int result = 0;

    if (!ctx || !au || !len || !file) return -1;

    if (ctx->last_ts_ms && ts_ms > ctx->last_ts_ms && ctx->timescale) {
        uint64_t ticks = (ts_ms - ctx->last_ts_ms) * ctx->timescale / 1000;

        if (ticks > 0 && ticks < ctx->timescale)
            duration = (uint32_t)ticks;
    }
    ctx->last_ts_ms = ts_ms;
    ctx->vid_duration = duration;

    while (nal_split((unsigned char *)au, &cursor, &nalLen, len) == 0) {
        unsigned int type;

        if (!nalLen) continue;
        type = ctx->is_h265 ? ((cursor[0] >> 1) & 0x3F) : (cursor[0] & 0x1F);

        if (ctx->is_h265) {
            if (type == NalUnitType_VPS_HEVC) {
                clip_set_param(ctx->vps, &ctx->vps_len, (char *)cursor, nalLen);
                continue;
            }
            if (type == NalUnitType_SPS_HEVC) {
                clip_set_param(ctx->sps, &ctx->sps_len, (char *)cursor, nalLen);
                continue;
            }
            if (type == NalUnitType_PPS_HEVC) {
                clip_set_param(ctx->pps, &ctx->pps_len, (char *)cursor, nalLen);
                continue;
            }
            /* 0..9 are TRAIL/TSA/STSA/RADL/RASL slices, 16..21 the
             * BLA/CRA/IDR pictures. */
            if (type > 21) continue;
        } else {
            if (type == NalUnitType_SPS) {
                clip_set_param(ctx->sps, &ctx->sps_len, (char *)cursor, nalLen);
                continue;
            }
            if (type == NalUnitType_PPS) {
                clip_set_param(ctx->pps, &ctx->pps_len, (char *)cursor, nalLen);
                continue;
            }
            if (type < 1 || type > 5) continue;
        }

        if (clip_build_header(ctx))
            return -1;
        if (!ctx->header_ready)
            return -1;

        if (!ctx->state.header_sent) {
            if (fwrite(ctx->header.buf, 1, ctx->header.offset, file) !=
                ctx->header.offset)
                return -2;
            ctx->state.header_sent = true;
        }

        result = clip_write_sample(ctx, (const char *)cursor, (uint32_t)nalLen,
            slices ? 0 : duration, isKey, file);
        if (result) return result;
        slices++;
    }

    return slices ? 0 : -1;
}

uint32_t mp4_clip_last_fragment_size(const mp4_clip_ctx *ctx)
{
    return ctx->moof.offset + ctx->mdat.offset;
}
