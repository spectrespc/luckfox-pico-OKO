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
void http_post_start();
void http_post_stop();
