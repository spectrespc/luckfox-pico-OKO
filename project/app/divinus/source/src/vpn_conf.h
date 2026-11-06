#pragma once

#include <stddef.h>

#define VPN_PROFILE_NAME_MAX 32
#define VPN_TYPE_MAX 8
#define VPN_IFNAME_MAX 8
#define VPN_ADDR_MAX 512
#define VPN_ENDPOINT_MAX 256
#define VPN_ALLOWED_MAX 1024
#define VPN_ERROR_MAX 256
#define VPN_CONF_MAX_BYTES 8192
#define VPN_CONF_LINE_MAX 512

typedef struct {
    char name[VPN_PROFILE_NAME_MAX];
    char type[VPN_TYPE_MAX];
    char ifname[VPN_IFNAME_MAX];
    char address4[VPN_ADDR_MAX];
    char address6[VPN_ADDR_MAX];
    char endpoint[VPN_ENDPOINT_MAX];
    char allowedips[VPN_ALLOWED_MAX];
    char admin_nets[VPN_ADDR_MAX];
    char dns[VPN_ADDR_MAX];
    int mtu;
    int fulltunnel;
    int auto_start;
    int dns_tunnel;
} vpn_conf_t;

/*
 * Parses a wg-quick-style configuration.  Script hooks are rejected, the
 * working keys are validated, and the type is taken from the "# oko:" line
 * or inferred from the AmneziaWG-only keys.
 *
 * Returns 0 on success, -1 with a message in "error" otherwise.
 */
int vpn_conf_parse_text(const char *text, size_t len, vpn_conf_t *out,
    char *error, size_t error_size);

int vpn_conf_parse_file(const char *path, vpn_conf_t *out,
    char *error, size_t error_size);

/* Replaces (or adds) "type=" in the first "# oko:" line.  The text buffer
 * must hold the configuration plus a possible metadata line. */
int vpn_conf_set_type(char *text, size_t capacity, const char *type);

/* Name rules shared by the API and the helper: [a-z0-9_-]{1,31}. */
int vpn_conf_valid_name(const char *name);