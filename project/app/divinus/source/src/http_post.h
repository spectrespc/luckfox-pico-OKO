#pragma once

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "hal/globals.h"
#include "hal/macros.h"
#include "jpeg.h"

int http_post_send(hal_jpegdata *jpeg);
/* Sends one frame of an event package with the X-Event-* headers. */
int http_post_event(hal_jpegdata *jpeg, const char *eventId, int index,
    int total, unsigned long long timestampMs, const char *cause,
    const char *model);
/* Sends one event clip as a PUT body with the X-Event-* headers.  The path is
 * derived from http_post.url (last segment "frame" becomes "clip"). */
int http_post_clip(const char *path, const char *eventId, const char *cause,
    const char *model, unsigned long long timestampMs,
    unsigned long long durationMs);
void http_post_start();
void http_post_stop();
