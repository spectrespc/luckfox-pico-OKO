#if defined(__ARM_PCS_VFP)

/*
 * YOLOv5 detection on the RV1106 NPU.
 *
 * Frame flow (one capture thread):
 *   VI/VPSS GetChnFrame -> NV12 copy into the ring -> ReleaseChnFrame
 *   -> RGA NV12->RGB888 into the RKNN input -> rknn_run -> decode/NMS.
 *
 * Event flow: a detection opens an event, the pre-roll is copied out of the
 * ring and the following frames are appended until post_seconds expires (a
 * new detection inside the window extends it).  A second thread encodes the
 * package through the dedicated VENC JPEG channel and sends one PUT per frame.
 */

#include "yolo.h"
#include "yolo_postprocess.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "app_config.h"
#include "hal/globals.h"
#include "hal/macros.h"
#include "hal/plus/rk_hal.h"
#include "hal/plus/rk_rga.h"
#include <sys/mman.h>
#include "http_post.h"

#define YOLO_EVENT_COOLDOWN_MS 5000
#define YOLO_MAX_OUTPUTS 3
#define YOLO_MAX_CONSECUTIVE_FAULTS 5

#define YOLO_EVENT_FREE 0
#define YOLO_EVENT_FILLING 1
#define YOLO_EVENT_READY 2

static const int yolo_anchors[3][6] = {
    {10, 13, 16, 30, 33, 23},
    {30, 61, 62, 45, 59, 119},
    {116, 90, 156, 198, 373, 326}
};

typedef struct {
    rknn_context ctx;
    rk_rknn_impl *api;          /* selected RKNN runtime (1.x or 2.x) */
    int runtime;                /* 1 or 2, for logging and models.json */
    int ready;
    int generation;
    int width, height, classes;
    int inputStride;            /* pixels per row in the NN input buffer */
    rknn_tensor_attr inputAttr;
    rknn_tensor_mem *inputMem;
    rknn_tensor_mem *outputMem[YOLO_MAX_OUTPUTS];
    rknn_tensor_attr outputAttr[YOLO_MAX_OUTPUTS];
    int outputCount;
    char path[192];
} yolo_model_dev;

typedef struct {
    void *blk;                  /* MB_BLK returned by RK_MPI_SYS_MmzAlloc */
    unsigned char *data;        /* CPU mapping of the full MMZ block */
    int fd;
    unsigned long long ts;
} yolo_ring_frame;

typedef struct {
    int state;
    int count;
    unsigned long long ts[64];
    unsigned char *frames;
} yolo_event_slot;

static yolo_model_dev yoloModel;
static pthread_mutex_t modelMtx = PTHREAD_MUTEX_INITIALIZER;

static pthread_t captureThread, senderThread;
static pthread_mutex_t senderMtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t senderCond = PTHREAD_COND_INITIALIZER;
static volatile int yoloRunning, senderRunning;
static int captureStarted, senderStarted;

static yolo_ring_frame *ring;
static int ringCapacity, ringCount, ringNext;
static yolo_event_slot eventSlots[2];

static int activeEvent = -1;
static unsigned long long activeEventEnd;
static unsigned long long lastTriggerMs;
static unsigned long long eventSequence;
static unsigned long long osdShownMs;
static int osdOwned;

static int consecutiveFaults;
static int rgaFailed;
static int inferenceDisabled;
static int frameWidth, frameHeight, frameBytes;
static int preFrames;
static int eventMaxFrames;
static unsigned long long eventPostMs;

static unsigned long long yolo_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000ull + ts.tv_nsec / 1000000ull;
}

static void yolo_osd(const char *text)
{
    FILE *file = fopen("/tmp/osd-ivs.txt", "w");
    if (!file) return;
    if (text && *text) fprintf(file, "%s\n", text);
    fclose(file);
    chmod("/tmp/osd-ivs.txt", 0644);
}

static void yolo_model_destroy(yolo_model_dev *model)
{
    if (model->ctx && model->api) {
        for (int i = 0; i < model->outputCount; i++) {
            if (model->outputMem[i]) {
                /* rknn_create_mem() marks the block ALLOC_INSIDE, so
                 * rknn_destroy_mem() releases the structure as well. */
                model->api->fnDestroyMem(model->ctx, model->outputMem[i]);
                model->outputMem[i] = NULL;
            }
        }
        if (model->inputMem) {
            model->api->fnDestroyMem(model->ctx, model->inputMem);
            model->inputMem = NULL;
        }
        model->api->fnDestroy(model->ctx);
        model->ctx = 0;
    }
    model->ready = 0;
    model->outputCount = 0;
}

/* Opens a model and validates that its tensors match the configured input.
 * `runTest` executes one inference: this catches a model that loads but is
 * rejected by the NPU (wrong toolkit generation, unsupported operators). */
static int yolo_model_open(const char *path, yolo_model_dev *model,
    int runTest, int runtime, char *error, size_t errorSize)
{
    rknn_input_output_num io;
    int ret;

    memset(&io, 0, sizeof(io));
    memset(model, 0, sizeof(*model));
    snprintf(error, errorSize, "%s", "");
    model->runtime = runtime;
    model->api = rk_rknn_for_runtime(runtime);

    if (!path || !*path) {
        snprintf(error, errorSize, "no model path configured");
        return EXIT_FAILURE;
    }
    if (access(path, R_OK)) {
        snprintf(error, errorSize, "model file is missing: %s", path);
        return EXIT_FAILURE;
    }
    if (!model->api) {
        snprintf(error, errorSize,
            "RKNN %d.x runtime is not installed (%s)", runtime,
            runtime == 2 ? RK_RKNN_RUNTIME2_PATH : RK_RKNN_RUNTIME1_PATH);
        return EXIT_FAILURE;
    }

    ret = model->api->fnInit(&model->ctx, (void *)path, 0, 0, NULL);
    if (ret < 0) {
        snprintf(error, errorSize,
            "rknn_init failed (%d): the model was probably built by an "
            "incompatible rknn-toolkit2", ret);
        return EXIT_FAILURE;
    }

    {
        rknn_sdk_version sdk;
        memset(&sdk, 0, sizeof(sdk));
        if (model->api->fnQuery(model->ctx, RKNN_QUERY_SDK_VERSION, &sdk,
                sizeof(sdk))) {
            snprintf(error, errorSize, "cannot query the runtime version");
            goto fail;
        }
        if (atoi(sdk.api_version) != runtime) {
            snprintf(error, errorSize,
                "runtime %d was loaded but reports RKNN SDK %s", runtime,
                sdk.api_version);
            goto fail;
        }
    }

    if (model->api->fnQuery(model->ctx, RKNN_QUERY_IN_OUT_NUM, &io, sizeof(io)) ||
        io.n_input != 1 || io.n_output < 1 ||
        io.n_output > YOLO_MAX_OUTPUTS) {
        snprintf(error, errorSize,
            "unsupported model layout (inputs %u, outputs %u)",
            io.n_input, io.n_output);
        goto fail;
    }
    model->outputCount = (int)io.n_output;

    memset(&model->inputAttr, 0, sizeof(model->inputAttr));
    model->inputAttr.index = 0;
    if (model->api->fnQuery(model->ctx, RKNN_QUERY_NATIVE_INPUT_ATTR,
            &model->inputAttr, sizeof(model->inputAttr))) {
        snprintf(error, errorSize, "cannot query the input tensor");
        goto fail;
    }

    {
        int dim1 = (int)model->inputAttr.dims[1];
        int dim2 = (int)model->inputAttr.dims[2];
        int cfgW = (int)app_config.yolo_input_width;
        int cfgH = (int)app_config.yolo_input_height;

        if (dim1 == cfgH && dim2 == cfgW) {
            model->height = dim1;
            model->width = dim2;
        } else if (dim1 == cfgW && dim2 == cfgH) {
            model->width = dim1;
            model->height = dim2;
        } else {
            snprintf(error, errorSize,
                "model input %dx%d does not match yolo input_width/height "
                "%dx%d", dim2, dim1, cfgW, cfgH);
            goto fail;
        }
    }

    /* Zero-copy input uses the native row stride.  It is usually equal to the
     * width, but the runtime may pad rows, and the vendor demo copies such
     * rows explicitly; writing packed RGB would shear the picture. */
    model->inputStride = model->inputAttr.w_stride ?
        (int)model->inputAttr.w_stride : model->width;
    if (model->inputStride < model->width ||
        (size_t)model->inputAttr.size_with_stride <
            (size_t)model->inputStride * (size_t)model->height * 3) {
        snprintf(error, errorSize,
            "unsupported input stride %d for a %dx%d RGB tensor",
            model->inputStride, model->width, model->height);
        goto fail;
    }

    model->inputAttr.type = RKNN_TENSOR_UINT8;
    model->inputAttr.fmt = RKNN_TENSOR_NHWC;
    model->inputMem = model->api->fnCreateMem(model->ctx,
        model->inputAttr.size_with_stride);
    if (!model->inputMem) {
        snprintf(error, errorSize, "cannot allocate the input tensor memory");
        goto fail;
    }
    if (model->api->fnSetIoMem(model->ctx, model->inputMem, &model->inputAttr)) {
        snprintf(error, errorSize, "rknn_set_io_mem failed for the input");
        goto fail;
    }

    for (int i = 0; i < model->outputCount; i++) {
        memset(&model->outputAttr[i], 0, sizeof(model->outputAttr[i]));
        model->outputAttr[i].index = i;
        if (model->api->fnQuery(model->ctx, RKNN_QUERY_NATIVE_NHWC_OUTPUT_ATTR,
                &model->outputAttr[i], sizeof(model->outputAttr[i]))) {
            snprintf(error, errorSize, "cannot query output tensor %d", i);
            goto fail;
        }
        /* The zero-copy runtime only fills 3D tensors partially (community
         * reports for RV1106); a 4D NHWC tensor is the supported layout. */
        if (model->outputAttr[i].n_dims != 4) {
            snprintf(error, errorSize,
                "output %d has %u dimensions; a 4D model is required "
                "(reshape the 3D output in the toolkit)",
                i, model->outputAttr[i].n_dims);
            goto fail;
        }
        if (model->outputAttr[i].fmt != RKNN_TENSOR_NHWC) {
            snprintf(error, errorSize,
                "output %d is %s, expected NHWC; rebuild the model with "
                "RKNN_QUERY_NATIVE_NHWC_OUTPUT_ATTR support",
                i, get_format_string(model->outputAttr[i].fmt));
            goto fail;
        }
        model->outputMem[i] = model->api->fnCreateMem(model->ctx,
            model->outputAttr[i].size_with_stride);
        if (!model->outputMem[i]) {
            snprintf(error, errorSize,
                "cannot allocate output tensor memory %d", i);
            goto fail;
        }
        if (model->api->fnSetIoMem(model->ctx, model->outputMem[i],
                &model->outputAttr[i])) {
            snprintf(error, errorSize, "rknn_set_io_mem failed for output %d", i);
            goto fail;
        }
    }

    {
        int channels = (int)model->outputAttr[0].dims[3];
        int classes = channels / 3 - 5;
        if (channels <= 0 || channels % 3 ||
            classes < 1 || classes > 255) {
            snprintf(error, errorSize,
                "output tensor has %d channels, expected 3*(5+classes)",
                channels);
            goto fail;
        }
        /* The decoder assumes the same interleaved NHWC layout for every
         * branch.  A padded/blocked channel count would silently shift all
         * boxes, so reject the model instead of guessing. */
        for (int i = 0; i < model->outputCount; i++) {
            int branchChannels = (int)model->outputAttr[i].dims[3];
            if (branchChannels != channels ||
                model->outputAttr[i].dims[1] <= 0 ||
                model->outputAttr[i].dims[2] <= 0) {
                snprintf(error, errorSize,
                    "output %d has shape %ux%ux%u, expected the %d-channel "
                    "YOLOv5 branch layout",
                    i, model->outputAttr[i].dims[1],
                    model->outputAttr[i].dims[2], branchChannels, channels);
                goto fail;
            }
        }
        model->classes = classes;
    }

    if (model->outputAttr[0].qnt_type != RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC) {
        snprintf(error, errorSize,
            "the model is not INT8 quantised; RV1106 requires an INT8 model");
        goto fail;
    }

    if (runTest) {
        ret = model->api->fnRun(model->ctx, NULL);
        if (ret < 0) {
            snprintf(error, errorSize, "test inference failed (%d)", ret);
            goto fail;
        }
    }

    snprintf(model->path, sizeof(model->path), "%s", path);
    model->ready = 1;
    {
        char shapes[128];
        int used = 0;
        shapes[0] = '\0';
        for (int i = 0; i < model->outputCount && used < (int)sizeof(shapes);
                i++)
            used += snprintf(shapes + used, sizeof(shapes) - used,
                "%s%ux%ux%u", i ? ", " : "",
                model->outputAttr[i].dims[1], model->outputAttr[i].dims[2],
                model->outputAttr[i].dims[3]);
        HAL_INFO("yolo", "Model %s: input %dx%d NHWC stride %d px, "
            "%d 4D outputs (%s)\n", path, model->width, model->height,
            model->inputStride, model->outputCount, shapes);
    }
    return EXIT_SUCCESS;

fail:
    yolo_model_destroy(model);
    return EXIT_FAILURE;
}

int yolo_model_probe_runtime(const char *path, int runtime,
    yolo_model_info *info, char *error, size_t errorSize)
{
    yolo_model_dev model;
    int ret = yolo_model_open(path, &model, 1, runtime, error, errorSize);

    if (ret == EXIT_SUCCESS) {
        if (info) {
            rknn_sdk_version sdk;
            memset(info, 0, sizeof(*info));
            info->width = model.width;
            info->height = model.height;
            info->classes = model.classes;
            info->runtime = model.runtime;
            memset(&sdk, 0, sizeof(sdk));
            if (!model.api->fnQuery(model.ctx, RKNN_QUERY_SDK_VERSION, &sdk,
                    sizeof(sdk)))
                snprintf(info->sdk, sizeof(info->sdk), "%s", sdk.api_version);
        }
        yolo_model_destroy(&model);
    }
    return ret;
}

int yolo_model_probe(const char *path, yolo_model_info *info,
    char *error, size_t errorSize)
{
    return yolo_model_probe_runtime(path, (int)app_config.yolo_runtime,
        info, error, errorSize);
}

int yolo_model_swap(const char *path, char *error, size_t errorSize)
{
    yolo_model_dev model;
    int ret = yolo_model_open(path, &model, 1, (int)app_config.yolo_runtime,
        error, errorSize);

    if (ret != EXIT_SUCCESS) return ret;

    pthread_mutex_lock(&modelMtx);
    yolo_model_destroy(&yoloModel);
    model.generation = yoloModel.generation + 1;
    yoloModel = model;
    pthread_mutex_unlock(&modelMtx);

    HAL_INFO("yolo", "Active model: %s (%dx%d, %d classes, runtime %d)\n",
        path, model.width, model.height, model.classes, model.runtime);
    return EXIT_SUCCESS;
}

/* BT.601 NV12 -> packed RGB888 with nearest-neighbour scaling.  The NPU
 * consumes RGB888 on RV1106, and librga is not usable on this image (the
 * legacy blit segfaults and im2d cannot import the MMZ/dma-buf fds), so the
 * conversion runs on the A7 core.  At the default 5 fps a 640x384 frame
 * costs a few tens of milliseconds, far below the 200 ms budget. */
static void yolo_convert_nv12_rgb(const unsigned char *src, int sw, int sh,
    int sstride, unsigned char *dst, int dw, int dh, int dstride)
{
    const unsigned char *uvBase = src + (size_t)sstride * sh;
    int same = (sw == dw && sh == dh);

    for (int y = 0; y < dh; y++) {
        int sy = same ? y : (int)((long)y * sh / dh);
        const unsigned char *yrow = src + (size_t)sy * sstride;
        const unsigned char *uvrow = uvBase + (size_t)(sy / 2) * sstride;
        unsigned char *out = dst + (size_t)y * dstride;

        for (int x = 0; x < dw; x++) {
            int sx = same ? x : (int)((long)x * sw / dw);
            int Y = yrow[sx];
            int U = uvrow[(sx & ~1)];
            int V = uvrow[(sx & ~1) + 1];
            int c = Y - 16;
            int d = U - 128;
            int e = V - 128;
            int r = (298 * c + 409 * e + 128) >> 8;
            int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
            int b = (298 * c + 516 * d + 128) >> 8;

            out[0] = (unsigned char)(r < 0 ? 0 : (r > 255 ? 255 : r));
            out[1] = (unsigned char)(g < 0 ? 0 : (g > 255 ? 255 : g));
            out[2] = (unsigned char)(b < 0 ? 0 : (b > 255 ? 255 : b));
            out += 3;
        }
    }
}

/* Nearest-neighbour NV12 resize for the emergency vi0 source. */
static void yolo_convert_nv12_nv12(const unsigned char *src, int sw, int sh,
    int sstride, unsigned char *dst, int dw, int dh)
{
    const unsigned char *srcUv = src + (size_t)sstride * sh;
    unsigned char *dstUv = dst + (size_t)dw * dh;

    for (int y = 0; y < dh; y++) {
        int sy = (int)((long)y * sh / dh);
        const unsigned char *row = src + (size_t)sy * sstride;
        unsigned char *out = dst + (size_t)y * dw;
        for (int x = 0; x < dw; x++)
            out[x] = row[(int)((long)x * sw / dw)];
    }
    for (int y = 0; y < dh / 2; y++) {
        int sy = (int)((long)y * (sh / 2) / (dh / 2));
        const unsigned char *row = srcUv + (size_t)sy * sstride;
        unsigned char *out = dstUv + (size_t)y * dw;
        for (int x = 0; x < dw; x++)
            out[x] = row[(int)((long)x * sw / dw) & ~1];
    }
}

static int yolo_copy_frame(const rk_common_frame_info *src, unsigned char *dst,
    int dstFd)
{
    unsigned char *data = (unsigned char *)rk_mb.fnGetData(src->frame.mbBlk);
    unsigned int width = src->frame.width;
    unsigned int height = src->frame.height;
    unsigned int stride = src->frame.virWidth;

    if (!data) return EXIT_FAILURE;

    if (width == (unsigned int)frameWidth && height == (unsigned int)frameHeight) {
        if (stride == width) {
            memcpy(dst, data, (size_t)frameBytes);
        } else {
            for (unsigned int y = 0; y < height; y++)
                memcpy(dst + y * width, data + y * stride, width);
            for (unsigned int y = 0; y < height / 2; y++)
                memcpy(dst + (size_t)width * height + y * width,
                    data + (size_t)stride * height + y * stride, width);
        }
        return EXIT_SUCCESS;
    }

    /* vi0 delivers 1080p frames; scale them down before they enter the ring.
     * Both sides are dma-buf fds, so the RGA MMU never sees malloc memory. */
    (void)dstFd;
    yolo_convert_nv12_nv12(data, (int)width, (int)height, (int)stride,
        dst, frameWidth, frameHeight);
    return EXIT_SUCCESS;
}

static void yolo_ring_push(const rk_common_frame_info *src)
{
    yolo_ring_frame *slot = &ring[ringNext];

    if (!slot->blk) {
        if (rk_mb.fnAlloc(&slot->blk, NULL, NULL, frameBytes))
            return;
        slot->fd = rk_mb.fnGetFd(slot->blk);
        slot->data = mmap(NULL, frameBytes, PROT_READ | PROT_WRITE,
            MAP_SHARED, slot->fd, 0);
        if (slot->data == MAP_FAILED) {
            rk_mb.fnFree(slot->blk);
            slot->blk = NULL;
            slot->data = NULL;
            return;
        }
    }
    if (yolo_copy_frame(src, slot->data, slot->fd)) return;

    slot->ts = yolo_now_ms();
    ringNext = (ringNext + 1) % ringCapacity;
    if (ringCount < ringCapacity) ringCount++;
}

static int yolo_infer(const yolo_ring_frame *frame, yolo_result *result)
{
    int dims[YOLO_MAX_OUTPUTS][4];
    const int8_t *outputs[YOLO_MAX_OUTPUTS];
    int32_t zp[YOLO_MAX_OUTPUTS];
    float scale[YOLO_MAX_OUTPUTS];
    int generation;

    result->count = 0;

    if (pthread_mutex_trylock(&modelMtx)) return EXIT_FAILURE;
    if (!yoloModel.ready || inferenceDisabled) {
        pthread_mutex_unlock(&modelMtx);
        return EXIT_FAILURE;
    }
    generation = yoloModel.generation;

    if (app_config.yolo_convert && !rgaFailed) {
        if (rk_rga_convert_nv12_rgb(frame->fd, frameWidth, frameHeight,
                yoloModel.inputMem->fd, yoloModel.width, yoloModel.height,
                yoloModel.inputStride) == 0) {
            goto converted;
        }
        /* Any RGA failure is permanent for this process: the CPU path is
         * the documented fallback and must not be retried per frame. */
        rgaFailed = 1;
        HAL_WARNING("yolo", "RGA NV12->RGB failed, falling back to CPU\n");
    }
    yolo_convert_nv12_rgb(frame->data, frameWidth, frameHeight, frameWidth,
        yoloModel.inputMem->virt_addr, yoloModel.width, yoloModel.height,
        yoloModel.inputStride * 3);

converted:
    if (yoloModel.api->fnRun(yoloModel.ctx, NULL) < 0) {
        consecutiveFaults++;
        if (consecutiveFaults >= YOLO_MAX_CONSECUTIVE_FAULTS &&
            !inferenceDisabled) {
            inferenceDisabled = 1;
            HAL_DANGER("yolo", "NPU inference failed %d times; detection "
                "disabled until restart\n", consecutiveFaults);
        }
        pthread_mutex_unlock(&modelMtx);
        return EXIT_FAILURE;
    }
    consecutiveFaults = 0;

    if (generation != yoloModel.generation) {
        pthread_mutex_unlock(&modelMtx);
        return EXIT_FAILURE;
    }

    for (int i = 0; i < yoloModel.outputCount; i++) {
        int stride = 8 << i;
        int gridW = yoloModel.width / stride;
        int gridH = yoloModel.height / stride;
        int d1 = (int)yoloModel.outputAttr[i].dims[1];
        int d2 = (int)yoloModel.outputAttr[i].dims[2];

        outputs[i] = (const int8_t *)yoloModel.outputMem[i]->virt_addr;
        zp[i] = yoloModel.outputAttr[i].zp;
        scale[i] = yoloModel.outputAttr[i].scale;

        /* Native NHWC attributes have been observed both as [1,W,H,C] and
         * [1,H,W,C]; the grid dimensions identify the orientation. */
        if (d1 == gridW && d2 == gridH) {
            dims[i][0] = 1;
            dims[i][1] = d1;
            dims[i][2] = d2;
        } else {
            dims[i][0] = 1;
            dims[i][1] = d2;
            dims[i][2] = d1;
        }
        dims[i][3] = yoloModel.outputAttr[i].dims[3];
    }

    yolo_postprocess(outputs, (const int (*)[4])dims,
        zp, scale, yolo_anchors, yoloModel.classes,
        yoloModel.width, yoloModel.height,
        (float)app_config.yolo_conf_threshold,
        (float)app_config.yolo_nms_threshold, result);

    pthread_mutex_unlock(&modelMtx);
    return EXIT_SUCCESS;
}

static void yolo_event_finalize(void)
{
    if (activeEvent < 0) return;

    pthread_mutex_lock(&senderMtx);
    eventSlots[activeEvent].state = YOLO_EVENT_READY;
    pthread_cond_signal(&senderCond);
    pthread_mutex_unlock(&senderMtx);

    activeEvent = -1;
}

static void yolo_event_start(void)
{
    yolo_event_slot *slot = NULL;
    unsigned long long now = yolo_now_ms();
    int pre;

    pthread_mutex_lock(&senderMtx);
    for (int i = 0; i < 2; i++) {
        if (eventSlots[i].state == YOLO_EVENT_FREE) {
            slot = &eventSlots[i];
            slot->state = YOLO_EVENT_FILLING;
            activeEvent = i;
            break;
        }
    }
    pthread_mutex_unlock(&senderMtx);

    if (!slot) {
        HAL_WARNING("yolo", "Event queue is full; dropping the new event\n");
        lastTriggerMs = now;
        return;
    }

    slot->count = 0;
    pre = preFrames < ringCount ? preFrames : ringCount;
    for (int i = 0; i < pre; i++) {
        int index = (ringNext - pre + i + ringCapacity) % ringCapacity;
        memcpy(slot->frames + (size_t)slot->count * frameBytes,
            ring[index].data, frameBytes);
        slot->ts[slot->count] = ring[index].ts;
        slot->count++;
    }

    activeEventEnd = now + eventPostMs;
    lastTriggerMs = now;
    eventSequence++;

    {
        char text[64];
        time_t wallClock = time(NULL);
        struct tm tm_buf, *tm_info = localtime_r(&wallClock, &tm_buf);
        snprintf(text, sizeof(text), "YOLO %02d:%02d:%02d",
            tm_info ? tm_info->tm_hour : 0,
            tm_info ? tm_info->tm_min : 0,
            tm_info ? tm_info->tm_sec : 0);
        yolo_osd(text);
        osdShownMs = now;
        osdOwned = 1;
    }

    HAL_INFO("yolo", "Event %llu: %d frame(s) of pre-roll\n",
        eventSequence, slot->count);
}

static void *yolo_capture_thread(void *arg)
{
    unsigned long long lastInfer = 0;
    unsigned long long inferInterval;
    yolo_result result;

    (void)arg;

    inferInterval = 1000 / (app_config.yolo_fps ? app_config.yolo_fps : 1);

    while (keepRunning && yoloRunning) {
        rk_common_frame_info frame;
        unsigned long long now;

        memset(&frame, 0, sizeof(frame));
        if (rk_yolo_source_frame(&frame, 200)) continue;

        yolo_ring_push(&frame);
        rk_yolo_source_release(&frame);

        if (!ringCount) continue;
        now = yolo_now_ms();

        if (osdOwned && now - osdShownMs > 10000) {
            yolo_osd("");
            osdOwned = 0;
        }

        if (activeEvent >= 0) {
            yolo_event_slot *slot = &eventSlots[activeEvent];
            int last = (ringNext - 1 + ringCapacity) % ringCapacity;

            if (slot->count < eventMaxFrames) {
                memcpy(slot->frames + (size_t)slot->count * frameBytes,
                    ring[last].data, frameBytes);
                slot->ts[slot->count] = ring[last].ts;
                slot->count++;
            }
            if (now >= activeEventEnd ||
                slot->count >= eventMaxFrames)
                yolo_event_finalize();
        }

        if (inferenceDisabled || now - lastInfer < inferInterval) continue;
        lastInfer = now;

        if (yolo_infer(&ring[(ringNext - 1 + ringCapacity) % ringCapacity],
                &result) || !result.count)
            continue;

        if (activeEvent >= 0) {
            unsigned long long extend = now + eventPostMs;
            if (extend > activeEventEnd) activeEventEnd = extend;
        } else if (now - lastTriggerMs >= YOLO_EVENT_COOLDOWN_MS) {
            yolo_event_start();
        }
    }

    if (osdOwned) yolo_osd("");
    return NULL;
}

static void yolo_send_event(int slotIndex)
{
    yolo_event_slot *slot = &eventSlots[slotIndex];
    char eventId[48];
    char modelName[96];
    const char *base;
    hal_jpegdata jpeg = {0};

    snprintf(eventId, sizeof(eventId), "%llu-%llu",
        (unsigned long long)time(NULL), eventSequence);
    base = strrchr(app_config.yolo_model, '/');
    snprintf(modelName, sizeof(modelName), "%s", base ? base + 1 :
        app_config.yolo_model);

    if (!app_config.http_post_enable || !*app_config.http_post_host) {
        HAL_INFO("yolo", "Event %s has %d frame(s); HTTP post is disabled\n",
            eventId, slot->count);
        goto done;
    }

    for (int i = 0; i < slot->count; i++) {
        if (rk_yolo_jpeg_encode(slot->frames + (size_t)i * frameBytes, &jpeg)) {
            HAL_WARNING("yolo", "Encoding event frame %d failed\n", i);
            break;
        }
        if (http_post_event(&jpeg, eventId, i, slot->count, slot->ts[i],
                "yolo", modelName)) {
            HAL_WARNING("yolo", "Sending event frame %d failed\n", i);
            break;
        }
    }

done:
    free(jpeg.data);
}

static void *yolo_sender_thread(void *arg)
{
    (void)arg;

    while (senderRunning) {
        int slotIndex = -1;

        pthread_mutex_lock(&senderMtx);
        while (senderRunning) {
            for (int i = 0; i < 2; i++) {
                if (eventSlots[i].state == YOLO_EVENT_READY) {
                    slotIndex = i;
                    break;
                }
            }
            if (slotIndex >= 0) break;
            pthread_cond_wait(&senderCond, &senderMtx);
        }
        pthread_mutex_unlock(&senderMtx);

        if (slotIndex < 0) break;

        yolo_send_event(slotIndex);

        pthread_mutex_lock(&senderMtx);
        eventSlots[slotIndex].state = YOLO_EVENT_FREE;
        pthread_mutex_unlock(&senderMtx);
    }

    return NULL;
}

int yolo_running(void)
{
    return yoloRunning;
}

int yolo_start(void)
{
    char error[192];
    pthread_attr_t attr;
    int fps = app_config.yolo_fps ? (int)app_config.yolo_fps : 1;

    if (yoloRunning) return EXIT_SUCCESS;
    if (!app_config.yolo_enable) return EXIT_FAILURE;
    if (!rk_rknn_runtime_available((int)app_config.yolo_runtime)) {
        HAL_WARNING("yolo", "RKNN %u.x runtime is not available\n",
            app_config.yolo_runtime);
        return EXIT_FAILURE;
    }

    frameWidth = (int)app_config.yolo_input_width;
    frameHeight = (int)app_config.yolo_input_height;
    frameBytes = frameWidth * frameHeight * 3 / 2;
    if (frameWidth <= 0 || frameHeight <= 0) return EXIT_FAILURE;

    eventMaxFrames = (int)app_config.yolo_max_frames;
    eventPostMs = (unsigned long long)app_config.yolo_post_seconds * 1000;
    preFrames = (int)app_config.yolo_pre_seconds * fps;
    if (preFrames > eventMaxFrames)
        preFrames = eventMaxFrames;

    ringCapacity = eventMaxFrames;
    if (ringCapacity < preFrames + 1) ringCapacity = preFrames + 1;
    if (ringCapacity > 64) ringCapacity = 64;

    if (yolo_model_open(app_config.yolo_model, &yoloModel, 1,
            (int)app_config.yolo_runtime, error, sizeof(error))) {
        HAL_WARNING("yolo", "Model unavailable: %s\n", error);
        return EXIT_FAILURE;
    }

    ring = calloc(ringCapacity, sizeof(*ring));
    if (!ring) goto fail;
    for (int i = 0; i < 2; i++) {
        eventSlots[i].frames = calloc((size_t)eventMaxFrames, frameBytes);
        eventSlots[i].state = YOLO_EVENT_FREE;
        if (!eventSlots[i].frames) goto fail;
    }

    if (rk_yolo_source_open((rk_yolo_source)app_config.yolo_source,
            frameWidth, frameHeight)) {
        HAL_WARNING("yolo", "Frame source could not be opened\n");
        goto fail;
    }
    if (rk_yolo_jpeg_open(frameWidth, frameHeight, 80)) {
        HAL_WARNING("yolo", "Event JPEG encoder is unavailable\n");
        goto fail;
    }

    ringCount = 0;
    ringNext = 0;
    activeEvent = -1;
    lastTriggerMs = 0;
    eventSequence = 0;
    consecutiveFaults = 0;
    inferenceDisabled = 0;
    yoloRunning = 1;
    senderRunning = 1;

    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 256 * 1024);
    if (pthread_create(&captureThread, &attr, yolo_capture_thread, NULL)) {
        pthread_attr_destroy(&attr);
        HAL_DANGER("yolo", "Starting the capture thread failed\n");
        goto fail;
    }
    captureStarted = 1;
    if (pthread_create(&senderThread, &attr, yolo_sender_thread, NULL)) {
        pthread_attr_destroy(&attr);
        HAL_DANGER("yolo", "Starting the sender thread failed\n");
        yoloRunning = 0;
        pthread_join(captureThread, NULL);
        captureStarted = 0;
        goto fail;
    }
    senderStarted = 1;
    pthread_attr_destroy(&attr);

    HAL_INFO("yolo", "Detection started at %d fps\n", fps);
    return EXIT_SUCCESS;

fail:
    senderRunning = 0;
    yoloRunning = 0;
    rk_yolo_jpeg_close();
    rk_yolo_source_close();
    pthread_mutex_lock(&modelMtx);
    yolo_model_destroy(&yoloModel);
    pthread_mutex_unlock(&modelMtx);
    for (int i = 0; i < 2; i++) {
        free(eventSlots[i].frames);
        eventSlots[i].frames = NULL;
    }
    if (ring) {
        for (int i = 0; i < ringCapacity; i++) {
            if (ring[i].data) munmap(ring[i].data, frameBytes);
            if (ring[i].blk) rk_mb.fnFree(ring[i].blk);
        }
        free(ring);
        ring = NULL;
    }
    return EXIT_FAILURE;
}

void yolo_stop(void)
{
    if (!yoloRunning && !ring) return;

    yoloRunning = 0;
    senderRunning = 0;
    pthread_mutex_lock(&senderMtx);
    pthread_cond_broadcast(&senderCond);
    pthread_mutex_unlock(&senderMtx);

    if (captureStarted) {
        pthread_join(captureThread, NULL);
        captureStarted = 0;
    }
    if (senderStarted) {
        pthread_join(senderThread, NULL);
        senderStarted = 0;
    }

    rk_yolo_jpeg_close();
    rk_yolo_source_close();

    pthread_mutex_lock(&modelMtx);
    yolo_model_destroy(&yoloModel);
    pthread_mutex_unlock(&modelMtx);

    for (int i = 0; i < 2; i++) {
        free(eventSlots[i].frames);
        eventSlots[i].frames = NULL;
    }
    if (ring) {
        for (int i = 0; i < ringCapacity; i++) {
            if (ring[i].data) munmap(ring[i].data, frameBytes);
            if (ring[i].blk) rk_mb.fnFree(ring[i].blk);
        }
        free(ring);
        ring = NULL;
    }
    ringCount = 0;
    activeEvent = -1;
}

#endif