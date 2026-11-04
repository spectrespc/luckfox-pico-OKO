#pragma once

#define RECORD_PATH_MAX 512

/* Set by /api/storage while a destructive card operation (format/unmount)
 * is in progress; recording must not open a segment during that window. */
extern volatile int storageBusy;

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "app_config.h"
#include "fmt/mp4.h"
#include "hal/macros.h"
#include "hal/types.h"

bool record_start(void);
void record_stop(void);
bool record_active(void);
time_t record_start_time(void);
void record_ingest_stream(hal_vidstream *stream, char isH265);

/* Which channel feeds the recorder: record.source for continuous recording,
 * the event source while an IVS motion clip is being written. */
bool record_wants_source(bool sub);
/* Motion-triggered clip.  Returns false when recording is already running or
 * no storage is available. */
bool record_start_event(unsigned int seconds, bool sub);
void record_stop_event(void);
