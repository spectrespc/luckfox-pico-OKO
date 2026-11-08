#define _GNU_SOURCE

#include "vpn.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define VPN_DIR "/userdata/vpn"
#define VPN_STATE_DIR "/userdata/vpn/state"
#define VPN_HELPER "/usr/bin/oko-vpn"
#define VPN_MAX_PROFILES 3
#define VPN_STATUS_BUF 8192
#define VPN_CREDS_MAX 256

#define VPN_ERR(reason)                                                   \
    do {                                                                  \
        if (error && error_size)                                          \
            snprintf(error, error_size, "%s", (reason));                  \
    } while (0)

typedef struct {
    char name[VPN_PROFILE_NAME_MAX];
    char type[VPN_TYPE_MAX];
    char ifname[VPN_IFNAME_MAX];
    char endpoint[VPN_ENDPOINT_MAX];
    char allowedips[VPN_ALLOWED_MAX];
    char routes[VPN_ADDR_MAX];
    char tunip[VPN_ENDPOINT_MAX];
    int auto_start;
    int fulltunnel;
    int valid;
    int state_up;
    int guard_reverted;
    long handshake_age;
    unsigned long long tx;
    unsigned long long rx;
    int mtu;
} vpn_live_t;

typedef struct {
    char *buffer;
    size_t capacity;
    size_t used;
    int failed;
} vpn_sbuf_t;

static char vpnToken[17];

static int file_exists(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0;
}

static int file_executable(const char *path)
{
    return path && access(path, X_OK) == 0;
}

static int tool_path(const char *name, char *out, size_t out_size)
{
    static const char *dirs[] = { "/oem/usr/bin", "/usr/bin", "/usr/sbin",
        "/sbin", "/bin" };
    size_t i;
    char path[256];

    for (i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        snprintf(path, sizeof(path), "%s/%s", dirs[i], name);
        if (file_executable(path)) {
            snprintf(out, out_size, "%s", path);
            return 0;
        }
    }
    return -1;
}

/* The token only guards destructive actions; it lives for the process and is
 * regenerated when requested through the API. */
static void vpn_token_generate(void)
{
    static const char hex[] = "0123456789abcdef";
    unsigned char raw[8];
    int filled = 0;
    FILE *random = fopen("/dev/urandom", "rb");
    size_t i;

    if (random) {
        filled = fread(raw, 1, sizeof(raw), random) == sizeof(raw);
        fclose(random);
    }
    if (!filled) {
        srand((unsigned)time(NULL) ^ (unsigned)getpid());
        for (i = 0; i < sizeof(raw); i++) raw[i] = (unsigned char)rand();
    }
    for (i = 0; i < sizeof(raw); i++) {
        vpnToken[i * 2] = hex[raw[i] >> 4];
        vpnToken[i * 2 + 1] = hex[raw[i] & 0x0f];
    }
    vpnToken[16] = '\0';
}

const char *vpn_token(void)
{
    if (!vpnToken[0]) vpn_token_generate();
    return vpnToken;
}

int vpn_available(void)
{
    char path[256];

    if (!file_executable(VPN_HELPER)) return 0;
    if (tool_path("wg", path, sizeof(path)) == 0) return 1;
    if (tool_path("awg", path, sizeof(path)) == 0) return 1;
    if (tool_path("openvpn", path, sizeof(path)) == 0) return 1;
    return 0;
}

static int capture(const char *command, char *output, size_t output_size)
{
    FILE *pipe;
    size_t length;

    if (!output || output_size == 0) return -1;
    output[0] = '\0';
    pipe = popen(command, "r");
    if (!pipe) return -1;
    length = fread(output, 1, output_size - 1, pipe);
    output[length] = '\0';
    return pclose(pipe) == 0 ? 0 : -1;
}

static void sb_init(vpn_sbuf_t *sb, char *buffer, size_t capacity)
{
    sb->buffer = buffer;
    sb->capacity = capacity;
    sb->used = 0;
    sb->failed = 0;
    if (capacity) buffer[0] = '\0';
}

static void sb_puts(vpn_sbuf_t *sb, const char *text)
{
    size_t len;

    if (sb->failed || !text) return;
    len = strlen(text);
    if (sb->used + len + 1 >= sb->capacity) {
        sb->failed = 1;
        return;
    }
    memcpy(sb->buffer + sb->used, text, len);
    sb->used += len;
    sb->buffer[sb->used] = '\0';
}

static void sb_printf(vpn_sbuf_t *sb, const char *format, ...)
{
    va_list args;
    int written;

    if (sb->failed) return;
    va_start(args, format);
    written = vsnprintf(sb->buffer + sb->used, sb->capacity - sb->used,
        format, args);
    va_end(args);
    if (written < 0 || (size_t)written >= sb->capacity - sb->used) {
        sb->failed = 1;
        return;
    }
    sb->used += (size_t)written;
}

static void sb_json_string(vpn_sbuf_t *sb, const char *value)
{
    const unsigned char *p = (const unsigned char *)(value ? value : "");

    sb_puts(sb, "\"");
    for (; *p; p++) {
        switch (*p) {
        case '"': sb_puts(sb, "\\\""); break;
        case '\\': sb_puts(sb, "\\\\"); break;
        case '\n': sb_puts(sb, "\\n"); break;
        case '\r': sb_puts(sb, "\\r"); break;
        case '\t': sb_puts(sb, "\\t"); break;
        default:
            if (*p < 0x20) sb_printf(sb, "\\u%04x", *p);
            else {
                char one[2] = { (char)*p, '\0' };
                sb_puts(sb, one);
            }
        }
    }
    sb_puts(sb, "\"");
}

static void profile_clear(vpn_live_t *profile)
{
    memset(profile, 0, sizeof(*profile));
    profile->handshake_age = -1;
}

static int parse_status_line(const char *line, vpn_live_t *profile)
{
    char copy[VPN_STATUS_BUF];
    char *token, *save = NULL;

    if (strncmp(line, "profile=", 8)) return -1;
    profile_clear(profile);
    snprintf(copy, sizeof(copy), "%s", line);
    for (token = strtok_r(copy, " \t\r\n", &save); token;
        token = strtok_r(NULL, " \t\r\n", &save)) {
        char *eq = strchr(token, '=');
        const char *value;

        if (!eq) continue;
        *eq = '\0';
        value = eq + 1;
        if (!strcmp(token, "name") || !strcmp(token, "profile"))
            snprintf(profile->name, sizeof(profile->name), "%s", value);
        else if (!strcmp(token, "type"))
            snprintf(profile->type, sizeof(profile->type), "%s", value);
        else if (!strcmp(token, "ifname"))
            snprintf(profile->ifname, sizeof(profile->ifname), "%s", value);
        else if (!strcmp(token, "endpoint"))
            snprintf(profile->endpoint, sizeof(profile->endpoint), "%s",
                value);
        else if (!strcmp(token, "allowedips"))
            snprintf(profile->allowedips, sizeof(profile->allowedips), "%s",
                value);
        else if (!strcmp(token, "routes"))
            snprintf(profile->routes, sizeof(profile->routes), "%s", value);
        else if (!strcmp(token, "auto"))
            profile->auto_start = !strcmp(value, "1");
        else if (!strcmp(token, "fulltunnel"))
            profile->fulltunnel = !strcmp(value, "1");
        else if (!strcmp(token, "valid"))
            profile->valid = !strcmp(value, "1");
        else if (!strcmp(token, "guard_reverted"))
            profile->guard_reverted = !strcmp(value, "1");
        else if (!strcmp(token, "state"))
            profile->state_up = !strcmp(value, "up");
        else if (!strcmp(token, "handshake_age"))
            profile->handshake_age = strtol(value, NULL, 10);
        else if (!strcmp(token, "tx"))
            profile->tx = strtoull(value, NULL, 10);
        else if (!strcmp(token, "rx"))
            profile->rx = strtoull(value, NULL, 10);
        else if (!strcmp(token, "mtu"))
            profile->mtu = (int)strtol(value, NULL, 10);
        else if (!strcmp(token, "tunip"))
            snprintf(profile->tunip, sizeof(profile->tunip), "%s", value);
    }
    return profile->name[0] ? 0 : -1;
}

static size_t load_profiles(vpn_live_t *profiles, size_t max)
{
    char output[VPN_STATUS_BUF];
    char *line, *save = NULL;
    size_t count = 0;

    if (capture(VPN_HELPER " status", output, sizeof(output)) != 0 &&
        !output[0])
        return 0;

    for (line = strtok_r(output, "\n", &save); line && count < max;
        line = strtok_r(NULL, "\n", &save)) {
        vpn_live_t parsed;

        if (parse_status_line(line, &parsed) == 0)
            profiles[count++] = parsed;
    }
    return count;
}

static void append_profiles_json(vpn_sbuf_t *sb, const vpn_live_t *profiles,
    size_t count)
{
    size_t i;

    sb_puts(sb, "\"profiles\":[");
    for (i = 0; i < count; i++) {
        const vpn_live_t *p = &profiles[i];

        if (i) sb_puts(sb, ",");
        sb_puts(sb, "{");
        sb_puts(sb, "\"name\":"); sb_json_string(sb, p->name);
        sb_puts(sb, ",\"type\":"); sb_json_string(sb, p->type);
        sb_puts(sb, ",\"ifname\":"); sb_json_string(sb, p->ifname);
        sb_printf(sb, ",\"auto\":%s", p->auto_start ? "true" : "false");
        sb_puts(sb, ",\"endpoint\":"); sb_json_string(sb, p->endpoint);
        sb_puts(sb, ",\"allowedips\":"); sb_json_string(sb, p->allowedips);
        sb_printf(sb, ",\"fulltunnel\":%s",
            p->fulltunnel ? "true" : "false");
        sb_printf(sb, ",\"valid\":%s", p->valid ? "true" : "false");
        sb_printf(sb, ",\"guard_reverted\":%s",
            p->guard_reverted ? "true" : "false");
        sb_puts(sb, ",\"state\":");
        sb_json_string(sb, p->state_up ? "up" : "down");
        sb_puts(sb, "}");
    }
    sb_puts(sb, "]");
}

static void append_live_json(vpn_sbuf_t *sb, const vpn_live_t *profiles,
    size_t count)
{
    size_t i;
    int first = 1;

    sb_puts(sb, "\"live\":{");
    for (i = 0; i < count; i++) {
        const vpn_live_t *p = &profiles[i];
        char routes[VPN_ADDR_MAX];
        char *route, *save = NULL;
        int route_first = 1;

        if (!p->state_up) continue;
        if (!first) sb_puts(sb, ",");
        first = 0;
        sb_json_string(sb, p->ifname[0] ? p->ifname : p->name);
        sb_puts(sb, ":{");
        if (!strcmp(p->type, "ovpn"))
            sb_puts(sb, "\"handshake_age\":null,");
        else
            sb_printf(sb, "\"handshake_age\":%ld,", p->handshake_age);
        sb_printf(sb, "\"tx\":%llu,\"rx\":%llu,", p->tx, p->rx);
        sb_printf(sb, "\"mtu\":%d,", p->mtu);
        sb_puts(sb, "\"tunip\":");
        sb_json_string(sb, p->tunip);
        sb_puts(sb, ",");
        sb_puts(sb, "\"routes\":[");
        snprintf(routes, sizeof(routes), "%s", p->routes);
        for (route = strtok_r(routes, " ", &save); route;
            route = strtok_r(NULL, " ", &save)) {
            if (!route_first) sb_puts(sb, ",");
            route_first = 0;
            sb_json_string(sb, route);
        }
        sb_puts(sb, "]}");
    }
    sb_puts(sb, "}");
}

size_t vpn_status_json(char *buffer, size_t capacity)
{
    vpn_live_t profiles[VPN_MAX_PROFILES];
    size_t count;
    vpn_sbuf_t sb;
    char wg[256] = "", awg[256] = "", ovpn[256] = "";

    if (!buffer || capacity < 64) return 0;
    sb_init(&sb, buffer, capacity);
    count = load_profiles(profiles, VPN_MAX_PROFILES);

    sb_puts(&sb, "{\"available\":{");
    sb_printf(&sb, "\"module_awg\":%s",
        (file_exists("/oem/usr/ko/amneziawg.ko") ||
            file_exists("/sys/module/amneziawg")) ? "true" : "false");
    sb_printf(&sb, ",\"wg\":%s",
        tool_path("wg", wg, sizeof(wg)) == 0 ? "true" : "false");
    sb_printf(&sb, ",\"awg_bin\":%s",
        tool_path("awg", awg, sizeof(awg)) == 0 ? "true" : "false");
    sb_printf(&sb, ",\"ovpn_bin\":%s",
        tool_path("openvpn", ovpn, sizeof(ovpn)) == 0 ? "true" : "false");
    sb_printf(&sb, ",\"helper\":%s",
        file_executable(VPN_HELPER) ? "true" : "false");
    sb_puts(&sb, "},\"guard\":{\"revert_seconds\":15},\"token\":");
    sb_json_string(&sb, vpn_token());
    sb_puts(&sb, ",");
    append_profiles_json(&sb, profiles, count);
    sb_puts(&sb, ",");
    append_live_json(&sb, profiles, count);
    sb_puts(&sb, "}");

    if (sb.failed) return 0;
    return sb.used;
}

static int mkdir_profile_dir(void)
{
    struct stat st;

    if (stat(VPN_DIR, &st) == 0) return S_ISDIR(st.st_mode) ? 0 : -1;
    if (mkdir(VPN_DIR, 0700) != 0 && errno != EEXIST) return -1;
    if (mkdir(VPN_STATE_DIR, 0700) != 0 && errno != EEXIST) {
        /* The state directory is created lazily by the helper as well. */
    }
    return 0;
}

int vpn_import_text(const char *text, size_t length, const char *type_hint,
    const char *creds_user, const char *creds_pass,
    char *name, size_t name_size, char *error, size_t error_size)
{
    vpn_conf_t conf;
    char copy[VPN_CONF_MAX_BYTES + VPN_CONF_LINE_MAX];
    char path[256], tmp[320], pass_path[256], pass_tmp[320];
    FILE *file;
    const char *hint = type_hint ? type_hint : "";
    int need_pass = 0;

    if (!text || length == 0) {
        VPN_ERR("the configuration is empty");
        return -1;
    }
    if (length > VPN_CONF_MAX_BYTES) {
        VPN_ERR("the configuration is larger than 8 KiB");
        return -1;
    }
    if (memchr(text, '\0', length)) {
        VPN_ERR("the configuration contains a null byte");
        return -1;
    }
    if (length >= sizeof(copy)) {
        VPN_ERR("the configuration is too large");
        return -1;
    }
    memcpy(copy, text, length);
    copy[length] = '\0';

    if (vpn_conf_parse_text(copy, length, &conf, error, error_size)) return -1;

    if (*hint && strcmp(hint, "auto") && strcmp(hint, "wg") &&
        strcmp(hint, "awg") && strcmp(hint, "ovpn")) {
        VPN_ERR("type must be auto, wg, awg or ovpn");
        return -1;
    }
    if (*hint && strcmp(hint, "auto") && strcmp(hint, conf.type)) {
        if (vpn_conf_set_type(copy, sizeof(copy), hint)) {
            VPN_ERR("the configuration is too large to rewrite");
            return -1;
        }
        if (vpn_conf_parse_text(copy, strlen(copy), &conf, error,
                error_size))
            return -1;
    }

    if (!strcmp(conf.type, "ovpn")) {
        if (vpn_conf_normalize_ovpn(copy, sizeof(copy))) {
            VPN_ERR("the configuration is too large to rewrite");
            return -1;
        }
        if (vpn_conf_parse_text(copy, strlen(copy), &conf, error,
                error_size))
            return -1;
        if (conf.needs_credentials) {
            if (!creds_user || !*creds_user) {
                VPN_ERR("credentials required: set the user and password "
                    "for this OpenVPN profile");
                return -1;
            }
            need_pass = 1;
        }
    }

    if (!vpn_conf_valid_name(conf.ifname)) {
        VPN_ERR("the profile name is invalid");
        return -1;
    }
    if (mkdir_profile_dir()) {
        VPN_ERR("cannot create /userdata/vpn");
        return -1;
    }
    snprintf(path, sizeof(path), VPN_DIR "/%s.conf", conf.ifname);
    {
        struct stat st;
        char ifpath[128];

        snprintf(ifpath, sizeof(ifpath), "/sys/class/net/%s", conf.ifname);
        if (stat(ifpath, &st) == 0) {
            VPN_ERR("disconnect the profile before importing it");
            return -1;
        }
    }

    snprintf(tmp, sizeof(tmp), VPN_DIR "/.%s.tmp.%d", conf.ifname,
        (int)getpid());
    file = fopen(tmp, "w");
    if (!file) {
        VPN_ERR("cannot write the profile");
        return -1;
    }
    if (fwrite(copy, 1, strlen(copy), file) != strlen(copy)) {
        fclose(file);
        unlink(tmp);
        VPN_ERR("cannot write the profile");
        return -1;
    }
    fflush(file);
    fclose(file);
    chmod(tmp, 0600);
    if (rename(tmp, path)) {
        unlink(tmp);
        VPN_ERR("cannot save the profile");
        return -1;
    }

    snprintf(pass_path, sizeof(pass_path), VPN_DIR "/%s.pass", conf.ifname);
    if (need_pass) {
        size_t user_len = strlen(creds_user);
        size_t pass_len = creds_pass ? strlen(creds_pass) : 0;

        if (user_len > VPN_CREDS_MAX || pass_len > VPN_CREDS_MAX ||
            strpbrk(creds_user, "\r\n") ||
            (creds_pass && strpbrk(creds_pass, "\r\n"))) {
            unlink(path);
            VPN_ERR("the credentials are invalid");
            return -1;
        }
        snprintf(pass_tmp, sizeof(pass_tmp), VPN_DIR "/.%s.pass.%d",
            conf.ifname, (int)getpid());
        file = fopen(pass_tmp, "w");
        if (!file) {
            unlink(path);
            VPN_ERR("cannot write the credentials");
            return -1;
        }
        if (fprintf(file, "%s\n%s\n", creds_user,
                creds_pass ? creds_pass : "") < 0) {
            fclose(file);
            unlink(pass_tmp);
            unlink(path);
            VPN_ERR("cannot write the credentials");
            return -1;
        }
        fflush(file);
        fclose(file);
        chmod(pass_tmp, 0600);
        if (rename(pass_tmp, pass_path)) {
            unlink(pass_tmp);
            unlink(path);
            VPN_ERR("cannot save the credentials");
            return -1;
        }
    } else {
        unlink(pass_path);
    }

    if (name && name_size) snprintf(name, name_size, "%s", conf.ifname);
    return 0;
}

int vpn_action(const char *action, const char *profile, const char *token,
    char *error, size_t error_size)
{
    char command[320];

    if (!action || !*action) {
        VPN_ERR("the action is missing");
        return -1;
    }
    if (!profile || !vpn_conf_valid_name(profile)) {
        VPN_ERR("the profile name is invalid");
        return -1;
    }
    snprintf(command, sizeof(command), VPN_DIR "/%s.conf", profile);
    if (!file_exists(command)) {
        VPN_ERR("the profile does not exist");
        return -1;
    }

    if (!strcmp(action, "up")) {
        snprintf(command, sizeof(command),
            "( " VPN_HELPER " up '%s' ) </dev/null >>/var/log/vpn.log 2>&1 &",
            profile);
        if (system(command) != 0) {
            VPN_ERR("cannot queue the connection");
            return -1;
        }
        return 0;
    }
    if (!strcmp(action, "down")) {
        snprintf(command, sizeof(command), VPN_HELPER " down '%s'", profile);
    } else if (!strcmp(action, "enable")) {
        snprintf(command, sizeof(command), VPN_HELPER " set-auto '%s' 1",
            profile);
    } else if (!strcmp(action, "disable")) {
        snprintf(command, sizeof(command), VPN_HELPER " set-auto '%s' 0",
            profile);
    } else if (!strcmp(action, "delete")) {
        if (!token || strcmp(token, vpn_token())) {
            VPN_ERR("delete token is invalid");
            return -1;
        }
        snprintf(command, sizeof(command), VPN_HELPER " delete '%s'",
            profile);
    } else {
        VPN_ERR("unknown action");
        return -1;
    }

    if (system(command) != 0) {
        VPN_ERR("the helper failed; see /var/log/vpn.log");
        return -1;
    }
    return 0;
}

int vpn_export_path(const char *profile, char *path, size_t path_size,
    char *error, size_t error_size)
{
    struct stat st;

    if (!profile || !vpn_conf_valid_name(profile)) {
        VPN_ERR("the profile name is invalid");
        return -1;
    }
    snprintf(path, path_size, VPN_DIR "/%s.conf", profile);
    if (lstat(path, &st) || !S_ISREG(st.st_mode)) {
        VPN_ERR("the profile does not exist");
        return -1;
    }
    return 0;
}