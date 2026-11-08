#define _GNU_SOURCE

#include "vpn_conf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifndef VPN_CONF_LINE_MAX
#define VPN_CONF_LINE_MAX 512
#endif

/* uClibc ctype macros are not usable in divinus (compat.c overrides the
 * tables), so the parser uses plain ASCII checks. */
static int ascii_space(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
        c == '\v';
}

static int ascii_alnum(unsigned char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z');
}

static char *trim_inplace(char *s)
{
    char *end;

    while (*s && ascii_space((unsigned char)*s)) s++;
    end = s + strlen(s);
    while (end > s && ascii_space((unsigned char)end[-1])) *--end = '\0';
    return s;
}

static int append_csv(char *dst, size_t capacity, const char *value)
{
    size_t used = strlen(dst);
    int need = dst[0] ? 1 : 0;

    if (used + (size_t)need + strlen(value) + 1 > capacity) return -1;
    if (need) strcat(dst, ",");
    strcat(dst, value);
    return 0;
}

static int valid_name(const char *name)
{
    size_t i, len;

    if (!name) return 0;
    len = strlen(name);
    if (len == 0 || len > 31) return 0;
    for (i = 0; i < len; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '_' || c == '-'))
            return 0;
    }
    return 1;
}

int vpn_conf_valid_name(const char *name)
{
    return valid_name(name);
}

static int allowed_chars(const char *value, const char *extra)
{
    const unsigned char *p = (const unsigned char *)value;

    for (; *p; p++) {
        if ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') ||
            (*p >= 'A' && *p <= 'F') || *p == ':' || *p == '.' || *p == '/' ||
            (extra && strchr(extra, *p)))
            continue;
        return 0;
    }
    return 1;
}

static int valid_endpoint(const char *endpoint)
{
    const char *colon;
    char host[VPN_ENDPOINT_MAX];
    const char *port;
    size_t len;
    long value;
    char *end = NULL;

    if (!endpoint || !*endpoint) return 0;
    colon = strrchr(endpoint, ':');
    if (!colon) return 0;
    len = (size_t)(colon - endpoint);
    if (len == 0 || len >= sizeof(host)) return 0;
    memcpy(host, endpoint, len);
    host[len] = '\0';
    /* Host names and IPv6 literals are allowed; no shell or JSON
     * metacharacters may pass through, because the value reaches JSON. */
    {
        const char *h;
        for (h = host; *h; h++) {
            if (ascii_alnum((unsigned char)*h) || *h == '.' || *h == '-' ||
                *h == '_' || *h == ':' || *h == '[' || *h == ']')
                continue;
            return 0;
        }
    }
    port = colon + 1;
    if (!*port) return 0;
    value = strtol(port, &end, 10);
    if (!end || *end || value < 1 || value > 65535) return 0;
    return 1;
}

static int awg_key(const char *key)
{
    static const char *keys[] = {
        "Jc", "Jmin", "Jmax", "S1", "S2", "S3", "S4",
        "H1", "H2", "H3", "H4", "I1", "I2", "I3", "I4", "I5",
        "HeaderProtectionKey", "ContentPaddingAddition",
        "AdvancedSecurity", "RandomTrailers", "DisableCookies",
        "RekeyAfterTime", "RekeyTimeout", "RejectAfterTime",
        "KeepaliveTimeout", "MaxHandshakeAttempts", NULL
    };
    int i;

    for (i = 0; keys[i]; i++)
        if (!strcmp(key, keys[i])) return 1;
    return 0;
}

static int hook_key(const char *key)
{
    return !strcasecmp(key, "PreUp") || !strcasecmp(key, "PostUp") ||
        !strcasecmp(key, "PreDown") || !strcasecmp(key, "PostDown");
}

static void parse_meta(const char *line, vpn_conf_t *out, int *type_seen,
    int *mtu_seen);

static int forbidden_ovpn(const char *key)
{
    static const char *keys[] = {
        "up", "down", "route-up", "route-pre-down", "ipchange",
        "client-disconnect", "tls-verify", "management",
        "management-client-auth", "plugin", "daemon", "log", "log-append",
        "pid", "pidfile", "status", "dev-node", "iep-proxy", "askpass",
        "config", "cd", "auth-user-pass-verify", "client-connect",
        "learn-address", NULL
    };
    int i;

    for (i = 0; keys[i]; i++)
        if (!strcasecmp(key, keys[i])) return 1;
    return 0;
}

/* An OpenVPN client profile has "client" or "pull" and a "remote". */
static int looks_like_ovpn(const char *text, size_t len)
{
    const char *cursor = text;
    const char *end = text + len;
    int has_client = 0, has_remote = 0;

    while (cursor < end) {
        const char *nl = memchr(cursor, '\n', (size_t)(end - cursor));
        size_t line_len = nl ? (size_t)(nl - cursor) : (size_t)(end - cursor);
        char line[VPN_CONF_LINE_MAX];
        char *value;

        if (line_len >= sizeof(line)) return 0;
        memcpy(line, cursor, line_len);
        line[line_len] = '\0';
        cursor = nl ? nl + 1 : end;

        value = trim_inplace(line);
        if (!*value || *value == '#' || *value == ';') continue;
        if (*value == '[') return 0;
        if (!strcmp(value, "client") || !strcmp(value, "pull"))
            has_client = 1;
        else if (!strncmp(value, "remote ", 7) ||
            !strcmp(value, "remote"))
            has_remote = 1;
    }
    return has_client && has_remote;
}

static void declared_meta_type(const char *text, size_t len, char *out,
    size_t out_size)
{
    const char *cursor = text;
    const char *end = text + len;

    out[0] = '\0';
    while (cursor < end) {
        const char *nl = memchr(cursor, '\n', (size_t)(end - cursor));
        size_t line_len = nl ? (size_t)(nl - cursor) : (size_t)(end - cursor);
        char line[VPN_CONF_LINE_MAX];
        char *p, *token, *save = NULL;

        if (line_len >= sizeof(line)) return;
        memcpy(line, cursor, line_len);
        line[line_len] = '\0';
        cursor = nl ? nl + 1 : end;

        p = trim_inplace(line);
        if (*p != '#') continue;
        p = trim_inplace(p + 1);
        if (strncmp(p, "oko:", 4)) continue;
        p += 4;
        for (token = strtok_r(p, " \t\r\n", &save); token;
            token = strtok_r(NULL, " \t\r\n", &save)) {
            if (!strncmp(token, "type=", 5)) {
                snprintf(out, out_size, "%s", token + 5);
                return;
            }
        }
    }
}

static char *next_token(char **cursor)
{
    char *s = *cursor, *start;

    while (*s && ascii_space((unsigned char)*s)) s++;
    if (!*s) {
        *cursor = s;
        return NULL;
    }
    start = s;
    while (*s && !ascii_space((unsigned char)*s)) s++;
    if (*s) *s++ = '\0';
    *cursor = s;
    return start;
}

static int valid_host(const char *host)
{
    const char *p;

    if (!host || !*host) return 0;
    for (p = host; *p; p++) {
        if (ascii_alnum((unsigned char)*p) || *p == '.' || *p == '-' ||
            *p == '_' || *p == ':' || *p == '[' || *p == ']')
            continue;
        return 0;
    }
    return 1;
}

/* Returns 0 when the line is not an inline tag, 1 for an opening tag and
 * -1 for a closing tag. */
static int inline_tag(const char *line, char *tag, size_t tag_size)
{
    size_t len = strlen(line);
    const char *p;
    size_t i, tag_len;

    if (len < 3 || line[0] != '<' || line[len - 1] != '>') return 0;
    if (line[1] == '/') return -1;
    tag_len = len - 2;
    if (tag_len == 0 || tag_len >= tag_size) return 0;
    p = line + 1;
    for (i = 0; i < tag_len; i++) {
        if (ascii_alnum((unsigned char)p[i]) || p[i] == '_' || p[i] == '-')
            continue;
        return 0;
    }
    memcpy(tag, p, tag_len);
    tag[tag_len] = '\0';
    return 1;
}

static int parse_ovpn_text(const char *text, size_t len, vpn_conf_t *out,
    char *error, size_t error_size)
{
    const char *cursor = text;
    const char *end = text + len;
    int has_client = 0;
    int has_dev = 0, dev_tun = 0;
    int has_remote = 0;
    int redirect = 0;
    int needs_credentials = 0, credentials_inline = 0;
    int in_inline = 0;
    int mt_meta = 0, type_unused = 0;

    while (cursor < end) {
        const char *nl = memchr(cursor, '\n', (size_t)(end - cursor));
        size_t line_len = nl ? (size_t)(nl - cursor) : (size_t)(end - cursor);
        char line[VPN_CONF_LINE_MAX];
        char tag[VPN_CONF_LINE_MAX];
        char *value, *key, *rest;
        int tag_kind;

        if (line_len >= sizeof(line)) {
            snprintf(error, error_size, "a line is longer than %d bytes",
                VPN_CONF_LINE_MAX - 1);
            return -1;
        }
        memcpy(line, cursor, line_len);
        line[line_len] = '\0';
        cursor = nl ? nl + 1 : end;

        value = trim_inplace(line);
        tag_kind = inline_tag(value, tag, sizeof(tag));
        if (in_inline) {
            if (tag_kind < 0) in_inline = 0;
            continue;
        }
        if (tag_kind > 0) {
            if (!strcmp(tag, "auth-user-pass")) credentials_inline = 1;
            in_inline = 1;
            continue;
        }
        if (!*value || *value == ';') continue;
        if (*value == '#') {
            parse_meta(value, out, &type_unused, &mt_meta);
            continue;
        }

        key = next_token(&value);
        rest = value;
        while (*rest && ascii_space((unsigned char)*rest)) rest++;
        if (!strcasecmp(key, "client") || !strcasecmp(key, "pull")) {
            has_client = 1;
        } else if (!strcasecmp(key, "dev")) {
            char *dev = next_token(&rest);

            if (!dev) {
                snprintf(error, error_size, "dev tun is required");
                return -1;
            }
            has_dev = 1;
            if (!strcmp(dev, "tun")) {
                dev_tun = 1;
            } else if (!strncmp(dev, "tun", 3) && dev[3]) {
                const char *d;
                dev_tun = 1;
                for (d = dev + 3; *d; d++)
                    if (*d < '0' || *d > '9') dev_tun = 0;
            }
            if (!dev_tun) {
                snprintf(error, error_size,
                    "only 'dev tun' is supported (got 'dev %s')", dev);
                return -1;
            }
        } else if (!strcasecmp(key, "remote")) {
            char *host = next_token(&rest);
            char *port = next_token(&rest);
            long port_num = 1194;
            char *port_end = NULL;
            char endpoint[VPN_ENDPOINT_MAX];

            if (!host) {
                snprintf(error, error_size, "the remote host is missing");
                return -1;
            }
            if (!valid_host(host)) {
                snprintf(error, error_size,
                    "the remote host is invalid: %s", host);
                return -1;
            }
            if (port) {
                port_num = strtol(port, &port_end, 10);
                if (!port_end || *port_end || port_num < 1 ||
                    port_num > 65535) {
                    snprintf(error, error_size,
                        "the remote port is invalid: %s", port);
                    return -1;
                }
            }
            has_remote = 1;
            if (!out->endpoint[0]) {
                if (strchr(host, ':') && host[0] != '[')
                    snprintf(endpoint, sizeof(endpoint), "[%s]:%ld", host,
                        port_num);
                else
                    snprintf(endpoint, sizeof(endpoint), "%s:%ld", host,
                        port_num);
                snprintf(out->endpoint, sizeof(out->endpoint), "%s", endpoint);
            }
        } else if (!strcasecmp(key, "redirect-gateway")) {
            redirect = 1;
        } else if (!strcasecmp(key, "auth-user-pass")) {
            if (*rest) {
                snprintf(error, error_size,
                    "auth-user-pass with a file is not supported; use the "
                    "WebUI fields");
                return -1;
            }
            needs_credentials = 1;
        } else if (forbidden_ovpn(key)) {
            snprintf(error, error_size,
                "the openvpn directive '%s' is not supported", key);
            return -1;
        }
    }

    if (!has_client) {
        snprintf(error, error_size,
            "the OpenVPN profile needs 'client' or 'pull'");
        return -1;
    }
    if (!has_dev || !dev_tun) {
        snprintf(error, error_size, "dev tun is required");
        return -1;
    }
    if (!has_remote) {
        snprintf(error, error_size, "at least one 'remote' is required");
        return -1;
    }

    snprintf(out->type, sizeof(out->type), "ovpn");
    snprintf(out->ifname, sizeof(out->ifname), "tun0");
    out->fulltunnel = redirect;
    out->needs_credentials = credentials_inline ? 0 : needs_credentials;
    out->credentials_inline = credentials_inline;
    (void)mt_meta;
    return 0;
}

static void parse_meta(const char *line, vpn_conf_t *out, int *type_seen,
    int *mtu_seen)
{
    char copy[VPN_CONF_LINE_MAX];
    char *token, *save = NULL;
    char *p;

    snprintf(copy, sizeof(copy), "%s", line);
    p = copy;
    while (*p && ascii_space((unsigned char)*p)) p++;
    if (*p != '#') return;
    p++;
    while (*p && ascii_space((unsigned char)*p)) p++;
    if (strncmp(p, "oko:", 4)) return;
    p += 4;

    for (token = strtok_r(p, " \t\r\n", &save); token;
        token = strtok_r(NULL, " \t\r\n", &save)) {
        char *eq = strchr(token, '=');
        char *key, *value;

        if (!eq) continue;
        *eq = '\0';
        key = token;
        value = eq + 1;
        if (!strcmp(key, "type")) {
            if (!strcmp(value, "wg") || !strcmp(value, "awg")) {
                snprintf(out->type, sizeof(out->type), "%s", value);
                *type_seen = 1;
            }
        } else if (!strcmp(key, "auto")) {
            if (!strcmp(value, "0") || !strcmp(value, "1"))
                out->auto_start = value[0] == '1';
        } else if (!strcmp(key, "mtu")) {
            long mtu = strtol(value, NULL, 10);
            if (mtu >= 576 && mtu <= 1500) {
                out->mtu = (int)mtu;
                *mtu_seen = 1;
            }
        } else if (!strcmp(key, "dns")) {
            out->dns_tunnel = !strcmp(value, "tunnel");
        } else if (!strcmp(key, "admin_nets")) {
            snprintf(out->admin_nets, sizeof(out->admin_nets), "%s", value);
        }
    }
}

static void finish_peer(int *peers, int *peers_ok, int has_pub, int has_ep)
{
    (*peers)++;
    if (has_pub && has_ep) (*peers_ok)++;
}

int vpn_conf_parse_text(const char *text, size_t len, vpn_conf_t *out,
    char *error, size_t error_size)
{
    enum { SECTION_NONE, SECTION_INTERFACE, SECTION_PEER };
    int section = SECTION_NONE;
    int has_private = 0;
    int has_pub = 0, has_ep = 0;
    int peers = 0, peers_ok = 0;
    int type_seen = 0;
    int awg_seen = 0;
    int hooks = 0;
    int mt_line = 0, mt_meta = 0;
    int dns_line_seen = 0;
    int header_key = 0;
    int s_seen[4] = {0, 0, 0, 0};
    int s_val[4] = {0, 0, 0, 0};
    const char *cursor = text;
    const char *end = text + len;

    if (!out || !error || error_size == 0) return -1;
    memset(out, 0, sizeof(*out));
    out->mtu = 1420;
    error[0] = '\0';

    if (!text) {
        snprintf(error, error_size, "the configuration is empty");
        return -1;
    }

    {
        char declared[VPN_TYPE_MAX];

        declared_meta_type(text, len, declared, sizeof(declared));
        if (!strcmp(declared, "ovpn") || looks_like_ovpn(text, len))
            return parse_ovpn_text(text, len, out, error, error_size);
    }

    while (cursor < end) {
        const char *nl = memchr(cursor, '\n', (size_t)(end - cursor));
        size_t line_len = nl ? (size_t)(nl - cursor) : (size_t)(end - cursor);
        char line[VPN_CONF_LINE_MAX];
        char *value, *key, *eq;

        if (line_len >= sizeof(line)) {
            snprintf(error, error_size, "a line is longer than %d bytes",
                VPN_CONF_LINE_MAX - 1);
            return -1;
        }
        memcpy(line, cursor, line_len);
        line[line_len] = '\0';
        cursor = nl ? nl + 1 : end;

        value = trim_inplace(line);
        if (!*value) continue;
        if (*value == '#') {
            parse_meta(value, out, &type_seen, &mt_meta);
            continue;
        }
        if (*value == '[') {
            if (!strcmp(value, "[Interface]")) {
                if (section == SECTION_PEER)
                    finish_peer(&peers, &peers_ok, has_pub, has_ep);
                section = SECTION_INTERFACE;
                has_pub = has_ep = 0;
            } else if (!strcmp(value, "[Peer]")) {
                if (section == SECTION_PEER)
                    finish_peer(&peers, &peers_ok, has_pub, has_ep);
                section = SECTION_PEER;
                has_pub = has_ep = 0;
            } else {
                snprintf(error, error_size, "unknown section: %s", value);
                return -1;
            }
            continue;
        }

        eq = strchr(value, '=');
        if (!eq) continue;
        *eq = '\0';
        key = trim_inplace(value);
        eq = trim_inplace(eq + 1);
        if (hook_key(key)) {
            hooks = 1;
            continue;
        }
        if (awg_key(key)) awg_seen = 1;

        if (!strcasecmp(key, "PrivateKey")) {
            if (*eq) has_private = 1;
        } else if (!strcasecmp(key, "PublicKey")) {
            if (*eq) has_pub = 1;
        } else if (!strcasecmp(key, "Endpoint")) {
            if (*eq) {
                has_ep = 1;
                if (!out->endpoint[0]) {
                    if (!valid_endpoint(eq)) {
                        snprintf(error, error_size,
                            "the endpoint is invalid: %s", eq);
                        return -1;
                    }
                    snprintf(out->endpoint, sizeof(out->endpoint), "%s", eq);
                }
            }
        } else if (!strcasecmp(key, "AllowedIPs")) {
            char copy[VPN_CONF_LINE_MAX];
            char *token, *save = NULL;

            snprintf(copy, sizeof(copy), "%s", eq);
            for (token = strtok_r(copy, ",", &save); token;
                token = strtok_r(NULL, ",", &save)) {
                char *item = trim_inplace(token);
                if (!*item) continue;
                if (!allowed_chars(item, "[]")) {
                    snprintf(error, error_size,
                        "AllowedIPs contains an invalid entry: %s", item);
                    return -1;
                }
                if (append_csv(out->allowedips, sizeof(out->allowedips),
                        item)) {
                    snprintf(error, error_size,
                        "AllowedIPs is too long");
                    return -1;
                }
                if (!strcmp(item, "0.0.0.0/0") || !strcmp(item, "::/0"))
                    out->fulltunnel = 1;
            }
        } else if (!strcasecmp(key, "Address")) {
            char copy[VPN_CONF_LINE_MAX];
            char *token, *save = NULL;

            snprintf(copy, sizeof(copy), "%s", eq);
            for (token = strtok_r(copy, ",", &save); token;
                token = strtok_r(NULL, ",", &save)) {
                char *item = trim_inplace(token);
                char *dst;
                size_t cap;

                if (!*item) continue;
                if (!allowed_chars(item, "[]")) {
                    snprintf(error, error_size,
                        "Address contains an invalid entry: %s", item);
                    return -1;
                }
                if (strchr(item, ':')) {
                    dst = out->address6;
                    cap = sizeof(out->address6);
                } else {
                    dst = out->address4;
                    cap = sizeof(out->address4);
                }
                if (append_csv(dst, cap, item)) {
                    snprintf(error, error_size, "Address is too long");
                    return -1;
                }
            }
        } else if (key[0] == 'S' && key[1] >= '1' && key[1] <= '4' &&
            key[2] == '\0') {
            int index = key[1] - '1';
            long value = strtol(eq, NULL, 10);
            if (value < 0 || value > 65535) {
                snprintf(error, error_size,
                    "%s must be between 0 and 65535", key);
                return -1;
            }
            s_seen[index] = 1;
            s_val[index] = (int)value;
        } else if (!strcasecmp(key, "HeaderProtectionKey")) {
            if (*eq) header_key = 1;
        } else if (!strcasecmp(key, "MTU")) {
            long mtu = strtol(eq, NULL, 10);
            if (mtu >= 576 && mtu <= 1500) {
                if (!mt_meta) out->mtu = (int)mtu;
                mt_line = 1;
            } else {
                snprintf(error, error_size,
                    "MTU must be between 576 and 1500");
                return -1;
            }
        } else if (!strcasecmp(key, "DNS")) {
            if (!dns_line_seen) {
                snprintf(out->dns, sizeof(out->dns), "%s", eq);
                dns_line_seen = 1;
            }
        }
    }

    if (section == SECTION_PEER)
        finish_peer(&peers, &peers_ok, has_pub, has_ep);

    if (hooks) {
        snprintf(error, error_size, "script hooks are not supported");
        return -1;
    }
    if (header_key) {
        int i;
        for (i = 0; i < 4; i++) {
            if (!s_seen[i] || s_val[i] < 12) {
                snprintf(error, error_size,
                    "HeaderProtectionKey requires S%d of at least 12",
                    i + 1);
                return -1;
            }
        }
    }
    if (!has_private) {
        snprintf(error, error_size, "[Interface] PrivateKey is required");
        return -1;
    }
    if (peers < 1 || peers_ok < 1) {
        snprintf(error, error_size,
            "at least one [Peer] needs PublicKey and Endpoint");
        return -1;
    }
    (void)mt_line;

    if (!type_seen)
        snprintf(out->type, sizeof(out->type), "%s", awg_seen ? "awg" : "wg");
    if (strcmp(out->type, "wg") && strcmp(out->type, "awg")) {
        snprintf(error, error_size, "type must be wg or awg");
        return -1;
    }
    snprintf(out->ifname, sizeof(out->ifname), "%s",
        !strcmp(out->type, "awg") ? "awg0" : "wg0");
    return 0;
}

int vpn_conf_parse_file(const char *path, vpn_conf_t *out,
    char *error, size_t error_size)
{
    FILE *file;
    char text[VPN_CONF_MAX_BYTES + 1];
    size_t used;

    if (!path) return -1;
    file = fopen(path, "r");
    if (!file) {
        if (error && error_size)
            snprintf(error, error_size, "cannot open the profile");
        return -1;
    }
    used = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    if (used >= VPN_CONF_MAX_BYTES) {
        if (error && error_size)
            snprintf(error, error_size,
                "the configuration is larger than %d bytes",
                VPN_CONF_MAX_BYTES);
        return -1;
    }
    text[used] = '\0';
    return vpn_conf_parse_text(text, used, out, error, error_size);
}

/* Rewrites the type token of the first "# oko:" line; inserts the line when
 * the configuration has none. */
int vpn_conf_set_type(char *text, size_t capacity, const char *type)
{
    char *line, *next;
    char output[VPN_CONF_MAX_BYTES + VPN_CONF_LINE_MAX];
    size_t out_len = 0;
    int replaced = 0;
    int first = 1;

    if (!text || !type || (strcmp(type, "wg") && strcmp(type, "awg") &&
        strcmp(type, "ovpn")))
        return -1;

    line = text;
    while (*line) {
        char copy[VPN_CONF_LINE_MAX];
        char newline[VPN_CONF_LINE_MAX];
        char *trimmed, *p;

        next = strchr(line, '\n');
        {
            size_t len = next ? (size_t)(next - line) : strlen(line);
            if (len >= sizeof(copy)) return -1;
            memcpy(copy, line, len);
            copy[len] = '\0';
        }

        trimmed = copy;
        while (*trimmed && ascii_space((unsigned char)*trimmed)) trimmed++;
        if (*trimmed == '#') {
            p = trimmed + 1;
            while (*p && ascii_space((unsigned char)*p)) p++;
            if (!strncmp(p, "oko:", 4) && !replaced) {
                char tokens[VPN_CONF_LINE_MAX];
                char *token, *save = NULL;
                size_t used = 0;

                snprintf(tokens, sizeof(tokens), "%s", p + 4);
                used += (size_t)snprintf(newline + used,
                    sizeof(newline) - used, "# oko:");
                for (token = strtok_r(tokens, " \t", &save); token;
                    token = strtok_r(NULL, " \t", &save)) {
                    char *eq = strchr(token, '=');
                    if (eq && !strncmp(token, "type=", 5)) continue;
                    used += (size_t)snprintf(newline + used,
                        sizeof(newline) - used, " %.32s", token);
                    if (used >= sizeof(newline) - 2) return -1;
                }
                used += (size_t)snprintf(newline + used,
                    sizeof(newline) - used, " type=%.4s", type);
                if (used >= sizeof(newline)) return -1;
                if (first) {
                    if (out_len + used + 1 >= sizeof(output)) return -1;
                    memcpy(output + out_len, newline, used);
                    out_len += used;
                    output[out_len++] = '\n';
                } else {
                    if (out_len + used + 1 >= sizeof(output)) return -1;
                    memcpy(output + out_len, newline, used);
                    out_len += used;
                    output[out_len++] = '\n';
                }
                replaced = 1;
                first = 0;
                line = next ? next + 1 : line + strlen(line);
                continue;
            }
        }

        {
            size_t len = strlen(copy);
            if (out_len + len + 1 >= sizeof(output)) return -1;
            memcpy(output + out_len, copy, len);
            out_len += len;
            output[out_len++] = '\n';
        }
        first = 0;
        line = next ? next + 1 : line + strlen(line);
    }

    if (!replaced) {
        char prefix[64];
        int len = snprintf(prefix, sizeof(prefix), "# oko: type=%s\n", type);
        if (len < 0 || (size_t)len + out_len + 1 >= sizeof(output)) return -1;
        memmove(output + len, output, out_len);
        memcpy(output, prefix, (size_t)len);
        out_len += (size_t)len;
    }
    if (out_len + 1 >= capacity) return -1;
    memcpy(text, output, out_len);
    text[out_len] = '\0';
    return 0;
}

/* Adds "def1" to a bare "redirect-gateway" line. */
int vpn_conf_normalize_ovpn(char *text, size_t capacity)
{
    char *line, *next;
    char output[VPN_CONF_MAX_BYTES + VPN_CONF_LINE_MAX];
    size_t out_len = 0;

    if (!text) return -1;

    line = text;
    while (*line) {
        char copy[VPN_CONF_LINE_MAX];
        size_t len;
        char *trimmed, *rest;

        next = strchr(line, '\n');
        len = next ? (size_t)(next - line) : strlen(line);
        if (len >= sizeof(copy)) return -1;
        memcpy(copy, line, len);
        copy[len] = '\0';

        trimmed = copy;
        while (*trimmed && ascii_space((unsigned char)*trimmed)) trimmed++;
        if (!strncmp(trimmed, "redirect-gateway", 16) &&
            (trimmed[16] == '\0' || ascii_space((unsigned char)trimmed[16]))) {
            int has_def1 = 0;
            char flags[VPN_CONF_LINE_MAX];
            char scan[VPN_CONF_LINE_MAX];
            char *token, *save = NULL;

            rest = trimmed + 16;
            while (*rest && ascii_space((unsigned char)*rest)) rest++;
            snprintf(flags, sizeof(flags), "%s", rest);
            snprintf(scan, sizeof(scan), "%s", rest);
            for (token = strtok_r(scan, " \t\r", &save); token;
                token = strtok_r(NULL, " \t\r", &save)) {
                if (!strcmp(token, "def1")) has_def1 = 1;
            }
            if (!has_def1) {
                if (flags[0])
                    len = (size_t)snprintf(copy, sizeof(copy),
                        "redirect-gateway def1 %s", flags);
                else
                    len = (size_t)snprintf(copy, sizeof(copy),
                        "redirect-gateway def1");
                if (len >= sizeof(copy)) return -1;
            }
        }
        if (out_len + len + 1 >= sizeof(output)) return -1;
        memcpy(output + out_len, copy, len);
        out_len += len;
        output[out_len++] = '\n';
        line = next ? next + 1 : line + strlen(line);
    }

    if (out_len + 1 >= capacity) return -1;
    memcpy(text, output, out_len);
    text[out_len] = '\0';
    return 0;
}