/*
 * Host test for the NPU YOLOv5 post-processing.
 *
 * The RV1106 native NHWC layout is exercised with a synthetic tensor: one
 * strong detection in branch 0 must survive decoding and NMS, invalid
 * dimensions must be rejected.
 *
 * Run:
 *   gcc -std=c99 -Wall -Wextra -O2 -Iproject/app/divinus/source/src/analytics \
 *       -o /tmp/yolo_postprocess_test \
 *       project/app/divinus/tests/yolo_postprocess_test.c \
 *       project/app/divinus/source/src/analytics/yolo_postprocess.c -lm &&
 *       /tmp/yolo_postprocess_test
 *
 * Expected output:
 *   ok one detection (class 1, score 1.613)
 *   ok invalid channels rejected
 *   ok 80-class interleaved decode (class 79, offset 21080)
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yolo_postprocess.h"

static const int anchors[3][6] = {
    {10, 13, 16, 30, 33, 23},
    {30, 61, 62, 45, 59, 119},
    {116, 90, 156, 198, 373, 326}
};

int main(void)
{
    int classes = 2;
    int grid_w = 80, grid_h = 48;
    int channels = 3 * (5 + classes);
    size_t size = (size_t)grid_w * grid_h * channels;
    int8_t *branch0 = calloc(size, 1);
    int8_t *branch1 = calloc((size_t)40 * 24 * channels, 1);
    int8_t *branch2 = calloc((size_t)20 * 12 * channels, 1);
    const int8_t *outputs[3] = { branch0, branch1, branch2 };
    int dims[3][4] = {
        { 1, grid_w, grid_h, channels },
        { 1, 40, 24, channels },
        { 1, 20, 12, channels }
    };
    int32_t zp[3] = { 0, 0, 0 };
    float scale[3] = { 0.01f, 0.01f, 0.01f };
    yolo_result result;

    if (!branch0 || !branch1 || !branch2) return 2;

    /* h=0, w=0, anchor 0: confidence and the class-1 score are maximal. */
    branch0[0] = 10;   /* box x: (0.1*2-0.5) * 8 = -2.4 */
    branch0[1] = 20;   /* box y: (0.2*2-0.5) * 8 = -0.8 */
    branch0[2] = 30;   /* box w: (0.6)^2 * 10 = 3.6 */
    branch0[3] = 30;   /* box h: (0.6)^2 * 13 = 4.68 */
    branch0[4] = 127;  /* object confidence 1.27 */
    branch0[5] = 0;    /* class 0 */
    branch0[6] = 127;  /* class 1: 1.27 * 1.27 = 1.6129 */

    int count = yolo_postprocess(outputs, (const int (*)[4])dims, zp, scale,
        anchors, classes, 640, 384, 0.25f, 0.45f, &result);
    if (count != 1) {
        printf("FAIL expected one detection, got %d\n", count);
        return 1;
    }
    if (result.det[0].class_id != 1 ||
        fabsf(result.det[0].score - 1.6129f) > 0.01f) {
        printf("FAIL class %d score %.4f\n", result.det[0].class_id,
            result.det[0].score);
        return 1;
    }
    printf("ok one detection (class %d, score %.3f)\n", result.det[0].class_id,
        result.det[0].score);

    dims[0][3] = channels - 1;
    count = yolo_postprocess(outputs, (const int (*)[4])dims, zp, scale,
        anchors, classes, 640, 384, 0.25f, 0.45f, &result);
    if (count != 0 || result.count != 0) {
        printf("FAIL invalid channel count was accepted\n");
        return 1;
    }
    printf("ok invalid channels rejected\n");

    /* 80 COCO classes give 255 channels, i.e. C % 16 != 0.  The native NHWC
     * layout is interleaved without 16-channel padding, so a detection placed
     * at a non-zero grid cell must be decoded exactly; an implementation that
     * silently padded channels would look at another offset and find nothing. */
    {
        int coco = 80;
        int coco_channels = 3 * (5 + coco);
        int cw = 80, ch = 48;
        int8_t *dense = calloc((size_t)cw * ch * coco_channels, 1);
        int8_t *sparse1 = calloc((size_t)40 * 24 * coco_channels, 1);
        int8_t *sparse2 = calloc((size_t)20 * 12 * coco_channels, 1);
        const int8_t *coco_outputs[3] = { dense, sparse1, sparse2 };
        int coco_dims[3][4] = {
            { 1, cw, ch, coco_channels },
            { 1, 40, 24, coco_channels },
            { 1, 20, 12, coco_channels }
        };
        int h = 1, w = 2, a = 2;
        int offset = (h * cw + w) * coco_channels + a * (5 + coco);

        if (!dense || !sparse1 || !sparse2) return 2;

        dense[offset + 4] = 100;          /* object confidence 1.0 */
        dense[offset + 5 + 79] = 100;     /* class 79, also 1.0 */

        count = yolo_postprocess(coco_outputs, (const int (*)[4])coco_dims,
            zp, scale, anchors, coco, 640, 384, 0.25f, 0.45f, &result);
        if (count != 1 || result.det[0].class_id != 79 ||
            fabsf(result.det[0].score - 1.0f) > 0.01f) {
            printf("FAIL 80-class interleaved decode: count %d, class %d, "
                "score %.4f\n", count, count ? result.det[0].class_id : -1,
                count ? result.det[0].score : 0.0f);
            return 1;
        }
        printf("ok 80-class interleaved decode (class 79, offset %d)\n",
            offset);

        free(dense);
        free(sparse1);
        free(sparse2);
    }

    free(branch0);
    free(branch1);
    free(branch2);
    return 0;
}