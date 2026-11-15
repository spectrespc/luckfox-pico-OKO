#include "network.h"

#include <pthread.h>
#include <string.h>
#include <arpa/inet.h>

/* mDNS publication is reconciled every MDNS_TICK_SECONDS: divinus starts in
 * S21appinit, long before dhcpcd/wpa_supplicant hand out an address, so the
 * responder cannot be started once at boot.  A failed start (no IPv4 yet, or
 * UDP 5353 taken by a foreign responder) is retried after a longer pause to
 * keep the log readable. */
#define MDNS_TICK_SECONDS 10
#define MDNS_RETRY_TICKS 6
#define MDNS_MAX_ATTEMPTS 5
#define MDNS_HOST_PORT 5353

char configured = 0;
NetInfo netinfo;

static struct mdnsd *mdns = NULL;
static pthread_mutex_t mdns_lock = PTHREAD_MUTEX_INITIALIZER;
static char mdns_name[MDNS_NAME_LEN];
static char mdns_addrs[MDNS_MAX_ADDRESSES][INET_ADDRSTRLEN];
static int mdns_addr_count = 0;
static int mdns_running = 0;
static int mdns_retry = 0;
static int mdns_fail_count = 0;
static char mdns_fail_addrs[MDNS_MAX_ADDRESSES][INET_ADDRSTRLEN];
static int mdns_fail_addr_count = 0;

void network_init(void) {
    if (configured) return;

    struct ifaddrs *ifa, *ifaddr;
    if (getifaddrs(&ifaddr) == -1) {
        HAL_DANGER("network", "Failed to get network interfaces!\n");
        return;
    }

    int count = 0;
    for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (count >= MDNS_MAX_ADDRESSES) break;
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;
        struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
        snprintf(netinfo.intf[count], sizeof(netinfo.intf[count]), "%s",
            ifa->ifa_name);
        inet_ntop(AF_INET, &addr->sin_addr.s_addr,
            netinfo.ipaddr[count], sizeof(*netinfo.ipaddr));
        HAL_INFO("network", "Interface %s has address %s\n",
            netinfo.intf[count], netinfo.ipaddr[count]);
        count++;
    }
    netinfo.count = (char)count;
    freeifaddrs(ifaddr);

    configured = 1;
}

int network_start(void) {
    network_init();
    if (!configured)
        return EXIT_FAILURE;

    if (app_config.onvif_enable)
        start_onvif();

    /* No IPv4 is normal at this point in the boot; network_tick() publishes
     * the name as soon as an address shows up. */
    if (app_config.mdns_enable)
        mdns_start();

    return EXIT_SUCCESS;
}

void network_stop(void) {
    mdns_stop();

    if (app_config.onvif_enable)
        stop_onvif();
}

/* Collects the non-loopback IPv4 addresses of the moment, deduplicated. */
static int collect_ipv4(char addrs[][INET_ADDRSTRLEN], int max) {
    struct ifaddrs *ifa, *ifaddr;
    int count = 0;

    if (!addrs || max <= 0) return 0;
    if (getifaddrs(&ifaddr) == -1)
        return 0;

    for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        char text[INET_ADDRSTRLEN];
        struct sockaddr_in *addr;
        int i, dup = 0;

        if (count >= max) break;
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;
        addr = (struct sockaddr_in *)ifa->ifa_addr;
        if (addr->sin_addr.s_addr == htonl(INADDR_ANY)) continue;
        if (!inet_ntop(AF_INET, &addr->sin_addr.s_addr, text, sizeof(text)))
            continue;
        for (i = 0; i < count; i++) {
            if (!strcmp(addrs[i], text)) {
                dup = 1;
                break;
            }
        }
        if (dup) continue;
        snprintf(addrs[count], INET_ADDRSTRLEN, "%s", text);
        count++;
    }

    freeifaddrs(ifaddr);
    return count;
}

/* Manual hex parsing: the ctype macros are unusable here, compat.c interposes
 * __ctype_b/__ctype_tolower and the table lookup segfaults (see FEATURES.md). */
static int mdns_hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* "oko-" plus the last four hex digits of the eth0 MAC (wlan0 is a fallback
 * only, so swapping the USB adapter does not change the published name).
 * Returns 0 and the bare name without ".local" on success. */
int mdns_hostname(char *out, size_t len) {
    static const char *sources[] = {
        "/sys/class/net/eth0/address",
        "/sys/class/net/wlan0/address"
    };
    char mac[64], hex[13];
    FILE *file = NULL;
    size_t i;
    int digits = 0, value;
    const char *p;

    if (!out || len < 10) return -1;

    for (i = 0; i < sizeof(sources) / sizeof(sources[0]) && !file; i++)
        file = fopen(sources[i], "r");
    if (!file) return -1;
    if (!fgets(mac, sizeof(mac), file)) {
        fclose(file);
        return -1;
    }
    fclose(file);

    for (p = mac; *p && *p != '\n' && *p != '\r'; p++) {
        if (*p == ':') continue;
        value = mdns_hex(*p);
        if (value < 0 || digits >= (int)sizeof(hex) - 1) {
            digits = -1;
            break;
        }
        hex[digits++] = (char)('0' + value);
    }
    if (digits != 12) return -1;
    hex[digits] = '\0';

    if (snprintf(out, len, "oko-%s", hex + 8) >= (int)len) return -1;
    for (p = out; *p; p++) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-'))
            return -1;
    }
    return 0;
}

/* tinysvcmdns keeps the A record in network order and runs htonl() on encode
 * (mdns_encode_pkt), so s_addr must be passed through unchanged; ip_to_int()
 * returns the host-order value and would put the octets on the wire backwards. */
static uint32_t mdns_a_addr(const char *text) {
    struct in_addr addr;

    if (!inet_aton(text, &addr)) return 0;
    return addr.s_addr;
}

static int mdns_snapshot_matches(const char addrs[][INET_ADDRSTRLEN],
    int count) {
    int i;

    if (count != mdns_addr_count) return 0;
    for (i = 0; i < count; i++) {
        if (strcmp(addrs[i], mdns_addrs[i])) return 0;
    }
    return 1;
}

/* A failed start is retried a few times with the 60 s backoff; after that the
 * same address set is not retried again, otherwise a permanent failure would
 * leak the library's descriptors once a minute.  A new address set or a
 * WebUI/API toggle re-arms the attempts. */
static void mdns_reset_failures(void) {
    mdns_fail_count = 0;
    mdns_fail_addr_count = 0;
}

static int mdns_retry_suppressed(const char addrs[][INET_ADDRSTRLEN],
    int count) {
    int i;

    if (mdns_fail_count < MDNS_MAX_ATTEMPTS) return 0;
    if (count != mdns_fail_addr_count) return 0;
    for (i = 0; i < count; i++) {
        if (strcmp(addrs[i], mdns_fail_addrs[i])) return 0;
    }
    return 1;
}

static void mdns_note_failure(const char addrs[][INET_ADDRSTRLEN], int count) {
    int i;

    mdns_fail_count++;
    mdns_fail_addr_count = count;
    for (i = 0; i < count; i++)
        snprintf(mdns_fail_addrs[i], sizeof(mdns_fail_addrs[i]), "%s",
            addrs[i]);
    if (mdns_fail_count == MDNS_MAX_ATTEMPTS) {
        HAL_WARNING("mdns", "Giving up after %d failed starts; the next "
            "attempt waits for a new address or a switch change\n",
            MDNS_MAX_ATTEMPTS);
    }
}

/* Caller holds mdns_lock. */
static void mdns_stop_locked(void) {
    if (mdns)
        mdnsd_stop(mdns);
    mdns = NULL;
    mdns_running = 0;
    mdns_addr_count = 0;
    memset(mdns_addrs, 0, sizeof(mdns_addrs));
}

/* Caller holds mdns_lock.  The hostname is immutable on a live instance
 * (mdnsd_set_hostname asserts), so an address change always recreates it. */
static int mdns_start_locked(const char addrs[][INET_ADDRSTRLEN], int count) {
    char hostname[MDNS_NAME_LEN + 8];
    char name[MDNS_NAME_LEN];
    struct mdnsd *instance;
    int i;

    if (count <= 0) return EXIT_FAILURE;
    if (mdns_hostname(name, sizeof(name))) {
        HAL_WARNING("mdns", "No usable MAC address; publication disabled\n");
        return EXIT_FAILURE;
    }

    instance = mdnsd_start();
    if (!instance) {
        HAL_ERROR("mdns", "Cannot start the responder on UDP %d\n", MDNS_HOST_PORT);
        return EXIT_FAILURE;
    }

    snprintf(hostname, sizeof(hostname), "%s.local", name);
    /* set_hostname() creates the A record for the first address and the NSEC
     * record; the remaining addresses are added explicitly. */
    mdnsd_set_hostname(instance, hostname, mdns_a_addr(addrs[0]));
    for (i = 1; i < count; i++) {
        struct rr_entry *entry = rr_create_a(create_nlabel(hostname),
            mdns_a_addr(addrs[i]));
        if (entry)
            mdnsd_add_rr(instance, entry);
    }

    snprintf(mdns_name, sizeof(mdns_name), "%s", name);
    for (i = 0; i < count; i++)
        snprintf(mdns_addrs[i], sizeof(mdns_addrs[i]), "%s", addrs[i]);
    mdns_addr_count = count;
    mdns = instance;
    mdns_running = 1;
    mdns_reset_failures();
    HAL_INFO("mdns", "Publishing %s with %d address(es)\n", hostname, count);
    return EXIT_SUCCESS;
}

int mdns_start(void) {
    char addrs[MDNS_MAX_ADDRESSES][INET_ADDRSTRLEN];
    int count, rc;

    count = collect_ipv4(addrs, MDNS_MAX_ADDRESSES);
    pthread_mutex_lock(&mdns_lock);
    mdns_stop_locked();
    mdns_reset_failures();
    rc = mdns_start_locked(addrs, count);
    pthread_mutex_unlock(&mdns_lock);
    return rc;
}

void mdns_stop(void) {
    pthread_mutex_lock(&mdns_lock);
    mdns_stop_locked();
    pthread_mutex_unlock(&mdns_lock);
}

void mdns_set_enabled(bool enabled) {
    char addrs[MDNS_MAX_ADDRESSES][INET_ADDRSTRLEN];
    int count = 0;

    if (enabled)
        count = collect_ipv4(addrs, MDNS_MAX_ADDRESSES);

    pthread_mutex_lock(&mdns_lock);
    app_config.mdns_enable = enabled;
    mdns_retry = 0;
    mdns_reset_failures();
    mdns_stop_locked();
    if (enabled)
        mdns_start_locked(addrs, count);
    pthread_mutex_unlock(&mdns_lock);
}

void mdns_get_state(MdnsState *state) {
    char name[MDNS_NAME_LEN];
    int i;

    if (!state) return;
    memset(state, 0, sizeof(*state));

    pthread_mutex_lock(&mdns_lock);
    state->enabled = app_config.mdns_enable;
    state->running = mdns_running != 0;
    if (!mdns_hostname(name, sizeof(name)))
        snprintf(state->name, sizeof(state->name), "%s", name);
    state->count = mdns_addr_count;
    for (i = 0; i < mdns_addr_count && i < MDNS_MAX_ADDRESSES; i++)
        snprintf(state->addresses[i], sizeof(state->addresses[i]), "%s",
            mdns_addrs[i]);
    pthread_mutex_unlock(&mdns_lock);
}

/* Keeps the published records in sync with the live address set.  Runs on the
 * main loop; a no-op unless the configured state and the snapshot differ. */
void network_tick(void) {
    static unsigned int ticks = 0;
    char addrs[MDNS_MAX_ADDRESSES][INET_ADDRSTRLEN];
    int count;

    if (++ticks < MDNS_TICK_SECONDS) return;
    ticks = 0;

    pthread_mutex_lock(&mdns_lock);
    if (!app_config.mdns_enable) {
        mdns_stop_locked();
    } else if (mdns_retry > 0) {
        mdns_retry--;
    } else {
        count = collect_ipv4(addrs, MDNS_MAX_ADDRESSES);
        if (count <= 0) {
            if (mdns_running)
                HAL_INFO("mdns", "No IPv4 address left; publication stopped\n");
            mdns_stop_locked();
            mdns_reset_failures();
        } else if (!mdns_running || !mdns_snapshot_matches(addrs, count)) {
            if (!mdns_retry_suppressed(addrs, count)) {
                mdns_stop_locked();
                if (mdns_start_locked(addrs, count) != EXIT_SUCCESS) {
                    mdns_note_failure(addrs, count);
                    mdns_retry = MDNS_RETRY_TICKS;
                }
            }
        }
    }
    pthread_mutex_unlock(&mdns_lock);
}