#pragma once

/* Pure hill-climbing decisions for focus_af.c.
 *
 * The worker in focus_af.c moves the lens, waits for the AF statistics to
 * settle and feeds each sample here.  Keeping the decision rules free of I2C,
 * threads and configuration lets the host test in tests/focus_af_test.c cover
 * the direction probe and the patience stop exactly as the firmware runs
 * them. */
#include <stdint.h>

typedef struct {
    unsigned int patience;          /* consecutive non-improvements to stop */
    unsigned int misses;            /* current run of non-improvements       */
    unsigned long long best_metric;
    int best_position;
    int have_best;
} af_tracker;

/* Starts a scan with the first sample already taken. */
void af_tracker_reset(af_tracker *tracker, unsigned int patience,
                      unsigned long long metric, int position);

/* Records a new sample.  Returns 1 when the scan should stop because
 * `patience` consecutive samples did not beat the best metric. */
int af_tracker_update(af_tracker *tracker, unsigned long long metric,
                      int position);

/* Direction of the first probe: +1 when only the positive step improves,
 * -1 when only the negative step improves, 0 when neither does (the start
 * point is a local peak and the coarse pass can be skipped). */
int af_pick_direction(unsigned long long start, unsigned long long plus,
                      unsigned long long minus);

/* Micro-fine window around `pos`: [pos - span, pos + span], clamped to
 * [lo, hi].  `pos` outside the scale is clamped into it first.  `span` is
 * clamped to the scale width, so *out_lo <= *out_hi always; span 0 yields a
 * single point. */
void af_window(int32_t pos, int32_t lo, int32_t hi, int32_t span,
               int32_t *out_lo, int32_t *out_hi);