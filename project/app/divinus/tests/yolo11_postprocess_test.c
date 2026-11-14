/*
 * Host test for the YOLO11 RKOPT post-processing.
 *
 * The expected detections come from the reference decoder of the vendor
 * python demo (rknn_model_zoo examples/yolo11, Apache-2.0) run on
 * ultralytics bus.jpg with the 640x384 RKOPT export: three persons and one
 * bus.  The int8 tensors are synthesised so that the DFL expectation encodes
 * exactly those boxes, which exercises the layout, the score-sum filter and
 * the shared NMS without an NPU.
 *
 * Run:
 *   gcc -std=c99 -Wall -Wextra -O2 -Iproject/app/divinus/source/src/analytics \
 *       -o /tmp/yolo11_postprocess_test \
 *       project/app/divinus/tests/yolo11_postprocess_test.c \
 *       project/app/divinus/source/src/analytics/yolo_postprocess.c -lm &&
 *       /tmp/yolo11_postprocess_test
 *
 * Expected output:
 *   ok bus.jpg golden (person 0.851, bus 0.824, person 0.800, person 0.733)
 *   ok score-sum branch filters a cell
 *   ok branch 0 decode (class 7)
 *   ok invalid class tensor rejected
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yolo_postprocess.h"

#define MODEL_W 640
#define MODEL_H 384
#define CLASSES 80
#define DFL 16
#define BOX_CH (4 * DFL)

static int8_t *box[3];
static int8_t *score[3];
static int8_t *sum[3];
static const int8_t *outputs[9];
static int dims[9][4];
static int32_t zp[9];
static float scale[9];

static int grid_w[3] = { 80, 40, 20 };
static int grid_h[3] = { 48, 24, 12 };

/* Affine parameters used by the synthetic tensors. */
#define BOX_SCALE 0.25f
#define SCORE_SCALE (1.0f / 255.0f)
#define SCORE_ZP (-128)

static void layout(void)
{
    for (int i = 0; i < 3; i++) {
        outputs[i * 3 + 0] = box[i];
        outputs[i * 3 + 1] = score[i];
        outputs[i * 3 + 2] = sum[i];
        dims[i * 3 + 0][0] = 1;
        dims[i * 3 + 0][1] = grid_w[i];
        dims[i * 3 + 0][2] = grid_h[i];
        dims[i * 3 + 0][3] = BOX_CH;
        dims[i * 3 + 1][0] = 1;
        dims[i * 3 + 1][1] = grid_w[i];
        dims[i * 3 + 1][2] = grid_h[i];
        dims[i * 3 + 1][3] = CLASSES;
        dims[i * 3 + 2][0] = 1;
        dims[i * 3 + 2][1] = grid_w[i];
        dims[i * 3 + 2][2] = grid_h[i];
        dims[i * 3 + 2][3] = 1;
        zp[i * 3 + 0] = 0;
        zp[i * 3 + 1] = SCORE_ZP;
        zp[i * 3 + 2] = SCORE_ZP;
        scale[i * 3 + 0] = BOX_SCALE;
        scale[i * 3 + 1] = SCORE_SCALE;
        scale[i * 3 + 2] = SCORE_SCALE;
    }
}

static void clear(void)
{
    for (int i = 0; i < 3; i++) {
        size_t b = (size_t)grid_w[i] * grid_h[i] * BOX_CH;

        memset(box[i], 0, b);
        for (size_t k = 0; k < b; k++)
            box[i][k] = (int8_t)lrintf(-30.0f / BOX_SCALE);
        for (size_t k = 0; k < (size_t)grid_w[i] * grid_h[i] * CLASSES; k++)
            score[i][k] = (int8_t)(-128);
        memset(sum[i], (int8_t)(-128),
            (size_t)grid_w[i] * grid_h[i]);
    }
    layout();
}

/* Encodes dfl expectation `value` (in grid cells) into the 16 bins of one
 * box edge.  Two adjacent bins carry the fractional part. */
static void set_edge(int branch, int cell, int edge, float value)
{
    int b = (int)floorf(value);
    float frac = value - (float)b;
    int base = cell * BOX_CH + edge * DFL;

    if (b < 0) b = 0;
    if (b > DFL - 1) b = DFL - 1;

    for (int k = 0; k < DFL; k++)
        box[branch][base + k] = (int8_t)lrintf(-30.0f / BOX_SCALE);

    if (frac > 0.0001f && frac < 0.9999f && b + 1 < DFL) {
        float logit = logf(frac / (1.0f - frac));

        box[branch][base + b] = (int8_t)lrintf(0.0f);
        box[branch][base + b + 1] = (int8_t)lrintf(logit / BOX_SCALE);
    } else {
        box[branch][base + b] = 0;
    }
}

static void set_cell(int branch, int i, int j, int cls, float conf,
    float left, float top, float right, float bottom)
{
    int cell = i * grid_w[branch] + j;
    int s32 = (int)lrintf(conf * 255.0f - 128.0f);

    if (s32 < -128) s32 = -128;
    if (s32 > 127) s32 = 127;
    score[branch][cell * CLASSES + cls] = (int8_t)s32;
    sum[branch][cell] = 127;
    set_edge(branch, cell, 0, left);
    set_edge(branch, cell, 1, top);
    set_edge(branch, cell, 2, right);
    set_edge(branch, cell, 3, bottom);
}

static int near(int value, float expected, float tolerance)
{
    return fabsf((float)value - expected) <= tolerance;
}

static float norm_x(float pixels) { return pixels / MODEL_W * 10000.0f; }
static float norm_y(float pixels) { return pixels / MODEL_H * 10000.0f; }

static int golden_test(void)
{
    yolo_result result;

    /* bus.jpg detections from the vendor python demo at 640x384. */
    static const struct {
        int branch, i, j, cls;
        float conf, x1, y1, x2, y2;
    } golden[] = {
        { 2, 7, 7, 0, 0.853f, 193.5f, 139.8f, 261.9f, 322.8f },
        { 2, 5, 10, 5, 0.823f, 178.5f, 82.2f, 462.1f, 266.2f },
        { 2, 7, 8, 0, 0.798f, 254.6f, 144.4f, 300.5f, 306.1f },
        { 2, 7, 13, 0, 0.732f, 413.9f, 147.0f, 464.6f, 309.7f }
    };
    float stride = 32.0f;   /* branch 2 of 640x384 */

    clear();
    for (size_t k = 0; k < sizeof(golden) / sizeof(golden[0]); k++) {
        float cx = ((float)golden[k].j + 0.5f) * stride;
        float cy = ((float)golden[k].i + 0.5f) * stride;

        set_cell(golden[k].branch, golden[k].i, golden[k].j, golden[k].cls,
            golden[k].conf,
            (cx - golden[k].x1) / stride, (cy - golden[k].y1) / stride,
            (golden[k].x2 - cx) / stride, (golden[k].y2 - cy) / stride);
    }

    int count = yolo11_postprocess(outputs, dims, zp, scale, CLASSES,
        MODEL_W, MODEL_H, 0.25f, 0.45f, &result);
    if (count != 4) {
        printf("FAIL golden: %d detections, expected 4\n", count);
        return 1;
    }
    for (int k = 0; k < 4; k++) {
        const yolo_detection *d = &result.det[k];

        if (d->class_id != golden[k].cls ||
            !near(d->x, norm_x(golden[k].x1), 40.0f) ||
            !near(d->y, norm_y(golden[k].y1), 40.0f) ||
            !near(d->w, norm_x(golden[k].x2 - golden[k].x1), 40.0f) ||
            !near(d->h, norm_y(golden[k].y2 - golden[k].y1), 40.0f)) {
            printf("FAIL golden det %d: cls %d score %.3f box %d,%d,%d,%d\n",
                k, d->class_id, d->score, d->x, d->y, d->w, d->h);
            return 1;
        }
        if (fabsf(d->score - golden[k].conf) > 0.01f) {
            printf("FAIL golden det %d score %.3f expected %.3f\n", k,
                d->score, golden[k].conf);
            return 1;
        }
    }
    printf("ok bus.jpg golden (person %.3f, bus %.3f, person %.3f, person %.3f)\n",
        result.det[0].score, result.det[1].score, result.det[2].score,
        result.det[3].score);
    return 0;
}

static int score_sum_test(void)
{
    yolo_result result;
    int count;

    clear();
    /* Confident class scores but a zero score-sum must be filtered out. */
    set_cell(1, 12, 20, 2, 0.90f, 1.0f, 1.0f, 1.0f, 1.0f);
    sum[1][12 * grid_w[1] + 20] = -128;

    count = yolo11_postprocess(outputs, dims, zp, scale, CLASSES,
        MODEL_W, MODEL_H, 0.25f, 0.45f, &result);
    if (count != 0) {
        printf("FAIL score-sum filter: %d detections\n", count);
        return 1;
    }
    printf("ok score-sum branch filters a cell\n");
    return 0;
}

static int branch0_test(void)
{
    yolo_result result;
    int count;

    clear();
    /* Cell (24, 40) centre is (326, 196); the four DFL distances give the
     * box 306..330 x 186..204. */
    set_cell(0, 24, 40, 7, 0.60f, 20.0f / 8.0f, 10.0f / 8.0f,
        4.0f / 8.0f, 8.0f / 8.0f);

    count = yolo11_postprocess(outputs, dims, zp, scale, CLASSES,
        MODEL_W, MODEL_H, 0.25f, 0.45f, &result);
    if (count != 1 || result.det[0].class_id != 7 ||
        !near(result.det[0].x, norm_x(306.0f), 40.0f) ||
        !near(result.det[0].y, norm_y(186.0f), 40.0f) ||
        !near(result.det[0].w, norm_x(24.0f), 40.0f) ||
        !near(result.det[0].h, norm_y(18.0f), 40.0f)) {
        printf("FAIL branch 0 decode: %d detections\n", count);
        return 1;
    }
    printf("ok branch 0 decode (class 7)\n");
    return 0;
}

static int invalid_test(void)
{
    yolo_result result;
    int count;

    clear();
    dims[1][3] = CLASSES - 1;
    count = yolo11_postprocess(outputs, dims, zp, scale, CLASSES,
        MODEL_W, MODEL_H, 0.25f, 0.45f, &result);
    if (count != 0) {
        printf("FAIL invalid class tensor accepted\n");
        return 1;
    }
    printf("ok invalid class tensor rejected\n");
    return 0;
}

int main(void)
{
    int ret = 0;

    for (int i = 0; i < 3; i++) {
        size_t cells = (size_t)grid_w[i] * grid_h[i];

        box[i] = calloc(cells * BOX_CH, 1);
        score[i] = calloc(cells * CLASSES, 1);
        sum[i] = calloc(cells, 1);
        if (!box[i] || !score[i] || !sum[i]) return 2;
    }
    layout();

    ret |= golden_test();
    ret |= score_sum_test();
    ret |= branch0_test();
    ret |= invalid_test();
    return ret;
}
