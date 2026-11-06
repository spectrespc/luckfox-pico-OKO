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

    if (!text || !type || (strcmp(type, "wg") && strcmp(type, "awg")))
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