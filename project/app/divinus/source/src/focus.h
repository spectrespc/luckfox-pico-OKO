#pragma once

/* Focus assistant: AF statistics aggregation and optional motor control.
 *
 * The HTTP layer only reads a cached snapshot and posts configuration updates;
 * the actual get3AStatsBlk calls run in this module's thread. */
#include <stdbool.h>

int focus_start(void);
void focus_stop(void);
int focus_running(void);
int focus_available(void);

/* Applies mode/ROI/fps to the running thread (or to the next start). */
void focus_configure(int mode, unsigned int x, unsigned int y,
                     unsigned int w, unsigned int h, unsigned int fps);
/* Reads the last applied configuration. */
void focus_get_config(int *mode, unsigned int *x, unsigned int *y,
                      unsigned int *w, unsigned int *h, unsigned int *fps);

/* Latest aggregated metric for the auto-focus worker.  Returns 0 when the
 * statistics thread is running and at least one frame has been measured. */
int focus_metric_get(unsigned long long *metric_avg,
                     unsigned long long *metric_smooth,
                     unsigned int *frame_id);

/* Returns a malloc()ed JSON object; the caller frees it.  NULL on OOM. */
char *focus_json(int with_map);