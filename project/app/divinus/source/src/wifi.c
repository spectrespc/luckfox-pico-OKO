#define _GNU_SOURCE

#include "wifi.h"

#include <arpa/inet.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define WIFI_HELPER "/usr/bin/oko-wifi"
#define WIFI_SCAN_CACHE "/tmp/wifi_scan.json"

#define WIFI_ERR(error, size, reason)                                      \
    do {                                                                   \
        if (error && size)                                                 \
            snprintf(error, size, "%s", (reason));                         \
    } while (0)

typedef struct {
    char mode[8];
    char ssid[128];
    char psk_set[4];
    char dhcp[4];
    char ipaddr[16];
    char netmask[16];
    char gateway[16];
    char dns[16];
    char metric[8];
    char ap_ssid[128];
    char ap_psk[128];
    char ap_ssid_effective[128];
    char ap_addr[16];
    char wlan_present[6];
    char wpa_state[32];
    char current_ip[16];
    char driver[32];
    char usb_id[24];
    char ap_active[6];
    char stations[8];
    char chip_supports_ap_st[6];
} wifi_state_t;

typedef struct {
    char *buffer;
    size_t capacity;
    size_t used;
    int failed;
} wifi_buf_t;

static void buf_printf(wifi_buf_t *w, const char *fmt, ...)
{
    va_list args;
    int written;

    if (w->failed || w->used >= w->capacity) {
        w->failed = 1;
        return;
    }
    va_start(args, fmt);
    written = vsnprintf(w->buffer + w->used, w->capacity - w->used, fmt, args);
    va_end(args);
    if (written < 0 || (size_t)written >= w->capacity - w->used) {
        w->failed = 1;
        return;
    }
    w->used += (size_t)written;
}

static void buf_string(wifi_buf_t *w, const char *value)
{
    size_t i;

    if (w->failed) return;
    for (i = 0; value && value[i]; i++) {
        unsigned char c = (unsigned char)value[i];
        if (c == '"' || c == '\\') {
            buf_printf(w, "\\%c", c);
        } else if (c < 0x20) {
            buf_printf(w, "\\u%04x", c);
        } else {
            buf_printf(w, "%c", c);
        }
        if (w->failed) return;
    }
}

static size_t buf_finish(wifi_buf_t *w)
{
    if (w->failed) return 0;
    return w->used;
}

int wifi_available(void)
{
    return access(WIFI_HELPER, X_OK) == 0;
}

static void set_field(char *dst, size_t size, const char *value)
{
    if (size) snprintf(dst, size, "%s", value ? value : "");
}

static int wifi_load_state(wifi_state_t *state)
{
    FILE *pipe;
    char line[256];
    int rc;

    memset(state, 0, sizeof(*state));
    set_field(state->mode, sizeof(state->mode), "off");
    set_field(state->dhcp, sizeof(state->dhcp), "on");
    set_field(state->metric, sizeof(state->metric), "600");

    pipe = popen(WIFI_HELPER " env 2>/dev/null", "r");
    if (!pipe) return -1;

    while (fgets(line, sizeof(line), pipe)) {
        char *value = strchr(line, '=');
        char *end;

        if (!value) continue;
        *value++ = '\0';
        end = value + strlen(value);
        while (end > value && (end[-1] == '\n' || end[-1] == '\r'))
            *--end = '\0';

        if (!strcmp(line, "mode"))
            set_field(state->mode, sizeof(state->mode), value);
        else if (!strcmp(line, "ssid"))
            set_field(state->ssid, sizeof(state->ssid), value);
        else if (!strcmp(line, "psk_set"))
            set_field(state->psk_set, sizeof(state->psk_set), value);
        else if (!strcmp(line, "dhcp"))
            set_field(state->dhcp, sizeof(state->dhcp), value);
        else if (!strcmp(line, "ipaddr"))
            set_field(state->ipaddr, sizeof(state->ipaddr), value);
        else if (!strcmp(line, "netmask"))
            set_field(state->netmask, sizeof(state->netmask), value);
        else if (!strcmp(line, "gateway"))
            set_field(state->gateway, sizeof(state->gateway), value);
        else if (!strcmp(line, "dns"))
            set_field(state->dns, sizeof(state->dns), value);
        else if (!strcmp(line, "metric"))
            set_field(state->metric, sizeof(state->metric), value);
        else if (!strcmp(line, "ap_ssid"))
            set_field(state->ap_ssid, sizeof(state->ap_ssid), value);
        else if (!strcmp(line, "ap_psk"))
            set_field(state->ap_psk, sizeof(state->ap_psk), value);
        else if (!strcmp(line, "ap_ssid_effective"))
            set_field(state->ap_ssid_effective,
                sizeof(state->ap_ssid_effective), value);
        else if (!strcmp(line, "ap_addr"))
            set_field(state->ap_addr, sizeof(state->ap_addr), value);
        else if (!strcmp(line, "wlan0_present"))
            set_field(state->wlan_present, sizeof(state->wlan_present), value);
        else if (!strcmp(line, "wpa_state"))
            set_field(state->wpa_state, sizeof(state->wpa_state), value);
        else if (!strcmp(line, "current_ip"))
            set_field(state->current_ip, sizeof(state->current_ip), value);
        else if (!strcmp(line, "driver"))
            set_field(state->driver, sizeof(state->driver), value);
        else if (!strcmp(line, "usb_id"))
            set_field(state->usb_id, sizeof(state->usb_id), value);
        else if (!strcmp(line, "ap_active"))
            set_field(state->ap_active, sizeof(state->ap_active), value);
        else if (!strcmp(line, "stations"))
            set_field(state->stations, sizeof(state->stations), value);
        else if (!strcmp(line, "chip_supports_ap_st"))
            set_field(state->chip_supports_ap_st,
                sizeof(state->chip_supports_ap_st), value);
    }
    rc = pclose(pipe);
    return rc == 0 ? 0 : -1;
}

static int flag_is_true(const char *value)
{
    return value && (!strcmp(value, "true") || !strcmp(value, "1"));
}

static void emit_string_field(wifi_buf_t *w, const char *name,
    const char *value, int comma)
{
    buf_printf(w, "%s\"%s\":\"", comma ? "," : "", name);
    buf_string(w, value);
    buf_printf(w, "\"");
}

static void emit_bool_field(wifi_buf_t *w, const char *name, int value,
    int comma)
{
    buf_printf(w, "%s\"%s\":%s", comma ? "," : "", name,
        value ? "true" : "false");
}

size_t wifi_status_json(char *buffer, size_t capacity)
{
    wifi_state_t state;
    wifi_buf_t w;

    if (!buffer || !capacity) return 0;
    if (wifi_load_state(&state) != 0) return 0;

    w.buffer = buffer;
    w.capacity = capacity;
    w.used = 0;
    w.failed = 0;

    buf_printf(&w, "{");
    emit_string_field(&w, "mode", state.mode, 0);
    emit_string_field(&w, "ssid", state.ssid, 1);
    emit_bool_field(&w, "psk_set", flag_is_true(state.psk_set), 1);
    emit_string_field(&w, "dhcp", state.dhcp, 1);
    emit_string_field(&w, "ipaddr", state.ipaddr, 1);
    emit_string_field(&w, "netmask", state.netmask, 1);
    emit_string_field(&w, "gateway", state.gateway, 1);
    emit_string_field(&w, "dns", state.dns, 1);
    buf_printf(&w, ",\"metric\":%d",
        state.metric[0] ? atoi(state.metric) : 600);
    emit_string_field(&w, "ap_ssid", state.ap_ssid, 1);
    emit_string_field(&w, "ap_psk", state.ap_psk, 1);
    emit_string_field(&w, "ap_ssid_effective", state.ap_ssid_effective, 1);
    emit_string_field(&w, "ap_addr", state.ap_addr, 1);
    emit_bool_field(&w, "wlan0_present", flag_is_true(state.wlan_present), 1);
    emit_string_field(&w, "wpa_state", state.wpa_state, 1);
    emit_string_field(&w, "current_ip", state.current_ip, 1);
    emit_string_field(&w, "driver", state.driver, 1);
    emit_string_field(&w, "usb_id", state.usb_id, 1);
    emit_bool_field(&w, "ap_active", flag_is_true(state.ap_active), 1);
    buf_printf(&w, ",\"stations\":%d",
        state.stations[0] ? atoi(state.stations) : 0);
    emit_bool_field(&w, "chip_supports_ap_st",
        flag_is_true(state.chip_supports_ap_st), 1);
    buf_printf(&w, "}");

    return buf_finish(&w);
}

size_t wifi_apinfo_json(char *buffer, size_t capacity)
{
    wifi_state_t state;
    wifi_buf_t w;

    if (!buffer || !capacity) return 0;
    if (wifi_load_state(&state) != 0) return 0;

    w.buffer = buffer;
    w.capacity = capacity;
    w.used = 0;
    w.failed = 0;

    buf_printf(&w, "{");
    emit_string_field(&w, "ssid", state.ap_ssid_effective, 0);
    emit_string_field(&w, "psk", state.ap_psk, 1);
    emit_string_field(&w, "address", state.ap_addr, 1);
    emit_bool_field(&w, "active", flag_is_true(state.ap_active), 1);
    buf_printf(&w, ",\"stations\":%d",
        state.stations[0] ? atoi(state.stations) : 0);
    buf_printf(&w, "}");

    return buf_finish(&w);
}

size_t wifi_scan_json(char *buffer, size_t capacity)
{
    FILE *file;
    size_t length;

    if (!buffer || !capacity) return 0;

    file = fopen(WIFI_SCAN_CACHE, "r");
    if (file) {
        length = fread(buffer, 1, capacity - 1, file);
        fclose(file);
        buffer[length] = '\0';
        /* A truncated object is not valid JSON: report "idle" instead so the
         * WebUI retries the scan rather than failing to parse the response. */
        if (length > 1 && buffer[0] == '{' && length < capacity - 1)
            return length;
    }

    length = (size_t)snprintf(buffer, capacity,
        "{\"state\":\"idle\",\"timestamp\":0,\"results\":[]}");
    return length < capacity ? length : 0;
}

int wifi_scan_start(char *error, size_t error_size)
{
    wifi_state_t state;

    if (!wifi_available()) {
        WIFI_ERR(error, error_size, "the Wi-Fi helper is not installed");
        return -1;
    }
    if (wifi_load_state(&state) == 0) {
        if (!flag_is_true(state.wlan_present)) {
            WIFI_ERR(error, error_size, "no USB Wi-Fi adapter is present");
            return -1;
        }
        if (flag_is_true(state.ap_active)) {
            WIFI_ERR(error, error_size,
                "the hotspot is active; switch to station mode first");
            return -1;
        }
    }
    if (system("( " WIFI_HELPER " scan json >/dev/null 2>&1 & )") != 0) {
        WIFI_ERR(error, error_size, "cannot start the scan");
        return -1;
    }
    return 0;
}

static int valid_text(const char *value, size_t max_length)
{
    size_t i;
    size_t length = strlen(value);

    if (length > max_length) return 0;
    for (i = 0; i < length; i++) {
        unsigned char c = (unsigned char)value[i];
        if (c < 0x20 || c == 0x7f || c == '\'' || c == '"' ||
            c == '\\')
            return 0;
    }
    return 1;
}

static int valid_ipv4(const char *value)
{
    struct in_addr address;
    return value && *value && inet_pton(AF_INET, value, &address) == 1;
}

/* Empty keeps the stored priority; otherwise 1..99999 as decimal digits. */
static int valid_metric(const char *value)
{
    size_t i;

    if (!value || !*value) return 1;
    if (strlen(value) > 5) return 0;
    for (i = 0; value[i]; i++)
        if (value[i] < '0' || value[i] > '9') return 0;
    return atoi(value) >= 1;
}

int wifi_save(const char *mode, const char *ssid, const char *psk,
    const char *dhcp, const char *ip, const char *mask, const char *gateway,
    const char *dns, const char *ap_ssid, const char *ap_psk,
    const char *metric, char *error, size_t error_size)
{
    char command[768];
    int rc;
    int status;

    if (!mode || (strcmp(mode, "sta") && strcmp(mode, "ap") &&
            strcmp(mode, "both") && strcmp(mode, "off"))) {
        WIFI_ERR(error, error_size, "mode must be sta, ap, both or off");
        return -1;
    }
    if (!valid_text(ssid ? ssid : "", 32)) {
        WIFI_ERR(error, error_size, "invalid SSID");
        return -1;
    }
    if (psk && *psk && !valid_text(psk, 63)) {
        WIFI_ERR(error, error_size, "invalid PSK");
        return -1;
    }
    if (psk && *psk && strlen(psk) < 8) {
        WIFI_ERR(error, error_size, "the PSK must be 8 to 63 characters");
        return -1;
    }
    if (!dhcp || (strcmp(dhcp, "on") && strcmp(dhcp, "off"))) {
        WIFI_ERR(error, error_size, "dhcp must be on or off");
        return -1;
    }
    if (ip && *ip && !valid_ipv4(ip)) {
        WIFI_ERR(error, error_size, "invalid IP address");
        return -1;
    }
    if (mask && *mask && !valid_ipv4(mask)) {
        WIFI_ERR(error, error_size, "invalid netmask");
        return -1;
    }
    if (!strcmp(dhcp, "off") && (!ip || !*ip || !mask || !*mask)) {
        WIFI_ERR(error, error_size, "static mode needs an address and netmask");
        return -1;
    }
    if (gateway && *gateway && !valid_ipv4(gateway)) {
        WIFI_ERR(error, error_size, "invalid gateway address");
        return -1;
    }
    if (dns && *dns && !valid_ipv4(dns)) {
        WIFI_ERR(error, error_size, "invalid DNS address");
        return -1;
    }
    if (!valid_text(ap_ssid ? ap_ssid : "", 32)) {
        WIFI_ERR(error, error_size, "invalid hotspot SSID");
        return -1;
    }
    if (ap_psk && *ap_psk && !valid_text(ap_psk, 63)) {
        WIFI_ERR(error, error_size, "invalid hotspot passphrase");
        return -1;
    }
    if (ap_psk && *ap_psk && strlen(ap_psk) < 8) {
        WIFI_ERR(error, error_size,
            "the hotspot passphrase must be 8 to 63 characters");
        return -1;
    }
    if (!valid_metric(metric)) {
        WIFI_ERR(error, error_size,
            "the priority must be 1 to 99999");
        return -1;
    }

    /* Empty passphrases keep the stored value; the helper treats "-" that
     * way.  All values are quote- and control-free (validated above). */
    snprintf(command, sizeof(command),
        WIFI_HELPER " set '%s' '%s' '%s' '%s' '%s' '%s' '%s' '%s' '%s' '%s' '%s'",
        mode,
        ssid ? ssid : "",
        (psk && *psk) ? psk : "-",
        dhcp,
        ip ? ip : "",
        mask ? mask : "",
        gateway ? gateway : "",
        dns ? dns : "",
        ap_ssid ? ap_ssid : "",
        (ap_psk && *ap_psk) ? ap_psk : "-",
        (metric && *metric) ? metric : "-");

    rc = system(command);
    if (rc == -1 || !WIFEXITED(rc)) {
        WIFI_ERR(error, error_size, "cannot run the Wi-Fi helper");
        return -3;
    }
    status = WEXITSTATUS(rc);
    if (status == 0) return 0;
    if (status == 2) {
        WIFI_ERR(error, error_size,
            "this adapter cannot run AP and STA at the same time");
        return -2;
    }
    WIFI_ERR(error, error_size, "cannot write the U-Boot environment");
    return -3;
}

int wifi_save_metric(const char *metric, char *error, size_t error_size)
{
    char command[128];
    int rc;

    if (!valid_metric(metric) || !metric || !*metric) {
        WIFI_ERR(error, error_size, "the priority must be 1 to 99999");
        return -1;
    }
    snprintf(command, sizeof(command), WIFI_HELPER " setmetric '%s'", metric);
    rc = system(command);
    if (rc == -1 || !WIFEXITED(rc) || WEXITSTATUS(rc) != 0) {
        WIFI_ERR(error, error_size, "cannot write the U-Boot environment");
        return -3;
    }
    return 0;
}