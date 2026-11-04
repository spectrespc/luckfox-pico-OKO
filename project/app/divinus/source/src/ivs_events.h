#pragma once

#include <stddef.h>

void ivs_events_start(void);
void ivs_events_stop(void);

/* Renders the recent events as a JSON array; returns the length. */
size_t ivs_events_json(char *out, size_t size);