#pragma once

#include <stddef.h>

/* Metadata read back from a .rknn model. */
/* Detector architectures recognised by the output count. */
#define YOLO_ARCH_V5 1      /* three branches, 3 * (5 + classes) channels */
#define YOLO_ARCH_11 2      /* RKOPT: 9 outputs (box, classes, score sum) */

typedef struct {
    int width;
    int height;
    int classes;
    int runtime;            /* 1 = RKNN 1.x, 2 = RKNN 2.x */
    int arch;               /* YOLO_ARCH_V5 or YOLO_ARCH_11 */
    char sdk[64];
} yolo_model_info;

/* Start/stop the capture + detection threads.  yolo_start() fails when the
 * configured model cannot be loaded; the caller keeps the camera running. */
int yolo_start(void);
void yolo_stop(void);
int yolo_running(void);

/* Live switch for the bounding boxes on the OSD.  The capture thread creates
 * or destroys the 2BPP overlay on its next tick; the value is not persisted
 * here (app_config.yolo_boxes is owned by the caller). */
void yolo_boxes_request(int enable);

/* Starts an event package without a detection (bench hook): the same JPEG
 * bundle and clip as a real YOLO event. */
int yolo_event_test(void);

/* Architecture of the running detector, or 0 when no model is active. */
int yolo_active_arch(void);

/* Load a model for validation without touching the active context.  The
 * yolo.runtime value selects the library; the *runtime variants let the
 * caller probe the other runtime to explain a mismatch. */
int yolo_model_probe(const char *path, yolo_model_info *info,
    char *error, size_t error_size);
int yolo_model_probe_runtime(const char *path, int runtime,
    yolo_model_info *info, char *error, size_t error_size);

/* Atomically replace the active model.  On failure the previous context is
 * kept and `error` describes the reason (incompatible toolkit, bad tensors). */
int yolo_model_swap(const char *path, char *error, size_t error_size);