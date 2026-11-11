#pragma once

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "hal/support.h"
#include "hal/tools.h"

#define MAX_UDP_PACKET_SIZE 1400
#define UDP_DEFAULT_PORT 5600
#define RTP_HEADER_SIZE 12
#define UDP_MAX_CLIENTS 8

typedef struct {
    struct sockaddr_in addr;
    int active;
    unsigned int ssrc;
    unsigned short seq;
    unsigned int tstamp;
    time_t last_act;
    /* 1 for a destination taken from the configuration: such a client is
     * never expired for inactivity, because a pure RTP receiver has no
     * reason to send anything back.  Auto-registered senders keep 0 and are
     * dropped after UDP_CLIENT_TIMEOUT seconds of silence. */
    int permanent;
} udp_client_t;

#define UDP_CLIENT_TIMEOUT 60

struct udp_stream_ctx {
    int socket_fd;
    unsigned short port;
    volatile int running;
    pthread_t thread;
    pthread_mutex_t mutex;
    udp_client_t clients[UDP_MAX_CLIENTS];
    int client_count;
    char is_mcast;
    unsigned int mcast_addr;
};

int udp_stream_init(unsigned short port, const char *mcast_addr);
void udp_stream_close(void);
int udp_stream_add_client(const char *host, unsigned short port);
void udp_stream_remove_client(int client_id);
int udp_stream_send_nal(const char *nal_data, int nal_size, int marker, int is_h265);
/* Advance the RTP timestamp of every client by one frame (90 kHz units) and
 * refresh the configured destinations.  Call once per captured frame, after
 * all of its NAL units have been sent. */
void udp_stream_frame_end(unsigned int tstamp_step);