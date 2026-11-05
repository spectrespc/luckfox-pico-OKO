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

/* Simple insertion sort for descending scores (max 256 entries) */
static void insertion_sort_desc(float *scores, yolo_detection *dets, int n) {
    int i, j;
    float key_score;
    yolo_detection key_det;

    for (i = 1; i < n; i++) {
        key_score = scores[i];
        key_det = dets[i];

        j = i - 1;
        while (j >= 0 && scores[j] < key_score) {
            scores[j + 1] = scores[j];
            dets[j + 1] = dets[j];
            j--;
        }
        scores[j + 1] = key_score;
        dets[j + 1] = key_det;
    }
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

    /* Sort all candidates by descending score */
    insertion_sort_desc(candidate_scores, candidates, total_candidates);

    /* Apply NMS per class using normalized coordinates */
    /* Build per-class NMS using the overlap calculation */
    yolo_detection nms_dets[YOLO_MAX_CANDIDATES];
    int nms_count = 0;

    for (int i = 0; i < total_candidates; i++) {
        /* Compute normalised coordinates: clamp to [0, 10000] */
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
    int keep[YOLO_MAX_CANDIDATES];
    memset(keep, 0, sizeof(keep));

    for (int i = 0; i < nms_count; i++) {
        if (keep[i]) continue;

        /* Compute IoU with all other detections of same class */
        for (int j = i + 1; j < nms_count; j++) {
            if (keep[j]) continue;
            if (nms_dets[i].class_id != nms_dets[j].class_id) continue;

            /* Compute IoU */
            float iou = CalculateOverlap(
                (float)nms_dets[i].x, (float)nms_dets[i].y,
                (float)nms_dets[i].x + (float)nms_dets[i].w,
                (float)nms_dets[i].y + (float)nms_dets[i].h,
                (float)nms_dets[j].x, (float)nms_dets[j].y,
                (float)nms_dets[j].x + (float)nms_dets[j].w,
                (float)nms_dets[j].y + (float)nms_dets[j].h
            );

            if (iou > nms_threshold) {
                keep[j] = 1;  /* Mark for removal */
            }
        }
    }

    /* Collect surviving detections (already sorted by score) */
    int result_count = 0;
    for (int i = 0; i < nms_count && result_count < YOLO_MAX_DETECTIONS; i++) {
        if (!keep[i]) {
            result->det[result_count].class_id = nms_dets[i].class_id;
            result->det[result_count].score = nms_dets[i].score;
            result->det[result_count].x = nms_dets[i].x;
            result->det[result_count].y = nms_dets[i].y;
            result->det[result_count].w = nms_dets[i].w;
            result->det[result_count].h = nms_dets[i].h;
            result_count++;
        }
    }

    result->count = result_count;
    return result_count;
}
