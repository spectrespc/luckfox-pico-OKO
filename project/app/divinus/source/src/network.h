#pragma once

#include <ifaddrs.h>
#include <net/if.h>
#include <stdio.h>

#include "app_config.h"
#include "hal/support.h"
#include "lib/tinysvcmdns.h"
#include "onvif.h"

#define MDNS_MAX_ADDRESSES 3
#define MDNS_NAME_LEN 16

typedef struct {
    char intf[3][16];
    char ipaddr[3][INET_ADDRSTRLEN];
    char count;
} NetInfo;

/* Snapshot of the mDNS publication for GET /api/mdns and the WebUI. */
typedef struct {
    bool enabled;
    bool running;
    char name[MDNS_NAME_LEN];                       /* "oko-xxxx", no ".local" */
    char addresses[MDNS_MAX_ADDRESSES][INET_ADDRSTRLEN];
    int count;
} MdnsState;

void network_init(void);
int network_start(void);
void network_stop(void);

/* Reconciles the published mDNS records with the live IPv4 set. */
void network_tick(void);

int mdns_hostname(char *out, size_t len);
int mdns_start(void);
void mdns_stop(void);
void mdns_set_enabled(bool enabled);
void mdns_get_state(MdnsState *state);