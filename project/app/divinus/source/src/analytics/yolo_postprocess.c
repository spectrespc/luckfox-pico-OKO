#include <math.h>
#include <stdint.h>
#include <string.h>
#include "yolo_postprocess.h"

/* Bounded candidate pool; the public result is capped at YOLO_MAX_DETECTIONS. */
#define YOLO_MAX_CANDIDATES 256

/* Dequantize int8 value to float: (q - zp) * scale */
static inline float deqnt_affine_to_f32(int8_t qnt, float scale, int32_t zp) {
    return (float)(qnt - zp) * scale;
}

/* Clamp value to [0, 10000] range */
static inline int clamp_0_10000(float x) {
    int val = (int)roundf(x);
    if (val < 0) val = 0;
    if (val > 10000) val = 10000;
    return val;
}

/* Calculate IoU overlap between two boxes with +1.0 in width/height */
static float CalculateOverlap(float x0, float y0, float x1, float y1,
                              float x2, float y2, float x3, float y3) {
    float square0 = (x1 - x0 + 1.0f) * (y1 - y0 + 1.0f);
    float square1 = (x3 - x2 + 1.0f) * (y3 - y2 + 1.0f);

    float x = fmaxf(x0, x2);
    float y = fmaxf(y0, y2);
    float w = fminf(x1, x3) - x + 1.0f;
    float h = fminf(y1, y3) - y + 1.0f;

    if (w <= 0.0f || h <= 0.0f)
        return 0.0f;

    float area = w * h;
    return area / (square0 + square1 - area);
}

/* Normalise the raw pixel candidates and apply per-class NMS.  Shared by the
 * YOLOv5 and YOLO11 decoders so both architectures use identical thresholds
 * and result ordering. */
static int yolo_finish_candidates(const yolo_detection *candidates,
    const float *candidate_x1, const float *candidate_y1,
    const float *candidate_x2, const float *candidate_y2, int total_candidates,
    int model_w, int model_h, float nms_threshold, yolo_result *result)
{
    yolo_detection nms_dets[YOLO_MAX_CANDIDATES];
    int order[YOLO_MAX_CANDIDATES];
    int nms_count = 0;
    int keep[YOLO_MAX_CANDIDATES];
    int result_count = 0;

    /* Sort candidate indices by descending score.  The coordinate arrays are
     * indexed through `order`, so a box always stays paired with its score. */
    for (int i = 0; i < total_candidates; i++)
        order[i] = i;
    for (int i = 1; i < total_candidates; i++) {
        int key = order[i];
        int j = i - 1;

        while (j >= 0 && candidates[order[j]].score < candidates[key].score) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = key;
    }

    for (int pos = 0; pos < total_candidates; pos++) {
        int i = order[pos];
        float nx1 = candidate_x1[i];
        float ny1 = candidate_y1[i];
        float nx2 = candidate_x2[i];
        float ny2 = candidate_y2[i];

        nms_dets[nms_count].class_id = candidates[i].class_id;
        nms_dets[nms_count].score = candidates[i].score;
        nms_dets[nms_count].x = clamp_0_10000(nx1 / (float)model_w * 10000.0f);
        nms_dets[nms_count].y = clamp_0_10000(ny1 / (float)model_h * 10000.0f);
        nms_dets[nms_count].w = clamp_0_10000((nx2 - nx1) / (float)model_w * 10000.0f);
        nms_dets[nms_count].h = clamp_0_10000((ny2 - ny1) / (float)model_h * 10000.0f);
        nms_count++;
    }

    /* Perform NMS: for each class, keep only detections with IoU <= threshold */
    memset(keep, 0, sizeof(keep));

    for (int i = 0; i < nms_count; i++) {
        if (keep[i]) continue;

        for (int j = i + 1; j < nms_count; j++) {
            float iou;

            if (keep[j]) continue;
            if (nms_dets[i].class_id != nms_dets[j].class_id) continue;

            iou = CalculateOverlap(
                (float)nms_dets[i].x, (float)nms_dets[i].y,
                (float)nms_dets[i].x + (float)nms_dets[i].w,
                (float)nms_dets[i].y + (float)nms_dets[i].h,
                (float)nms_dets[j].x, (float)nms_dets[j].y,
                (float)nms_dets[j].x + (float)nms_dets[j].w,
                (float)nms_dets[j].y + (float)nms_dets[j].h);

            if (iou > nms_threshold) keep[j] = 1;
        }
    }

    for (int i = 0; i < nms_count && result_count < YOLO_MAX_DETECTIONS; i++) {
        if (keep[i]) continue;
        result->det[result_count++] = nms_dets[i];
    }

    result->count = result_count;
    return result_count;
}

/*
 * Decode YOLOv5 INT8 outputs and produce detections.
 * Implements the RV1106 NHWC layout per anchor specification.
 */
int yolo_postprocess(const int8_t *const outputs[3], const int dims[3][4],
                     const int32_t zp[3], const float scale[3],
                     const int anchors[3][6], int classes, int model_w, int model_h,
                     float conf_threshold, float nms_threshold, yolo_result *result) {
    int branch;
    int total_candidates = 0;
    /* The raw decode can produce far more boxes than the public result
     * limit; keep a bounded candidate pool for NMS. */
    yolo_detection candidates[YOLO_MAX_CANDIDATES];
    float candidate_scores[YOLO_MAX_CANDIDATES];
    float candidate_x1[YOLO_MAX_CANDIDATES];
    float candidate_y1[YOLO_MAX_CANDIDATES];
    float candidate_x2[YOLO_MAX_CANDIDATES];
    float candidate_y2[YOLO_MAX_CANDIDATES];

    /* Validate input parameters */
    if (outputs == NULL || dims == NULL || zp == NULL || scale == NULL ||
        anchors == NULL || result == NULL) {
        result->count = 0;
        return 0;
    }

    if (classes < 1 || classes > 255) {
        result->count = 0;
        return 0;
    }

    memset(result, 0, sizeof(yolo_result));

    /* Process each of the 3 YOLO branches */
    for (branch = 0; branch < 3; branch++) {
        const int8_t *output = outputs[branch];
        const int *dims_branch = dims[branch];
        int32_t branch_zp = zp[branch];
        float branch_scale = scale[branch];

        /* Validate dimensions */
        if (output == NULL || dims_branch == NULL) {
            continue;
        }

        int grid_w = dims_branch[1];
        int grid_h = dims_branch[2];
        int channels = dims_branch[3];

        /* Validate channel count: must be 3 * (5 + classes) */
        if (channels != 3 * (5 + classes)) {
            continue;
        }

        if (grid_w <= 0 || grid_h <= 0) {
            continue;
        }

        int align_c = 3 * (5 + classes);  /* = channels, no 16-px padding */
        int boxes_per_grid = 5 + classes;

        /* Compute stride for this branch */
        float stride_w = (float)model_w / (float)grid_w;
        float stride_h = (float)model_h / (float)grid_h;

        /* Process each grid cell and anchor */
        for (int h = 0; h < grid_h; h++) {
            for (int w = 0; w < grid_w; w++) {
                for (int a = 0; a < 3; a++) {
                    /* Compute offset for this anchor in NHWC layout */
                    int hw_offset = (h * grid_w + w) * align_c + a * boxes_per_grid;

                    /* Get box confidence (element 4 of anchor block) */
                    int8_t box_conf_qnt = output[hw_offset + 4];
                    /* The RKNN YOLOv5 graph already ends with sigmoid;
                     * the reference post-processing dequantises directly. */
                    float box_conf = deqnt_affine_to_f32(box_conf_qnt, branch_scale, branch_zp);

                    /* Get best class score (elements 5 to 5+classes-1) */
                    float best_class_score = 0.0f;
                    int best_class_id = 0;
                    for (int c = 0; c < classes; c++) {
                        int8_t class_score_qnt = output[hw_offset + 5 + c];
                        float class_score = deqnt_affine_to_f32(class_score_qnt, branch_scale, branch_zp);
                        if (class_score > best_class_score) {
                            best_class_score = class_score;
                            best_class_id = c;
                        }
                    }

                    /* Final decision: product of box_conf and best_class_score */
                    float final_score = box_conf * best_class_score;
                    if (final_score < conf_threshold) {
                        continue;
                    }

                    /* If we've reached max capacity, try to replace lowest score */
                    if (total_candidates >= YOLO_MAX_CANDIDATES) {
                        /* Find minimum score in candidates */
                        int min_idx = 0;
                        float min_score = candidate_scores[0];
                        for (int i = 1; i < total_candidates; i++) {
                            if (candidate_scores[i] < min_score) {
                                min_score = candidate_scores[i];
                                min_idx = i;
                            }
                        }
                        /* Only replace if current score is higher */
                        if (final_score > min_score) {
                            candidate_scores[min_idx] = candidate_scores[total_candidates - 1];
                            candidates[min_idx] = candidates[total_candidates - 1];
                            candidate_x1[min_idx] = candidate_x1[total_candidates - 1];
                            candidate_y1[min_idx] = candidate_y1[total_candidates - 1];
                            candidate_x2[min_idx] = candidate_x2[total_candidates - 1];
                            candidate_y2[min_idx] = candidate_y2[total_candidates - 1];
                            total_candidates--;
                        } else {
                            continue;
                        }
                    }

                    /* Decode box coordinates */
                    /* Box params: x, y, w, h are elements 0-3 */
                    int8_t box_x_qnt = output[hw_offset + 0];
                    int8_t box_y_qnt = output[hw_offset + 1];
                    int8_t box_w_qnt = output[hw_offset + 2];
                    int8_t box_h_qnt = output[hw_offset + 3];

                    float box_x = deqnt_affine_to_f32(box_x_qnt, branch_scale, branch_zp);
                    float box_y = deqnt_affine_to_f32(box_y_qnt, branch_scale, branch_zp);
                    float box_w = deqnt_affine_to_f32(box_w_qnt, branch_scale, branch_zp);
                    float box_h = deqnt_affine_to_f32(box_h_qnt, branch_scale, branch_zp);

                    /* Decode as in the reference: bx = (deq * 2 - 0.5 + w) * stride */
                    float bx = (box_x * 2.0f - 0.5f + (float)w) * stride_w;
                    float by = (box_y * 2.0f - 0.5f + (float)h) * stride_h;

                    /* bw = (deq * 2)^2 * anchor[2*a] */
                    float anchor_w = (float)anchors[branch][2 * a];
                    float anchor_h = (float)anchors[branch][2 * a + 1];
                    float bw = powf(box_w * 2.0f, 2.0f) * anchor_w;
                    float bh = powf(box_h * 2.0f, 2.0f) * anchor_h;

                    /* Convert to x1, y1, x2, y2 (center-based to corner-based) */
                    float x1 = bx - bw / 2.0f;
                    float y1 = by - bh / 2.0f;
                    float x2 = bx + bw / 2.0f;
                    float y2 = by + bh / 2.0f;

                    /* Store candidate detection */
                    candidates[total_candidates].class_id = best_class_id;
                    candidates[total_candidates].score = final_score;
                    candidates[total_candidates].x = (int)x1;
                    candidates[total_candidates].y = (int)y1;
                    candidates[total_candidates].w = (int)(x2 - x1);
                    candidates[total_candidates].h = (int)(y2 - y1);

                    candidate_scores[total_candidates] = final_score;
                    candidate_x1[total_candidates] = x1;
                    candidate_y1[total_candidates] = y1;
                    candidate_x2[total_candidates] = x2;
                    candidate_y2[total_candidates] = y2;

                    total_candidates++;
                }
            }
        }
    }

    return yolo_finish_candidates(candidates, candidate_x1, candidate_y1,
        candidate_x2, candidate_y2, total_candidates, model_w, model_h,
        nms_threshold, result);
}

/* -------------------------------------------------------------------------
 * YOLO11 (RKOPT export)
 *
 * Ported from the Apache-2.0 reference implementation in
 * airockchip/rknn_model_zoo (examples/yolo11/cpp/postprocess.cc,
 * Copyright (c) 2024 Rockchip Electronics Co., Ltd.).  The graph already
 * ends with sigmoid on the class scores, so only dequantisation and the DFL
 * expectation are left on the CPU.
 * ---------------------------------------------------------------------- */

#define YOLO11_DFL_LEN 16
#define YOLO11_OUTPUTS_PER_BRANCH 3

/* Softmax over one DFL bin group followed by the expectation value. */
static float yolo11_dfl_expectation(const float *bins, int len)
{
    float exponent[YOLO11_DFL_LEN];
    float sum = 0.0f;
    float weighted = 0.0f;

    if (len > YOLO11_DFL_LEN) len = YOLO11_DFL_LEN;
    for (int i = 0; i < len; i++) {
        exponent[i] = expf(bins[i]);
        sum += exponent[i];
    }
    if (sum <= 0.0f) return 0.0f;
    for (int i = 0; i < len; i++)
        weighted += exponent[i] / sum * (float)i;
    return weighted;
}

int yolo11_postprocess(const int8_t *const outputs[9], const int dims[9][4],
    const int32_t zp[9], const float scale[9], int classes,
    int model_w, int model_h, float conf_threshold, float nms_threshold,
    yolo_result *result)
{
    yolo_detection candidates[YOLO_MAX_CANDIDATES];
    float candidate_scores[YOLO_MAX_CANDIDATES];
    float candidate_x1[YOLO_MAX_CANDIDATES];
    float candidate_y1[YOLO_MAX_CANDIDATES];
    float candidate_x2[YOLO_MAX_CANDIDATES];
    float candidate_y2[YOLO_MAX_CANDIDATES];
    int total_candidates = 0;

    if (result) {
        result->count = 0;
        memset(result, 0, sizeof(*result));
    }
    if (!outputs || !dims || !zp || !scale || !result)
        return 0;
    if (classes < 1 || classes > 255 || model_w <= 0 || model_h <= 0)
        return 0;

    for (int branch = 0; branch < 3; branch++) {
        int base = branch * YOLO11_OUTPUTS_PER_BRANCH;
        const int8_t *box = outputs[base];
        const int8_t *scores = outputs[base + 1];
        const int8_t *scoreSum = outputs[base + 2];
        int boxChannels = dims[base][3];
        int gridW, gridH, dfl;
        int strideW, strideH;

        if (!box || !scores || !scoreSum) continue;
        if (boxChannels % 4 || boxChannels < 4)
            continue;
        dfl = boxChannels / 4;
        if (dfl < 1 || dfl > YOLO11_DFL_LEN)
            continue;
        /* All three tensors of a scale share one grid; the channel counts are
         * verified by the caller before the detector is activated. */
        if (dims[base + 1][1] != dims[base][1] ||
            dims[base + 1][2] != dims[base][2] ||
            dims[base + 2][1] != dims[base][1] ||
            dims[base + 2][2] != dims[base][2] ||
            dims[base + 1][3] != classes || dims[base + 2][3] != 1)
            continue;
        gridW = dims[base][1];
        gridH = dims[base][2];
        if (gridW <= 0 || gridH <= 0) continue;
        strideW = model_w / gridW;
        strideH = model_h / gridH;
        if (strideW <= 0 || strideH <= 0) continue;

        for (int i = 0; i < gridH; i++) {
            for (int j = 0; j < gridW; j++) {
                int cell = i * gridW + j;
                float sum = deqnt_affine_to_f32(scoreSum[cell],
                    scale[base + 2], zp[base + 2]);
                float best = 0.0f;
                int bestClass = 0;

                if (sum < conf_threshold) continue;

                for (int c = 0; c < classes; c++) {
                    float score = deqnt_affine_to_f32(
                        scores[cell * classes + c], scale[base + 1],
                        zp[base + 1]);
                    if (score > best && score >= conf_threshold) {
                        best = score;
                        bestClass = c;
                    }
                }
                if (best <= 0.0f) continue;

                {
                    float edges[4];
                    float x1, y1, x2, y2;

                    for (int e = 0; e < 4; e++) {
                        float bins[YOLO11_DFL_LEN];

                        for (int k = 0; k < dfl; k++)
                            bins[k] = deqnt_affine_to_f32(
                                box[cell * boxChannels + e * dfl + k],
                                scale[base], zp[base]);
                        edges[e] = yolo11_dfl_expectation(bins, dfl);
                    }

                    x1 = (-edges[0] + (float)j + 0.5f) * (float)strideW;
                    y1 = (-edges[1] + (float)i + 0.5f) * (float)strideH;
                    x2 = (edges[2] + (float)j + 0.5f) * (float)strideW;
                    y2 = (edges[3] + (float)i + 0.5f) * (float)strideH;

                    if (total_candidates >= YOLO_MAX_CANDIDATES) {
                        int minIndex = 0;
                        float minScore = candidate_scores[0];

                        for (int k = 1; k < total_candidates; k++) {
                            if (candidate_scores[k] < minScore) {
                                minScore = candidate_scores[k];
                                minIndex = k;
                            }
                        }
                        if (best <= minScore) continue;
                        total_candidates--;
                        candidates[minIndex] = candidates[total_candidates];
                        candidate_scores[minIndex] = candidate_scores[total_candidates];
                        candidate_x1[minIndex] = candidate_x1[total_candidates];
                        candidate_y1[minIndex] = candidate_y1[total_candidates];
                        candidate_x2[minIndex] = candidate_x2[total_candidates];
                        candidate_y2[minIndex] = candidate_y2[total_candidates];
                    }

                    candidates[total_candidates].class_id = bestClass;
                    candidates[total_candidates].score = best;
                    candidate_scores[total_candidates] = best;
                    candidate_x1[total_candidates] = x1;
                    candidate_y1[total_candidates] = y1;
                    candidate_x2[total_candidates] = x2;
                    candidate_y2[total_candidates] = y2;
                    total_candidates++;
                }
            }
        }
    }

    return yolo_finish_candidates(candidates, candidate_x1, candidate_y1,
        candidate_x2, candidate_y2, total_candidates, model_w, model_h,
        nms_threshold, result);
}
