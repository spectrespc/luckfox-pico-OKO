#pragma once

/*
 * YOLOv5 post-processing for the RV1106 zero-copy RKNN output layout.
 *
 * The module is deliberately free of RKNN and HAL headers so it can be
 * compiled and exercised by a host test.  Detection boxes are normalised to
 * the 0..10000 range of the model input, which keeps them independent of the
 * source frame size.
 */

#include <stdint.h>

#define YOLO_MAX_DETECTIONS 64

typedef struct {
    int class_id;
    float score;
    int x, y, w, h;     /* normalised to 0..10000 */
} yolo_detection;

typedef struct {
    int count;
    yolo_detection det[YOLO_MAX_DETECTIONS];
} yolo_result;

/*
 * Decode the three YOLOv5 branches produced by an INT8 RV1106 model.
 *
 * outputs[i]  - int8 tensor data of branch i, NHWC layout as returned by
 *               RKNN_QUERY_NATIVE_NHWC_OUTPUT_ATTR.  The RV1106 runtime
 *               interleaves the channels inside every grid cell and does not
 *               pad them to 16 (align_c = 3 * (5 + classes), matching the
 *               v1.6.0 vendor demo); an NC1HWC2 tensor must not be passed
 *               here.  Offset: ((h * grid_w + w) * 3 + anchor) * (5 + classes).
 * dims[i]     - tensor dimensions {1, grid_w, grid_h, channels}; channels
 *               must be 3 * (5 + classes).
 * zp/scale[i] - quantisation parameters of branch i.
 * anchors     - three pairs of anchors per branch, 3 * 6 values.
 * classes     - number of model classes (80 for COCO).
 * model_w/h   - model input size in pixels.
 * conf/nms    - score and IoU thresholds.
 *
 * Returns the number of detections stored in result (0 on invalid input).
 * Detections are sorted by descending score and limited to
 * YOLO_MAX_DETECTIONS.
 */
int yolo_postprocess(const int8_t *const outputs[3], const int dims[3][4],
    const int32_t zp[3], const float scale[3], const int anchors[3][6],
    int classes, int model_w, int model_h,
    float conf_threshold, float nms_threshold, yolo_result *result);