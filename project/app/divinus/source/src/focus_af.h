#pragma once

/* Automatic focus search for the STM8 focus motor (TO_DO §5 variant B).
 *
 * The search is a hill climb over the AF statistics metric: probe, coarse
 * pass, fine pass, then move to the best position.  It is asynchronous like
 * the home/zero job: the HTTP thread starts a worker and the WebUI polls
 * GET /api/focus for "af".  Only the focus scale is moved; zoom is manual. */
#include <stdint.h>

typedef struct {
    char state[12];              /* idle | running | done | error */
    int position;                /* current focus position               */
    int best_position;           /* best position found so far           */
    unsigned long long metric;   /* metric at the current position       */
    unsigned long long best_metric;
    int progress;                /* 0..100                               */
    int done;
    char error[128];
} focus_af_status;

int focus_af_init(void);
void focus_af_shutdown(void);

/* 1 while a search is running. */
int focus_af_running(void);

/* Starts a search.  Only motor 0 (focus) is supported.
 * Returns 0 on success, -1 when a job is already running, -2 when the worker
 * could not be started or the motor is invalid. */
int focus_af_start(int motor);

/* Asks the worker to stop after the current move/sample; the final state is
 * "error" with error "cancelled".  Safe to call when no job is running. */
void focus_af_cancel(void);

void focus_af_status_get(focus_af_status *out);