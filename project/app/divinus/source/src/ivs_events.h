#pragma once

#include <stddef.h>

void ivs_events_start(void);
void ivs_events_stop(void);

/* Motion/OD rectangles on the main stream.  The flag is applied by the IVS
 * thread on its next iteration, so no stream restart is needed. */
void ivs_boxes_request(int enable);

/* Renders the recent events as a JSON array; returns the length. */
size_t ivs_events_json(char *out, size_t size);

/* Last motion area of the analytics loop in permille of the frame, or -1
 * when IVS is disabled or has not produced a result yet. */
int ivs_last_area_permille(void);