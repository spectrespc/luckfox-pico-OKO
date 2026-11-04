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
void http_post_start();
void http_post_stop();
