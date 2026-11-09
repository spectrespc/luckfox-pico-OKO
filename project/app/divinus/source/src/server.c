#include "server.h"

#include <dirent.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include "flash.h"
#include "focus.h"
#include "focus_af.h"
#include "focus_geom.h"
#include "focus_home.h"
#include "focus_motor.h"
#include "focus_proto.h"
#include "firmware_status.h"
#include "gating.h"
#include "ivs_events.h"
#include "ntp.h"
#include "update.h"
#include "vpn.h"
#include "analytics/models.h"
#include "analytics/yolo.h"

#define HTTP_MAX_CLIENTS 50
#define HTTP_MIN_BUF_SIZE 4096
#define HTTP_MAX_BUF_SIZE (4 * 1024 * 1024)

IMPORT_STR(.rodata, "../res/index.html", indexhtml);
extern const char indexhtml[];
IMPORT_STR(.rodata, "../res/onvif/badauth.xml", badauthxml);
extern const char badauthxml[];

enum StreamType {
    STREAM_H26X  = (1 << 0),
    STREAM_JPEG  = (1 << 1),
    STREAM_MJPEG = (1 << 2),
    STREAM_MP3   = (1 << 3),
    STREAM_MP4   = (1 << 4),
    STREAM_PCM   = (1 << 5)
};

typedef struct {
    int clntFd;
    char *input, *method, *payload, *prot, *query, *uri;
    int paysize, total;
    size_t buf_size;
    int header_len;
    int prepared;               /* headers were inspected for a streamed body */
    int uploading;              /* POST /api/update/upload is in progress */
    long long upload_total;
    long long upload_received;
    time_t last_activity;
} http_request_t;

struct {
    int sockFd;
    enum StreamType type;
    struct Mp4State mp4;
    unsigned int nalCnt;
} client_fds[HTTP_MAX_CLIENTS];

typedef struct {
    char *name, *value;
} http_header_t;

typedef struct {
    int code;
    const char *msg, *desc;
} http_error_t;

const http_error_t http_errors[] = {
    {400, "Bad Request", "The server has no handler to the request."},
    {401, "Unauthorized", "You are not authorized to access this resource."},
    {403, "Forbidden", "You have been denied access to this resource."},
    {404, "Not Found", "The requested resource was not found."},
    {405, "Method Not Allowed", "This method is not handled on this endpoint."},
    {500, "Internal Server Error", "An invalid operation was caught on this request."},
    {501, "Not Implemented", "The server does not support the functionality."}
};
http_header_t http_headers[17] = {{"\0", "\0"}};

int server_fd = -1;
pthread_t server_thread_id;
pthread_mutex_t client_fds_mutex;

static bool is_local_address(const char *client_ip) {
    if (!client_ip) return false;

    if (!strcmp(client_ip, "127.0.0.1") ||
        !strncmp(client_ip, "127.", 4))
        return true;

    if (!strcmp(client_ip, "::1"))
        return true;

    if (!strncmp(client_ip, "::ffff:127.", 11))
        return true;

    return false;
}


/* Shared access checks.  The streamed update upload parses the raw header
 * block itself, so the whitelist and Basic-auth rules must live in one place
 * instead of being copied into that path. */
static bool ip_whitelisted(const char *client_ip)
{
    if (EMPTY(*app_config.web_whitelist))
        return true;
    if (!client_ip)
        return false;
    for (int i = 0; app_config.web_whitelist[i] &&
         *app_config.web_whitelist[i]; i++)
        if (ip_in_cidr(client_ip, app_config.web_whitelist[i]))
            return true;
    return false;
}

static bool credentials_valid(const char *auth)
{
    char cred[80], valid[192];

    if (!auth) return false;
    snprintf(cred, sizeof(cred), "%s:%s", app_config.web_auth_user,
        app_config.web_auth_pass);
    snprintf(valid, sizeof(valid), "Basic ");
    base64_encode(valid + 6, cred, strlen(cred));
    return !strcmp(auth, valid);
}

static char storageFormatToken[17];
static bool storageToolsChecked;
static bool storageToolsAvailable;

static void storage_token_generate(void) {
    static const char hex[] = "0123456789abcdef";
    unsigned char raw[8];
    bool filled = false;
    FILE *random = fopen("/dev/urandom", "rb");

    if (random) {
        filled = fread(raw, 1, sizeof(raw), random) == sizeof(raw);
        fclose(random);
    }
    if (!filled) {
        srand((unsigned)time(NULL) ^ (unsigned)getpid());
        for (size_t i = 0; i < sizeof(raw); i++) raw[i] = (unsigned char)rand();
    }

    for (size_t i = 0; i < sizeof(raw); i++) {
        storageFormatToken[i * 2] = hex[raw[i] >> 4];
        storageFormatToken[i * 2 + 1] = hex[raw[i] & 0x0f];
    }
    storageFormatToken[16] = '\0';
}

static const char *storage_token_get(void) {
    if (!storageFormatToken[0]) storage_token_generate();
    return storageFormatToken;
}

static bool storage_tool_exists(const char *path) {
    return access(path, X_OK) == 0;
}

static bool storage_format_supported(void) {
    if (!storageToolsChecked) {
        storageToolsAvailable =
            storage_tool_exists("/usr/bin/oko-sdcard") &&
            (storage_tool_exists("/sbin/sfdisk") ||
                storage_tool_exists("/usr/sbin/sfdisk")) &&
            (storage_tool_exists("/usr/sbin/mkfs.vfat") ||
                storage_tool_exists("/sbin/mkfs.vfat"));
        storageToolsChecked = true;
    }
    return storageToolsAvailable;
}

static void storage_read_state(char *device, size_t deviceSize,
    char *fs, size_t fsSize) {
    FILE *file;
    char line[128];

    if (deviceSize) device[0] = '\0';
    if (fsSize) fs[0] = '\0';
    file = fopen("/run/oko-sd.state", "r");
    if (!file) return;

    while (fgets(line, sizeof(line), file)) {
        char *value = strchr(line, '=');
        if (!value) continue;
        *value++ = '\0';
        value[strcspn(value, "\r\n")] = '\0';
        if (!strcmp(line, "device") && deviceSize)
            snprintf(device, deviceSize, "%s", value);
        else if (!strcmp(line, "fs") && fsSize)
            snprintf(fs, fsSize, "%s", value);
    }
    fclose(file);
}

static int capture_command(const char *command, char *output, size_t outputSize) {
    FILE *pipe;
    size_t length;

    if (!output || outputSize == 0) return -1;
    output[0] = '\0';
    pipe = popen(command, "r");
    if (!pipe) return -1;
    length = fread(output, 1, outputSize - 1, pipe);
    output[length] = '\0';
    return pclose(pipe) == 0 ? 0 : -1;
}

static bool valid_ipv4(const char *value) {
    struct in_addr address;
    return value && *value && inet_pton(AF_INET, value, &address) == 1;
}

/* OKO: helpers shared by the operational endpoints (WebUI rework). */

/* Defined further down; the helpers below send through them. */
int send_to_fd(int fd, char *buf, ssize_t size);
void send_http_error(int fd, int code);
static void close_socket_fd(int sockFd);

#define REC_ENTRY_LIMIT 200

typedef struct {
    char name[256];
    unsigned long long size;
    time_t mtime;
} rec_entry;

static int rec_entry_cmp(const void *a, const void *b) {
    const rec_entry *ra = a, *rb = b;
    if (ra->mtime != rb->mtime) return ra->mtime < rb->mtime ? 1 : -1;
    return strcmp(rb->name, ra->name);
}

/* Sends a complete JSON body with a correct Content-Length and closes. */
static void reply_json(int fd, const char *body, size_t len) {
    char header[192];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json;charset=UTF-8\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n\r\n", len);

    send_to_fd(fd, header, header_len);
    send_to_fd(fd, (char *)body, (ssize_t)len);
    close_socket_fd(fd);
}

static void reply_error(int fd, const char *code, const char *reason,
    const char *hint) {
    char body[640];
    int len = snprintf(body, sizeof(body),
        "{\"error\":\"%s\",\"reason\":\"%s\",\"hint\":\"%s\"}",
        code ? code : "failed", reason ? reason : "", hint ? hint : "");
    reply_json(fd, body, len);
}

/* Validation failures of an API argument use a real 4xx status; the rest of
 * the legacy endpoints keep their JSON error with HTTP 200. */
static void reply_bad_request(int fd, const char *reason, const char *hint) {
    char body[640];
    int bodyLen = snprintf(body, sizeof(body),
        "{\"error\":\"invalid\",\"reason\":\"%s\",\"hint\":\"%s\"}",
        reason ? reason : "", hint ? hint : "");
    char header[192];
    int headerLen = snprintf(header, sizeof(header),
        "HTTP/1.1 400 Bad Request\r\n"
        "Content-Type: application/json;charset=UTF-8\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n", bodyLen);
    send_to_fd(fd, header, headerLen);
    send_to_fd(fd, body, bodyLen);
    close_socket_fd(fd);
}

/* Non-200 JSON responses for the focus/flash APIs. */
static void reply_status_json(int fd, int code, const char *body) {
    char header[192];
    const char *reason = code == 202 ? "Accepted" :
        (code == 401 ? "Unauthorized" : (code == 403 ? "Forbidden" :
        (code == 409 ? "Conflict" : (code == 502 ? "Bad Gateway" :
        (code == 400 ? "Bad Request" : "OK")))));
    int len = (int)strlen(body);
    int headerLen = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: application/json;charset=UTF-8\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n", code, reason, len);
    send_to_fd(fd, header, headerLen);
    send_to_fd(fd, (char *)body, len);
    close_socket_fd(fd);
}

/* Streams a regular file with a proper MIME type and Content-Length.
 * max_bytes > 0 sends only the tail of the file (log export). */
static int send_stream_file(const int fd, const char *path, const char *mime,
    const char *filename, bool attachment, size_t max_bytes) {
    struct stat st;
    FILE *file = fopen(path, "rb");

    if (!file || fstat(fileno(file), &st) || !S_ISREG(st.st_mode)) {
        if (file) fclose(file);
        send_http_error(fd, 404);
        return -1;
    }

    long long start = 0;
    long long length = (long long)st.st_size;
    if (max_bytes && length > (long long)max_bytes) {
        start = length - (long long)max_bytes;
        length = (long long)max_bytes;
    }
    if (start && fseek(file, (long)start, SEEK_SET)) {
        fclose(file);
        send_http_error(fd, 404);
        return -1;
    }

    char header[512];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Disposition: %s; filename=\"%s\"\r\n"
        "Content-Length: %lld\r\n"
        "Connection: close\r\n\r\n",
        mime, attachment ? "attachment" : "inline",
        filename, length);
    send_to_fd(fd, header, header_len);

    char buf[8192];
    long long sent = 0;
    while (sent < length) {
        size_t want = sizeof(buf);
        if ((long long)want > length - sent) want = (size_t)(length - sent);
        size_t n = fread(buf, 1, want, file);
        if (n == 0) break;
        if (send_to_fd(fd, buf, (ssize_t)n) < 0) break;
        sent += (long long)n;
    }
    fclose(file);
    close_socket_fd(fd);
    return 0;
}

static const char *mime_for_name(const char *name) {
    const char *ext = get_extension(name);

    if (!strcasecmp(ext, "mp4")) return "video/mp4";
    if (!strcasecmp(ext, "jpg") || !strcasecmp(ext, "jpeg")) return "image/jpeg";
    if (!strcasecmp(ext, "png")) return "image/png";
    if (!strcasecmp(ext, "bmp")) return "image/bmp";
    if (!strcasecmp(ext, "txt") || !strcasecmp(ext, "log")) return "text/plain";
    return "application/octet-stream";
}

/* Recording names are plain basenames: no separators, no traversal. */
static bool valid_basename(const char *name) {
    static const char *allowed = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz._-";
    size_t len = name ? strlen(name) : 0;
    const char *ext;

    if (len < 5 || len > 68) return false;
    if (strspn(name, allowed) != len) return false;
    if (name[0] == '.') return false;

    ext = get_extension(name);
    return !strcasecmp(ext, "mp4") || !strcasecmp(ext, "jpg");
}

/* NTP server: hostname, IPv4 or IPv6 literal; the shell never sees more
 * than these characters, which also keeps the generated config safe. */
static bool valid_hostname(const char *value) {
    size_t len = value ? strlen(value) : 0;

    if (len == 0 || len > 127) return false;
    if (strstr(value, "..")) return false;
    if (value[0] == '.' || value[0] == '-' ||
        value[len - 1] == '.' || value[len - 1] == '-') return false;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)value[i];
        /* OKO: do not use isalnum() here.  compat.c defines a function named
         * __ctype_b for librockit and -rdynamic exports it, which interposes
         * the uClibc ctype table pointer: every ctype macro then dereferences
         * code as a table and crashes.  The NTP server charset is ASCII only. */
        bool alnum = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z');
        if (!alnum && c != '.' && c != '-' && c != ':') return false;
    }
    return true;
}

/* Time zone: an IANA name from the uClibc zoneinfo set.  The charset check
 * runs before any filesystem access, so a traversal never reaches the link. */
#define TZ_ZONEINFO_DIR "/usr/share/zoneinfo/uclibc/"

static bool valid_tz(const char *value) {
    size_t len = value ? strlen(value) : 0;
    char path[128];

    if (len == 0 || len >= 64) return false;
    if (value[0] == '/' || strstr(value, "..")) return false;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)value[i];
        /* Same ctype workaround as valid_hostname(). */
        bool alnum = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z');
        if (!alnum && c != '/' && c != '_' && c != '-') return false;
    }

    snprintf(path, sizeof(path), TZ_ZONEINFO_DIR "%s", value);
    return !access(path, F_OK);
}

/* Point /etc/TZ at an installed zone.  Buildroot's tz package creates the same
 * relative symlink, and /etc/TZ is a symlink in the image, so the rename
 * replaces it atomically. */
static bool set_tz_link(const char *value) {
    char target[128], tmp[32];

    snprintf(target, sizeof(target), "../usr/share/zoneinfo/uclibc/%s", value);
    snprintf(tmp, sizeof(tmp), "/etc/TZ.tmp.%d", (int)getpid());
    unlink(tmp);
    if (symlink(target, tmp)) return false;
    if (rename(tmp, "/etc/TZ")) {
        unlink(tmp);
        return false;
    }
    return true;
}

/* Effective zone: what libc actually reads, i.e. the target of /etc/TZ.  It
 * can differ from the saved setting after a hand edit until the next Apply or
 * boot, so the WebUI shows both. */
static void current_tz(char *out, size_t size) {
    char buf[128];
    ssize_t len;

    if (!out || !size) return;
    out[0] = '\0';

    len = readlink("/etc/TZ", buf, sizeof(buf) - 1);
    if (len > 0) {
        char *zone;
        buf[len] = '\0';
        zone = strstr(buf, "zoneinfo/uclibc/");
        if (zone) zone += strlen("zoneinfo/uclibc/");
        else zone = buf;
        snprintf(out, size, "%s", zone);
        return;
    }

    /* Some images use a plain file whose first line is the zone name. */
    FILE *file = fopen("/etc/TZ", "r");
    if (file) {
        if (fgets(buf, sizeof(buf), file)) {
            buf[strcspn(buf, "\r\n")] = '\0';
            snprintf(out, size, "%s", buf);
        }
        fclose(file);
    }
}

static void local_ipv4(char *out, size_t size) {
    int fd;
    struct ifreq ifr;

    if (!out || !size) return;
    out[0] = '\0';

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return;

    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "eth0");
    if (ioctl(fd, SIOCGIFADDR, &ifr) == 0) {
        struct sockaddr_in *addr = (struct sockaddr_in *)&ifr.ifr_addr;
        inet_ntop(AF_INET, &addr->sin_addr, out, size);
    }
    close(fd);
}

static void close_socket_fd(int sockFd) {
    shutdown(sockFd, SHUT_RDWR);
    close(sockFd);
}

void free_client(int i) {
    if (client_fds[i].sockFd < 0) return;

    close_socket_fd(client_fds[i].sockFd);
    client_fds[i].sockFd = -1;
    client_fds[i].type = -1;
}

int send_to_fd(int fd, char *buf, ssize_t size) {
    if (fd < 0) return -1;
    /* A zero-length chunk used to fall into send(fd, buf, 0) == 0 and be
     * treated as a failure, which closed the MP4 client before moov/mdat. */
    if (size == 0) return EXIT_SUCCESS;

    ssize_t total = 0;
    for (int attempts = 0; attempts < 4; attempts++) {
        ssize_t n = send(fd, buf + total, size - total, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (n > 0) {
            total += n;
            attempts = -1;
        } else if (n == 0) return -1;
        else if (errno != EAGAIN && errno != EWOULDBLOCK) return -1;

        if (total == size) return EXIT_SUCCESS;

        struct pollfd pfd = { .fd = fd, .events = POLLOUT };
        if (poll(&pfd, 1, 25) < 0) return -1;
    }

    return -1;
}

int sendv_to_client(int i, struct iovec *iov, int iovcnt) {
    int fd = client_fds[i].sockFd;
    if (fd < 0) return -1;

    for (int attempts = 0; attempts < 4; attempts++) {
        ssize_t n = writev(fd, iov, iovcnt);
        if (n > 0) {
            while (n > 0 && iovcnt > 0) {
                if (n >= iov[0].iov_len) {
                    n -= iov[0].iov_len;
                    iov++;
                    iovcnt--;
                } else {
                    iov[0].iov_base = (char *)iov[0].iov_base + n;
                    iov[0].iov_len -= n;
                    n = 0;
                }
            }
            if (iovcnt == 0) return 0;
            attempts = -1;
        } else if (n == 0) goto error;
        else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) goto error;

        struct pollfd pfd = { .fd = fd, .events = POLLOUT };
        if (poll(&pfd, 1, 25) < 0) goto error;
    }

error:
    free_client(i);
    return -1;
}

int send_to_client(int i, char *buf, ssize_t size) {
    if (send_to_fd(client_fds[i].sockFd, buf, size) < 0) {
        free_client(i);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static void send_and_close(int client_fd, char *buf, ssize_t size) {
    send_to_fd(client_fd, buf, size);
    close_socket_fd(client_fd);
}

void send_http_error(int fd, int code) {
    const char *desc = "\0", *msg = "Unspecified";
    char buffer[256];
    int len;

    for (int i = 0; i < sizeof(http_errors) / sizeof(*http_errors); i++) {
        if (http_errors[i].code == code) {
            desc = http_errors[i].desc;
            msg = http_errors[i].msg;
            break;
        }
    }

    len = snprintf(buffer, sizeof(buffer),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/plain\r\n"
        "Connection: close\r\n"
        "\r\n%s\r\n",
        code, msg, desc);

    send_and_close(fd, buffer, len);
}

void send_h26x_to_client(char index, hal_vidstream *stream) {
    for (unsigned int i = 0; i < stream->count; ++i) {
        hal_vidpack *pack = &stream->pack[i];
        unsigned char *pack_data = pack->data + pack->offset;

        pthread_mutex_lock(&client_fds_mutex);
        for (unsigned int c = 0; c < HTTP_MAX_CLIENTS; ++c) {
            if (client_fds[c].sockFd < 0) continue;
            if (client_fds[c].type != STREAM_H26X) continue;

            for (char j = 0; j < pack->naluCnt; j++) {
                if (client_fds[c].nalCnt == 0 &&
                    pack->nalu[j].type != NalUnitType_SPS &&
                    pack->nalu[j].type != NalUnitType_SPS_HEVC)
                    continue;

                char len_buf[16];
                int len_size = sprintf(len_buf, "%zX\r\n", pack->nalu[j].length);

                struct iovec iov[3];
                iov[0].iov_base = len_buf;
                iov[0].iov_len = len_size;
                iov[1].iov_base = pack_data + pack->nalu[j].offset;
                iov[1].iov_len = pack->nalu[j].length;
                iov[2].iov_base = (void*)"\r\n";
                iov[2].iov_len = 2;

                if (sendv_to_client(c, iov, 3) < 0)
                    break;

                client_fds[c].nalCnt++;
                if (client_fds[c].nalCnt == 300) {
                    char end[] = "0\r\n\r\n";
                    send_to_client(c, end, sizeof(end) - 1);
                    free_client(c);
                    break;
                }
            }
        }
        pthread_mutex_unlock(&client_fds_mutex);
    }
}

char any_http_audio(void) {
    char any = 0;
    pthread_mutex_lock(&client_fds_mutex);
    for (unsigned int i = 0; i < HTTP_MAX_CLIENTS; ++i)
        if (client_fds[i].sockFd >= 0 &&
            (client_fds[i].type & (STREAM_MP3 | STREAM_MP4 | STREAM_PCM))) { any = 1; break; }
    pthread_mutex_unlock(&client_fds_mutex);
    return any;
}

/* Feed one Annex B NAL to the fMP4 muxer.
 *
 * The vendor pack metadata exposes the whole HEVC access unit as one NAL
 * entry, so the per-entry type in pack->nalu[] cannot be used to find
 * VPS/SPS/PPS.  The recorder already splits the payload with nal_split();
 * the HTTP MP4 preview must do the same, otherwise the moov header never
 * gets built and /video.mp4 sends an empty chunk and closes. */
static void feed_mp4_nal(const unsigned char *data, size_t len,
    unsigned int type, char isH265, char anyClient) {
    if (!data || !len) return;

    if (isH265) {
        switch (type) {
            case NalUnitType_VPS_HEVC:
                mp4_set_vps((const char *)data, (uint32_t)len);
                return;
            case NalUnitType_SPS_HEVC:
                mp4_set_sps((const char *)data, (uint32_t)len, 1);
                return;
            case NalUnitType_PPS_HEVC:
                mp4_set_pps((const char *)data, (uint32_t)len, 1);
                return;
            default:
                break;
        }
        if (!anyClient) return;
        if (type == 19 || type == 20)       /* IDR_W_RADL / IDR_N_LP */
            mp4_set_slice((const char *)data, (uint32_t)len, 1);
        else if (type <= 9)                 /* TRAIL/TSA/STSA... */
            mp4_set_slice((const char *)data, (uint32_t)len, 0);
        return;
    }

    switch (type) {
        case NalUnitType_SPS:
            mp4_set_sps((const char *)data, (uint32_t)len, 0);
            return;
        case NalUnitType_PPS:
            mp4_set_pps((const char *)data, (uint32_t)len, 0);
            return;
        default:
            break;
    }
    if (!anyClient) return;
    if (type == NalUnitType_CodedSliceIdr || type == NalUnitType_CodedSliceAux)
        mp4_set_slice((const char *)data, (uint32_t)len, 1);
    else if (type == NalUnitType_CodedSliceNonIdr)
        mp4_set_slice((const char *)data, (uint32_t)len, 0);
}

void send_mp4_to_client(char index, hal_vidstream *stream, char isH265) {
    char anyClient = 0;
    pthread_mutex_lock(&client_fds_mutex);
    for (unsigned int i = 0; i < HTTP_MAX_CLIENTS; ++i)
        if (client_fds[i].sockFd >= 0 && client_fds[i].type == STREAM_MP4) { anyClient = 1; break; }
    pthread_mutex_unlock(&client_fds_mutex);

    for (unsigned int i = 0; i < stream->count; ++i) {
        hal_vidpack *pack = &stream->pack[i];
        unsigned char *pack_data;
        size_t pack_len;

        if (pack->length <= pack->offset) continue;
        pack_len = pack->length - pack->offset;
        pack_data = pack->data + pack->offset;

        if (plat == HAL_PLATFORM_AK)
            mp4_set_timestamp(pack->timestamp);

        unsigned char *nalptr = pack_data;
        size_t single_len = 0;
        while (nal_split(pack_data, &nalptr, &single_len, pack_len) == 0) {
            if (!single_len) continue;
            unsigned int type = isH265 ?
                ((nalptr[0] >> 1) & 0x3F) : (nalptr[0] & 0x1F);
            feed_mp4_nal(nalptr, single_len, type, isH265, anyClient);
        }
        if (!anyClient) continue;

        static enum BufError err;
        char len_buf[50];
        pthread_mutex_lock(&client_fds_mutex);
        for (unsigned int i = 0; i < HTTP_MAX_CLIENTS; ++i) {
            if (client_fds[i].sockFd < 0) continue;
            if (client_fds[i].type != STREAM_MP4) continue;

            if (!client_fds[i].mp4.header_sent) {
                struct BitBuf header_buf;
                err = mp4_get_header(&header_buf);
                if (err != BUF_OK || header_buf.offset == 0) continue;
                ssize_t len_size =
                    sprintf(len_buf, "%zX\r\n", header_buf.offset);
                if (send_to_client(i, len_buf, len_size) < 0)
                    continue;
                if (send_to_client(i, header_buf.buf, header_buf.offset) < 0)
                    continue;
                if (send_to_client(i, (void*)"\r\n", 2) < 0)
                    continue;

                client_fds[i].mp4.sequence_number = 0;
                client_fds[i].mp4.base_data_offset = header_buf.offset;
                client_fds[i].mp4.base_media_decode_time = 0;
                client_fds[i].mp4.header_sent = true;
                client_fds[i].mp4.nals_count = 0;
                client_fds[i].mp4.default_sample_duration =
                    default_sample_size;
            }

            /* buf_moof and the pos_* cursors are shared with the recorder,
               so the whole muxing sequence has to be exclusive. */
            pthread_mutex_lock(&mp4_mtx);
            err = mp4_set_state(&client_fds[i].mp4);
            if (err != BUF_OK) {
                pthread_mutex_unlock(&mp4_mtx);
                continue;
            }
            {
                struct BitBuf moof_buf;
                err = mp4_get_moof(&moof_buf);
                if (err != BUF_OK || moof_buf.offset == 0) {
                    pthread_mutex_unlock(&mp4_mtx);
                    continue;
                }
                ssize_t len_size =
                    sprintf(len_buf, "%zX\r\n", (ssize_t)moof_buf.offset);

                struct iovec iov[3];
                iov[0].iov_base = len_buf;
                iov[0].iov_len = len_size;
                iov[1].iov_base = moof_buf.buf;
                iov[1].iov_len = moof_buf.offset;
                iov[2].iov_base = (void*)"\r\n";
                iov[2].iov_len = 2;

                if (sendv_to_client(i, iov, 3) < 0) {
                    pthread_mutex_unlock(&mp4_mtx);
                    continue;
                }
            }
            {
                struct BitBuf mdat_buf;
                err = mp4_get_mdat(&mdat_buf);
                if (err != BUF_OK || mdat_buf.offset == 0) {
                    pthread_mutex_unlock(&mp4_mtx);
                    continue;
                }
                ssize_t len_size =
                    sprintf(len_buf, "%zX\r\n", (ssize_t)mdat_buf.offset);

                struct iovec iov[3];
                iov[0].iov_base = len_buf;
                iov[0].iov_len = len_size;
                iov[1].iov_base = mdat_buf.buf;
                iov[1].iov_len = mdat_buf.offset;
                iov[2].iov_base = (void*)"\r\n";
                iov[2].iov_len = 2;

                if (sendv_to_client(i, iov, 3) < 0) {
                    pthread_mutex_unlock(&mp4_mtx);
                    continue;
                }
            }
            pthread_mutex_unlock(&mp4_mtx);
        }
        pthread_mutex_unlock(&client_fds_mutex);
    }
}

void send_mp3_to_client(char *buf, ssize_t size) {
    pthread_mutex_lock(&client_fds_mutex);
    for (unsigned int i = 0; i < HTTP_MAX_CLIENTS; ++i) {
        if (client_fds[i].sockFd < 0) continue;
        if (client_fds[i].type != STREAM_MP3) continue;

        char len_buf[50];
        ssize_t len_size = sprintf(len_buf, "%zX\r\n", size);
        if (send_to_client(i, len_buf, len_size) < 0)
            continue; // send <SIZE>\r\n
        if (send_to_client(i, buf, size) < 0)
            continue; // send <DATA>
        if (send_to_client(i, "\r\n", 2) < 0)
            continue; // send \r\n
    }
    pthread_mutex_unlock(&client_fds_mutex);
}

void send_pcm_to_client(hal_audframe *frame) {
    pthread_mutex_lock(&client_fds_mutex);
    for (unsigned int i = 0; i < HTTP_MAX_CLIENTS; ++i) {
        if (client_fds[i].sockFd < 0) continue;
        if (client_fds[i].type != STREAM_PCM) continue;

        char len_buf[50];
        ssize_t len_size = sprintf(len_buf, "%zX\r\n", frame->length[0]);
        if (send_to_client(i, len_buf, len_size) < 0)
            continue; // send <SIZE>\r\n
        if (send_to_client(i, frame->data[0], frame->length[0]) < 0)
            continue; // send <DATA>
        if (send_to_client(i, "\r\n", 2) < 0)
            continue; // send \r\n
    }
    pthread_mutex_unlock(&client_fds_mutex);
}

void send_mjpeg_to_client(char index, char *buf, ssize_t size) {
    static char prefix_buf[128];
    ssize_t prefix_size = sprintf(prefix_buf,
        "--boundarydonotcross\r\n"
        "Content-Type:image/jpeg\r\n"
        "Content-Length: %lu\r\n\r\n", size);
    buf[size++] = '\r';
    buf[size++] = '\n';

    pthread_mutex_lock(&client_fds_mutex);
    for (unsigned int i = 0; i < HTTP_MAX_CLIENTS; ++i) {
        if (client_fds[i].sockFd < 0) continue;
        if (client_fds[i].type != STREAM_MJPEG) continue;

        if (send_to_client(i, prefix_buf, prefix_size) < 0)
            continue; // send <SIZE>\r\n
        if (send_to_client(i, buf, size) < 0)
            continue; // send <DATA>\r\n
    }
    pthread_mutex_unlock(&client_fds_mutex);
}

void send_jpeg_to_client(char index, char *buf, ssize_t size) {
    static char prefix_buf[128];
    ssize_t prefix_size = sprintf(
        prefix_buf,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: image/jpeg\r\n"
        "Content-Length: %lu\r\n"
        "Connection: close\r\n\r\n", size);

    pthread_mutex_lock(&client_fds_mutex);
    for (unsigned int i = 0; i < HTTP_MAX_CLIENTS; ++i) {
        if (client_fds[i].sockFd < 0) continue;
        if (client_fds[i].type != STREAM_JPEG) continue;

        if (send_to_client(i, prefix_buf, prefix_size) < 0)
            continue; // send <SIZE>\r\n
        if (send_to_client(i, buf, size) < 0)
            continue; // send <DATA>\r\n
        free_client(i);
    }
    pthread_mutex_unlock(&client_fds_mutex);
}

struct jpegtask {
    int client_fd;
    uint16_t width;
    uint16_t height;
    uint8_t qfactor;
    uint8_t color2Gray;
};

int send_file(const int client_fd, const char *path) {
    char resolved[PATH_MAX];

    /* OKO: static serving is a download path for the microSD card only.
     * It used to hand out any absolute path as root and echo the URI as the
     * Content-Type, so "//etc/passwd" leaked the file system.  Resolve
     * symlinks as well, so a link on the card cannot leave it. */
    if (!path || !STARTS_WITH(path, "/mnt/sdcard/") ||
        !realpath(path, resolved) || !STARTS_WITH(resolved, "/mnt/sdcard/")) {
        send_http_error(client_fd, 404);
        return EXIT_FAILURE;
    }

    if (!access(resolved, F_OK)) {
        const char *mime = mime_for_name(resolved);
        FILE *file = fopen(resolved, "r");
        if (file == NULL) {
            close_socket_fd(client_fd);
            return EXIT_SUCCESS;
        }
        char header[1024];
        int header_len = sprintf(header,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: %s\r\n"
            "Transfer-Encoding: chunked\r\n"
            "Connection: keep-alive\r\n\r\n", mime);
        send_to_fd(client_fd, header, header_len); // zero ending string!
        const int buf_size = 1024;
        char buf[buf_size + 2];
        char len_buf[50];
        ssize_t len_size;
        while (1) {
            ssize_t size = fread(buf, sizeof(char), buf_size, file);
            if (size <= 0) break;
            len_size = sprintf(len_buf, "%zX\r\n", size);
            buf[size++] = '\r';
            buf[size++] = '\n';
            if (send_to_fd(client_fd, len_buf, len_size) < 0) break;
            if (send_to_fd(client_fd, buf, size) < 0) break;
        }
        char end[] = "0\r\n\r\n";
        send_to_fd(client_fd, end, sizeof(end) - 1);
        fclose(file);
        close_socket_fd(client_fd);
        return EXIT_FAILURE;
    }

    send_http_error(client_fd, 404);
    return EXIT_FAILURE;
}

void send_binary(const int fd, const char *data, const long size) {
    char *buf;
    int buf_len = asprintf(&buf,
        "HTTP/1.1 200 OK\r\n" \
        "Content-Type: application/octet-stream\r\n" \
        "Content-Length: %zu\r\n" \
        "Connection: close\r\n\r\n", size);
    send_to_fd(fd, buf, buf_len);
    send_to_fd(fd, (char*)data, size);
    send_to_fd(fd, "\r\n", 2);
    close_socket_fd(fd);
    free(buf);
}

void send_html(const int fd, const char *data) {
    char *buf;
    int buf_len = asprintf(&buf,
        "HTTP/1.1 200 OK\r\n" \
        "Content-Type: text/html\r\n" \
        "Content-Length: %zu\r\n" \
        "Cache-Control: no-cache\r\n" \
        "Connection: close\r\n" \
        "\r\n%s", strlen(data), data);
    buf[buf_len++] = 0;
    send_and_close(fd, buf, buf_len);
    free(buf);
}

char *request_header(const char *name) {
    http_header_t *h = http_headers;
    for (; h->name; h++)
        if (!strcasecmp(h->name, name))
            return h->value;
    return NULL;
}

http_header_t *request_headers(void) { return http_headers; }


/* ---------------------------------------------------------------------------
 * Streamed update upload.
 *
 * The generic request buffer is capped at 4 MiB, while a full OKO package is
 * about 86 MiB.  POST /api/update/upload is therefore recognised as soon as
 * the headers arrive: the whitelist/auth checks run first, then the body goes
 * straight to the microSD card.  Only this endpoint bypasses parse_request();
 * everything else still waits for the complete body.
 * ------------------------------------------------------------------------- */

static int raw_header(const char *block, int len, const char *name,
    char *out, size_t out_size)
{
    size_t name_len = strlen(name);
    const char *p, *limit = block + len;

    if (!block || !name || !out || out_size == 0) return -1;
    out[0] = '\0';
    p = memstr((char *)block, "\r\n", len, 2);
    if (!p) return -1;
    p += 2;
    while (p < limit) {
        const char *end = memstr((char *)p, "\r\n", (int)(limit - p), 2);
        if (!end || end == p) break;
        if ((size_t)(end - p) > name_len && p[name_len] == ':' &&
            !strncasecmp(p, name, name_len)) {
            const char *v = p + name_len + 1;
            size_t n;
            while (v < end && (*v == ' ' || *v == '\t')) v++;
            n = (size_t)(end - v);
            if (n >= out_size) n = out_size - 1;
            memcpy(out, v, n);
            out[n] = '\0';
            return 0;
        }
        p = end + 2;
    }
    return -1;
}

static bool is_update_upload_request(const char *block, int len)
{
    static const char path[] = "/api/update/upload";
    const char *uri;
    char after;

    if (!block || len < 6 || strncmp(block, "POST ", 5)) return false;
    uri = block + 5;
    if ((size_t)len < 5 + sizeof(path) - 1) return false;
    if (strncmp(uri, path, sizeof(path) - 1)) return false;
    after = uri[sizeof(path) - 1];
    return after == ' ' || after == '?' || after == '\r' || after == '\n';
}

/* Whitelist and Basic auth for the streaming path; respond_request() repeats
 * the same checks for normal requests. */
static bool stream_request_allowed(int fd, const char *block, int len)
{
    struct sockaddr_in client_sock;
    socklen_t client_sock_len = sizeof(client_sock);
    char *client_ip = NULL;

    if (!EMPTY(*app_config.web_whitelist) ||
        (app_config.web_enable_auth && app_config.web_auth_skiplocal)) {
        memset(&client_sock, 0, client_sock_len);
        if (getpeername(fd, (struct sockaddr *)&client_sock,
                &client_sock_len) == 0)
            client_ip = inet_ntoa(client_sock.sin_addr);
    }

    if (!ip_whitelisted(client_ip)) return false;
    if (!app_config.web_enable_auth) return true;
    if (app_config.web_auth_skiplocal && is_local_address(client_ip))
        return true;

    {
        char auth[512];
        if (raw_header(block, len, "Authorization", auth, sizeof(auth)))
            return false;
        return credentials_valid(auth);
    }
}

/* 1 = the upload already finished, 0 = keep streaming, -1 = answered/closed. */
static int begin_update_upload(http_request_t *req)
{
    char value[64];
    char body[640];
    int prefix;
    int len;

    if (!stream_request_allowed(req->clntFd, req->input, req->header_len)) {
        reply_status_json(req->clntFd, 401,
            "{\"error\":\"forbidden\",\"reason\":\"authentication is required\","
            "\"hint\":\"reload the page and sign in\"}");
        return -1;
    }
    if (req->paysize <= 0) {
        reply_status_json(req->clntFd, 400,
            "{\"error\":\"invalid\",\"reason\":\"Content-Length is required\","
            "\"hint\":\"send the package as the raw request body\"}");
        return -1;
    }
    if (update_upload_open(req->paysize) != 0) {
        len = snprintf(body, sizeof(body),
            "{\"error\":\"blocked\",\"reason\":\"%s\"}",
            update_upload_error());
        reply_status_json(req->clntFd, 409, body);
        return -1;
    }

    if (!raw_header(req->input, req->header_len, "Expect", value,
            sizeof(value)) && !strncasecmp(value, "100-continue", 12))
        send_to_fd(req->clntFd, "HTTP/1.1 100 Continue\r\n\r\n", 25);

    req->uploading = 1;
    req->upload_total = req->paysize;
    req->upload_received = 0;
    req->last_activity = time(NULL);

    /* Body bytes that arrived together with the headers. */
    prefix = req->total - req->header_len;
    if (prefix > req->paysize) prefix = req->paysize;
    if (prefix > 0) {
        if (update_upload_write(req->input + req->header_len, prefix) < 0) {
            update_upload_abort("cannot write to the microSD card");
            reply_status_json(req->clntFd, 500,
                "{\"error\":\"failed\",\"reason\":\"cannot write to the microSD card\"}");
            return -1;
        }
        req->upload_received = prefix;
    }
    req->total = 0;

    if (req->upload_received >= req->upload_total) {
        if (update_upload_finish() != 0) {
            reply_status_json(req->clntFd, 500,
                "{\"error\":\"failed\",\"reason\":\"cannot start the staging worker\"}");
        } else {
            reply_status_json(req->clntFd, 202, "{\"state\":\"staging\"}");
        }
        return 1;
    }
    return 0;
}

static void finish_update_upload(http_request_t *req)
{
    if (update_upload_finish() != 0)
        reply_status_json(req->clntFd, 500,
            "{\"error\":\"failed\",\"reason\":\"cannot start the staging worker\"}");
    else
        reply_status_json(req->clntFd, 202, "{\"state\":\"staging\"}");
}

void parse_request(http_request_t *req) {
    struct sockaddr_in client_sock;
    socklen_t client_sock_len = sizeof(client_sock);
    memset(&client_sock, 0, client_sock_len);

    getpeername(req->clntFd,
        (struct sockaddr *)&client_sock, &client_sock_len);
    char *client_ip = inet_ntoa(client_sock.sin_addr);

    if (!ip_whitelisted(client_ip)) {
        close_socket_fd(req->clntFd);
        req->clntFd = -1;
        return;
    }

    if (req->total <= 0) return;

    req->input[req->total] = '\0';
    char *state = NULL;
    req->method = strtok_r(req->input, " \t\r\n", &state);
    req->uri = strtok_r(NULL, " \t", &state);
    req->prot = strtok_r(NULL, " \t\r\n", &state);

    if (!req->method || !req->uri || !req->prot) {
        close_socket_fd(req->clntFd);
        req->clntFd = -1;
        return;
    }

    HAL_INFO("server", "\x1b[32mNew request: (%s) %s\n"
        "         Received from: %s\x1b[0m\n",
        req->method, req->uri, client_ip);

    if (req->query = strchr(req->uri, '?'))
        *req->query++ = '\0';
    else
        req->query = NULL;

    if (req->uri && strstr(req->uri, "..")) {
        close_socket_fd(req->clntFd);
        req->clntFd = -1;
        return;
    }

    http_header_t *h = http_headers;
    while (h < http_headers + 16) {
        char *k, *v, *e;
        if (!(k = strtok_r(NULL, "\r\n: \t", &state)))
            break;
        v = strtok_r(NULL, "\r\n", &state);
        if (!v) break;
        while (*v && *v == ' ' && v++);
        h->name = k;
        h++->value = v;
#ifdef DEBUG_HTTP
        fprintf(stderr, "         (H) %s: %s\n", k, v);
#endif
        if (state >= req->input + req->header_len) break;
        e = v + 1 + strlen(v);
        if (e[1] == '\r' && e[2] == '\n')
            break;
    }

    req->payload = req->input + req->header_len;

    /* OKO: a form-encoded POST body is accepted as the query string when the
     * URI carries no parameters.  Every existing split("&")/split("=") loop
     * then works for POST without changes, and GET stays compatible. */
    if (!req->query && EQUALS(req->method, "POST") && req->total > req->header_len) {
        char *type = request_header("Content-Type");
        if (type && STARTS_WITH(type, "application/x-www-form-urlencoded"))
            req->query = req->payload;
    }
}

static int query_bool(const char *value)
{
    return EQUALS_CASE(value, "true") || EQUALS(value, "1") ||
        EQUALS_CASE(value, "on") || EQUALS_CASE(value, "yes");
}

static void gating_block(http_request_t *req, gating_result gate)
{
    char response[512];
    int len = sprintf(response,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json;charset=UTF-8\r\n"
        "Connection: close\r\n"
        "\r\n"
        "{\"error\":\"blocked\",\"reason\":\"%s\",\"hint\":\"%s\"}",
        gate.reason ? gate.reason : "blocked",
        gate.hint ? gate.hint : "");
    send_and_close(req->clntFd, response, len);
}

/* Extracts a plain text field from a multipart/form-data body.  The VPN
 * import uses it for the OpenVPN "user" and "pass" fields. */
static int multipart_field(const char *payload, int body_len,
    const char *boundary, const char *field, char *out, size_t out_size)
{
    char marker[104];
    char needle[96];
    const char *cursor = payload;
    const char *limit = payload + body_len;

    if (!payload || !boundary || !field || !out || out_size == 0) return -1;
    out[0] = '\0';
    snprintf(marker, sizeof(marker), "--%s", boundary);
    snprintf(needle, sizeof(needle), "name=\"%s\"", field);
    while (cursor < limit) {
        char *part = memstr((char *)cursor, marker,
            (int)(limit - cursor), (char)strlen(marker));
        char *header_end, *body, *next, *body_end;
        size_t len;

        if (!part) break;
        part += strlen(marker);
        if (part + 2 <= limit && part[0] == '-' && part[1] == '-') break;
        while (part < limit && (*part == '\r' || *part == '\n')) part++;
        header_end = memstr(part, "\r\n\r\n", (int)(limit - part), 4);
        if (!header_end) break;
        body = header_end + 4;
        next = memstr(body, marker, (int)(limit - body),
            (char)strlen(marker));
        body_end = next ? next : (char *)limit;
        while (body_end > body &&
            (body_end[-1] == '\n' || body_end[-1] == '\r'))
            body_end--;
        if (memstr(part, needle, (int)(header_end + 4 - part),
                (char)strlen(needle))) {
            len = (size_t)(body_end - body);
            if (len >= out_size) len = out_size - 1;
            memcpy(out, body, len);
            out[len] = '\0';
            return 0;
        }
        if (!next) break;
        cursor = next;
    }
    return -1;
}

void respond_request(http_request_t *req) {
    char response[8192] = {0};
    int respLen = 0;

    if (req->clntFd < 0) return;

    if (!EQUALS(req->method, "GET") && !EQUALS(req->method, "POST")) {
        send_http_error(req->clntFd, 405);
        return;
    }

    if (app_config.onvif_enable && STARTS_WITH(req->uri, "/onvif")) {
        char *path = req->uri + 6;
        if (*path == '/') path++;

        if (!EQUALS(req->method, "POST")) {
            send_http_error(req->clntFd, 405);
            return;
        }

        char *action = onvif_extract_soap_action(req->payload);
        HAL_INFO("onvif", "\x1b[32mAction: %s\x1b[0m\n", action);
        respLen = sizeof(response);

        if (app_config.onvif_enable_auth && !onvif_is_preauth_action(action) &&
            !onvif_validate_soap_auth(req->payload)) {
            respLen = sprintf(response,
                "HTTP/1.1 401 Unauthorized\r\n"
                "Content-Type: text/plain\r\n"
                "WWW-Authenticate: Digest realm=\"Access the camera services\"\r\n"
                "Connection: close\r\n\r\n%s",
                badauthxml);
            send_and_close(req->clntFd, response, respLen);
            return;
        }

        if (EQUALS(path, "device_service")) {
            if (EQUALS(action, "GetCapabilities")) {
                onvif_respond_capabilities((char*)response, &respLen);
                send_and_close(req->clntFd, response, respLen);
                return;
            } else if (EQUALS(action, "GetDeviceInformation")) {
                onvif_respond_deviceinfo((char*)response, &respLen);
                send_and_close(req->clntFd, response, respLen);
                return;
            } else if (EQUALS(action, "GetSystemDateAndTime")) {
                onvif_respond_systemtime((char*)response, &respLen);
                send_and_close(req->clntFd, response, respLen);
                return;
            }
        } else if (EQUALS(path, "media_service")) {
            if (EQUALS(action, "GetProfiles")) {
                onvif_respond_mediaprofiles((char*)response, &respLen);
                send_and_close(req->clntFd, response, respLen);
                return;
            } else if (EQUALS(action, "GetSnapshotUri")) {
                onvif_respond_snapshot((char*)response, &respLen);
                send_and_close(req->clntFd, response, respLen);
                return;
            } else if (EQUALS(action, "GetStreamUri")) {
                onvif_respond_stream((char*)response, &respLen);
                send_and_close(req->clntFd, response, respLen);
                return;
            } else if (EQUALS(action, "GetVideoSources")) {
                onvif_respond_videosources((char*)response, &respLen);
                send_and_close(req->clntFd, response, respLen);
                return;
            }
        }

        if (!EMPTY(action))
            HAL_WARNING("server", "Unknown ONVIF request: %s->%s\n", path, action);
        send_http_error(req->clntFd, 501);
        return;
    }

    if (app_config.web_enable_auth) {
        bool should_skip_auth = false;

        if (app_config.web_auth_skiplocal) {
            struct sockaddr_in client_sock;
            socklen_t client_sock_len = sizeof(client_sock);
            memset(&client_sock, 0, client_sock_len);

            if (getpeername(req->clntFd, (struct sockaddr *)&client_sock, &client_sock_len) == 0) {
                char *client_ip = inet_ntoa(client_sock.sin_addr);
                should_skip_auth = is_local_address(client_ip);
            }
        }

        if (!should_skip_auth) {
            char *auth = request_header("Authorization");

            if (!credentials_valid(auth)) {
                respLen = sprintf(response,
                    "HTTP/1.1 401 Unauthorized\r\n"
                    "Content-Type: text/plain\r\n"
                    "WWW-Authenticate: Basic realm=\"Access the camera services\"\r\n"
                    "Connection: close\r\n\r\n");
                send_and_close(req->clntFd, response, respLen);
                return;
            }
        }
    }

    if (EQUALS(req->uri, "/exit")) {
        respLen = sprintf(response,
            "HTTP/1.1 200 OK\r\n"
            "Connection: close\r\n\r\n"
            "Closing...");
        send_and_close(req->clntFd, response, respLen);
        keepRunning = 0;
        graceful = 1;
        return;
    }

    if (EQUALS(req->uri, "/") || EQUALS(req->uri, "/index.htm") || EQUALS(req->uri, "/index.html")) {
        send_html(req->clntFd, indexhtml);
        return;
    }

    if (app_config.audio_enable && EQUALS(req->uri, "/audio.mp3")) {
        respLen = sprintf(response,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: audio/mpeg\r\n"
            "Transfer-Encoding: chunked\r\n"
            "Connection: keep-alive\r\n\r\n");
        send_to_fd(req->clntFd, response, respLen);
        pthread_mutex_lock(&client_fds_mutex);
        for (uint32_t i = 0; i < HTTP_MAX_CLIENTS; ++i)
            if (client_fds[i].sockFd < 0) {
                client_fds[i].sockFd = req->clntFd;
                client_fds[i].type = STREAM_MP3;
                break;
            }
        pthread_mutex_unlock(&client_fds_mutex);
        return;
    }

    if (app_config.audio_enable && EQUALS(req->uri, "/audio.pcm")) {
        respLen = sprintf(response,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: audio/pcm\r\n"
            "Transfer-Encoding: chunked\r\n"
            "Connection: keep-alive\r\n\r\n");
        send_to_fd(req->clntFd, response, respLen);
        pthread_mutex_lock(&client_fds_mutex);
        for (uint32_t i = 0; i < HTTP_MAX_CLIENTS; ++i)
            if (client_fds[i].sockFd < 0) {
                client_fds[i].sockFd = req->clntFd;
                client_fds[i].type = STREAM_PCM;
                break;
            }
        pthread_mutex_unlock(&client_fds_mutex);
        return;
    }

    if ((!app_config.mp4_codecH265 && EQUALS(req->uri, "/video.264")) ||
        (app_config.mp4_codecH265 && EQUALS(req->uri, "/video.265"))) {
        request_idr(channel_by_role(HAL_CHNROLE_MAIN));
        respLen = sprintf(response,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/octet-stream\r\n"
            "Transfer-Encoding: chunked\r\n"
            "Connection: keep-alive\r\n\r\n");
        send_to_fd(req->clntFd, response, respLen);
        pthread_mutex_lock(&client_fds_mutex);
        for (uint32_t i = 0; i < HTTP_MAX_CLIENTS; ++i)
            if (client_fds[i].sockFd < 0) {
                client_fds[i].sockFd = req->clntFd;
                client_fds[i].type = STREAM_H26X;
                client_fds[i].nalCnt = 0;
                break;
            }
        pthread_mutex_unlock(&client_fds_mutex);
        return;
    }

    if (app_config.mp4_enable && EQUALS(req->uri, "/video.mp4")) {
        request_idr(channel_by_role(HAL_CHNROLE_MAIN));
        respLen = sprintf(response,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: video/mp4\r\n"
            "Transfer-Encoding: chunked\r\n"
            "Connection: keep-alive\r\n\r\n");
        send_to_fd(req->clntFd, response, respLen);
        pthread_mutex_lock(&client_fds_mutex);
        for (uint32_t i = 0; i < HTTP_MAX_CLIENTS; ++i)
            if (client_fds[i].sockFd < 0) {
                client_fds[i].sockFd = req->clntFd;
                client_fds[i].type = STREAM_MP4;
                client_fds[i].mp4.header_sent = false;
                break;
            }
        pthread_mutex_unlock(&client_fds_mutex);
        return;
    }

    if (app_config.mjpeg_enable && EQUALS(req->uri, "/mjpeg")) {
        respLen = sprintf(response,
            "HTTP/1.0 200 OK\r\n"
            "Cache-Control: no-cache\r\n"
            "Pragma: no-cache\r\n"
            "Connection: close\r\n"
            "Content-Type: multipart/x-mixed-replace; boundary=boundarydonotcross\r\n\r\n");
        send_to_fd(req->clntFd, response, respLen);
        pthread_mutex_lock(&client_fds_mutex);
        for (uint32_t i = 0; i < HTTP_MAX_CLIENTS; ++i)
            if (client_fds[i].sockFd < 0) {
                client_fds[i].sockFd = req->clntFd;
                client_fds[i].type = STREAM_MJPEG;
                break;
            }
        pthread_mutex_unlock(&client_fds_mutex);
        return;
    }

    if (app_config.jpeg_enable && STARTS_WITH(req->uri, "/image.jpg")) {
        {
            struct jpegtask *task = malloc(sizeof(struct jpegtask));
            task->client_fd = req->clntFd;
            task->width = app_config.jpeg_width;
            task->height = app_config.jpeg_height;
            task->qfactor = app_config.jpeg_qfactor;
            task->color2Gray = 0;

            if (req->query) {
                char *remain;
                while (req->query) {
                    char *value = split(&req->query, "&");
                    if (!value || !*value) continue;
                    char *key = split(&value, "=");
                    if (!key || !*key || !value || !*value) continue;
                    unescape_uri(value);
                    if (EQUALS(key, "width")) {
                        short result = strtol(value, &remain, 10);
                        if (remain != value)
                            task->width = result;
                    }
                    else if (EQUALS(key, "height")) {
                        short result = strtol(value, &remain, 10);
                        if (remain != value)
                            task->height = result;
                    }
                    else if (EQUALS(key, "qfactor")) {
                        short result = strtol(value, &remain, 10);
                        if (remain != value)
                            task->qfactor = result;
                    }
                    else if (EQUALS(key, "color2gray") || EQUALS(key, "gray")) {
                        if (EQUALS_CASE(value, "true") || EQUALS(value, "1"))
                            task->color2Gray = 1;
                        else if (EQUALS_CASE(value, "false") || EQUALS(value, "0"))
                            task->color2Gray = 0;
                    }
                }
            }

            hal_jpegdata jpeg = {0};
            if (jpeg_get(task->width, task->height, task->qfactor,
                    task->color2Gray, &jpeg)) {
                send_http_error(req->clntFd, 503);
                free(task);
                return;
            }

            char header[256];
            int header_len = sprintf(header,
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: image/jpeg\r\n"
                "Content-Length: %u\r\n"
                "Connection: close\r\n\r\n",
                (unsigned)jpeg.jpegSize);
            send_to_fd(req->clntFd, header, header_len);
            send_to_fd(req->clntFd, jpeg.data, jpeg.jpegSize);
            free(jpeg.data);
            free(task);
            close_socket_fd(req->clntFd);
            req->clntFd = -1;
            return;
        }
        return;
    }

    if (EQUALS(req->uri, "/api/audio")) {
        if (req->query) {
            while (req->query) {
                char *value = split(&req->query, "&");
                if (!value || !*value) continue;
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
                unescape_uri(value);
                if (EQUALS(key, "bitrate")) {
                    long result;
                    if (parse_ranged(value, 32, 320, &result))
                        app_config.audio_bitrate = (unsigned int)result;
                } else if (EQUALS(key, "enable")) {
                    if (EQUALS_CASE(value, "true") || EQUALS(value, "1"))
                        app_config.audio_enable = 1;
                    else if (EQUALS_CASE(value, "false") || EQUALS(value, "0"))
                        app_config.audio_enable = 0;
                } else if (EQUALS(key, "gain")) {
                    long result;
                    if (parse_ranged(value, -60, 30, &result))
                        app_config.audio_gain = (int)result;
                } else if (EQUALS(key, "srate")) {
                    long result;
                    if (parse_ranged(value, 8000, 96000, &result))
                        app_config.audio_srate = (unsigned int)result;
                }
            }

            media_audio_disable();
            if (app_config.audio_enable) media_audio_enable();
        }

        respLen = sprintf(response,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json;charset=UTF-8\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"enable\":%s,\"bitrate\":%d,\"gain\":%d,\"srate\":%d}",
            app_config.audio_enable ? "true" : "false",
            app_config.audio_bitrate, app_config.audio_gain, app_config.audio_srate);
        send_and_close(req->clntFd, response, respLen);
        return;
    }

    if (EQUALS(req->uri, "/api/cmd")) {
        int result = -1, doReboot = 0, doExport = 0, doLog = 0;
        if (req->query) {
            while (req->query) {
                char *value = split(&req->query, "&");
                if (!value || !*value) continue;
                char *key = split(&value, "=");
                if (!key || !*key) continue;
                if (value) unescape_uri(value);
                if (EQUALS(key, "save")) {
                    result = app_config_save();
                    if (!result)
                        HAL_INFO("server", "Configuration saved!\n");
                    else
                        HAL_WARNING("server", "Failed to save configuration!\n");
                    break;
                } else if (EQUALS(key, "restart")) {
                    kill(getpid(), SIGHUP);
                    result = 0;
                    break;
                } else if (EQUALS(key, "reboot")) {
                    result = 0;
                    doReboot = 1;
                    break;
                } else if (EQUALS(key, "export")) {
                    doExport = 1;
                    break;
                } else if (EQUALS(key, "log")) {
                    doLog = 1;
                    break;
                }
            }
        }

        /* OKO: export the in-memory configuration, so the download matches
         * what the Apply buttons have already changed (before Save). */
        if (doExport) {
            FILE *tmp = fopen("/tmp/divinus-export.yaml", "w");
            if (!tmp) {
                reply_error(req->clntFd, "failed",
                    "cannot create the export file",
                    "check the free space in /tmp");
                return;
            }
            app_config_dump(tmp);
            fclose(tmp);
            send_stream_file(req->clntFd, "/tmp/divinus-export.yaml",
                "application/x-yaml", "divinus.yaml", true, 0);
            unlink("/tmp/divinus-export.yaml");
            return;
        }

        /* OKO: syslog tail; logread is not part of this busybox build, but
         * syslogd writes /var/log/messages. */
        if (doLog) {
            struct stat st;
            if (stat("/var/log/messages", &st) || !S_ISREG(st.st_mode)) {
                reply_error(req->clntFd, "unavailable",
                    "the system log is not available",
                    "syslogd writes /var/log/messages on this image");
                return;
            }
            send_stream_file(req->clntFd, "/var/log/messages",
                "text/plain", "divinus.log", true, 256 * 1024);
            return;
        }

        respLen = sprintf(response,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json;charset=UTF-8\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"code\":%d}", result);
        send_and_close(req->clntFd, response, respLen);

        /* OKO: answer first, reboot afterwards so the client sees the JSON. */
        if (doReboot) {
            HAL_INFO("server", "Rebooting the device\n");
            system("(sleep 1; reboot) >/dev/null 2>&1 &");
        }
        return;
    }

    /* OKO: POST /api/cmd/import — replace divinus.yaml with an uploaded copy.
     * The current file is rotated to .bak first, so main.c can roll back when
     * the imported file does not parse. */
    if (EQUALS(req->uri, "/api/cmd/import")) {
        char *type = request_header("Content-Type");
        int body_len = req->total - req->header_len;
        char *payload = req->payload;
        char bound[96];
        char marker[104];
        char *boundary, *start, *stop;
        size_t imported_len;

        if (!EQUALS(req->method, "POST")) {
            send_http_error(req->clntFd, 405);
            return;
        }
        if (!type || !STARTS_WITH(type, "multipart/form-data") || body_len <= 0) {
            reply_error(req->clntFd, "unsupported",
                "the upload must be multipart/form-data",
                "send the configuration as the 'file' field");
            return;
        }

        boundary = strstr(type, "boundary=");
        if (!boundary) {
            reply_error(req->clntFd, "unsupported",
                "the multipart boundary is missing", "");
            return;
        }
        boundary += strlen("boundary=");
        size_t bound_len = strcspn(boundary, ";\r\n");
        if (bound_len >= 2 && boundary[0] == '"' && boundary[bound_len - 1] == '"') {
            boundary++;
            bound_len -= 2;
        }
        if (bound_len == 0 || bound_len >= sizeof(bound)) {
            reply_error(req->clntFd, "unsupported",
                "the multipart boundary is invalid", "");
            return;
        }
        snprintf(bound, sizeof(bound), "%.*s", (int)bound_len, boundary);

        start = memstr(payload, "\r\n\r\n", body_len, 4);
        if (!start) {
            reply_error(req->clntFd, "unsupported",
                "the multipart body has no part header", "");
            return;
        }
        start += 4;

        snprintf(marker, sizeof(marker), "--%s", bound);
        stop = memstr(start, marker, body_len - (int)(start - payload),
            (char)strlen(marker));
        if (stop) {
            /* Strip the CRLF that precedes the closing boundary. */
            if (stop - start >= 2)
                stop -= 2;
            else
                stop = start;
        } else {
            stop = payload + body_len;
        }
        imported_len = (size_t)(stop - start);

        if (imported_len == 0 || imported_len > 128 * 1024) {
            reply_error(req->clntFd, "blocked",
                "the uploaded configuration is empty or too large",
                "the limit is 128 KiB");
            return;
        }
        if (!memstr(start, "system:", (int)imported_len, 7) ||
            !memstr(start, "web_port:", (int)imported_len, 9)) {
            reply_error(req->clntFd, "blocked",
                "the uploaded file is not a divinus configuration",
                "export the current configuration and edit that copy");
            return;
        }

        {
            if (app_config_replace(start, imported_len)) {
                reply_error(req->clntFd, "failed",
                    "cannot write the configuration file",
                    "check the permissions of divinus.yaml");
                return;
            }
        }

        {
            char body[320];
            int len = snprintf(body, sizeof(body),
                "{\"imported\":true,\"bytes\":%zu,"
                "\"note\":\"the imported configuration applies after restart\"}",
                imported_len);
            HAL_INFO("server", "Imported configuration (%zu bytes)\n", imported_len);
            reply_json(req->clntFd, body, len);
        }
        return;
    }

    /* OKO: model management for the NPU analytics.
     *   GET  /api/models            — list, free space, active model
     *   POST /api/models            — multipart upload (*.rknn)
     *   POST /api/models/activate   — load and persist ?name=
     *   POST /api/models/delete     — remove ?name= (active model refused) */
    if (STARTS_WITH(req->uri, "/api/models")) {
        if (EQUALS(req->uri, "/api/models")) {
            if (EQUALS(req->method, "GET")) {
                char body[4096];
                size_t len = models_list_json(body, sizeof(body));
                reply_json(req->clntFd, body, len);
                return;
            }

            if (!EQUALS(req->method, "POST")) {
                send_http_error(req->clntFd, 405);
                return;
            }

            {
                char *type = request_header("Content-Type");
                int body_len = req->total - req->header_len;
                char *payload = req->payload;
                char bound[96], marker[104];
                char *boundary, *start, *stop, *fnStart, *fnEnd;
                char filename[96] = {0};
                size_t uploaded_len;
                char error[192];

                if (!type || !STARTS_WITH(type, "multipart/form-data") ||
                    body_len <= 0) {
                    reply_error(req->clntFd, "unsupported",
                        "the upload must be multipart/form-data",
                        "send the model as the 'file' field");
                    return;
                }
                boundary = strstr(type, "boundary=");
                if (!boundary) {
                    reply_error(req->clntFd, "unsupported",
                        "the multipart boundary is missing", "");
                    return;
                }
                boundary += strlen("boundary=");
                {
                    size_t bound_len = strcspn(boundary, ";\r\n");
                    if (bound_len >= 2 && boundary[0] == '"' &&
                        boundary[bound_len - 1] == '"') {
                        boundary++;
                        bound_len -= 2;
                    }
                    if (bound_len == 0 || bound_len >= sizeof(bound)) {
                        reply_error(req->clntFd, "unsupported",
                            "the multipart boundary is invalid", "");
                        return;
                    }
                    snprintf(bound, sizeof(bound), "%.*s", (int)bound_len,
                        boundary);
                }

                start = memstr(payload, "\r\n\r\n", body_len, 4);
                if (!start) {
                    reply_error(req->clntFd, "unsupported",
                        "the multipart body has no part header", "");
                    return;
                }

                fnStart = memstr(payload, "filename=\"",
                    (int)(start - payload), 10);
                if (fnStart) {
                    fnStart += 10;
                    fnEnd = strchr(fnStart, '"');
                    if (fnEnd && fnEnd < start) {
                        char *base = fnStart;
                        for (char *p = fnStart; p < fnEnd; p++)
                            if (*p == '/' || *p == '\\') base = p + 1;
                        if ((size_t)(fnEnd - base) < sizeof(filename))
                            snprintf(filename, sizeof(filename), "%.*s",
                                (int)(fnEnd - base), base);
                    }
                }
                if (!filename[0]) {
                    reply_error(req->clntFd, "unsupported",
                        "the upload has no file name",
                        "select a .rknn file in the Events section");
                    return;
                }

                start += 4;
                snprintf(marker, sizeof(marker), "--%s", bound);
                stop = memstr(start, marker,
                    body_len - (int)(start - payload), (char)strlen(marker));
                if (stop) {
                    if (stop - start >= 2)
                        stop -= 2;
                    else
                        stop = start;
                } else {
                    stop = payload + body_len;
                }
                uploaded_len = (size_t)(stop - start);

                if (models_upload(filename, start, uploaded_len, error,
                        sizeof(error))) {
                    reply_error(req->clntFd, "blocked", error,
                        "the limit is 3 MiB and the model must be .rknn");
                    return;
                }

                {
                    /* Names are restricted to [A-Za-z0-9._-] by
                     * models_upload(), so no JSON escaping is needed. */
                    char body[320];
                    int len = snprintf(body, sizeof(body),
                        "{\"uploaded\":true,\"name\":\"%s\",\"bytes\":%zu,"
                        "\"note\":\"activate the model to load it\"}",
                        filename, uploaded_len);
                    reply_json(req->clntFd, body, len);
                }
                return;
            }
        }

        if (EQUALS(req->uri, "/api/models/activate") ||
            EQUALS(req->uri, "/api/models/delete")) {
            char name[96] = {0};
            char error[192];
            int activate = EQUALS(req->uri, "/api/models/activate");

            if (!EQUALS(req->method, "POST")) {
                send_http_error(req->clntFd, 405);
                return;
            }

            while (req->query) {
                char *value = split(&req->query, "&");
                char *key;
                if (!value || !*value) continue;
                key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
                unescape_uri(value);
                if (EQUALS(key, "name")) {
                    strncpy(name, value, sizeof(name) - 1);
                    name[sizeof(name) - 1] = 0;
                }
            }
            if (!name[0]) {
                reply_error(req->clntFd, "unsupported",
                    "the 'name' parameter is missing", "");
                return;
            }

            if (activate) {
                if (models_activate(name, error, sizeof(error))) {
                    reply_error(req->clntFd, "blocked", error,
                        "the previous model stays active");
                    return;
                }
                {
                    char body[320];
                    int len = snprintf(body, sizeof(body),
                        "{\"activated\":true,\"name\":\"%s\","
                        "\"note\":\"yolo.model saved; no restart needed\"}",
                        name);
                    reply_json(req->clntFd, body, len);
                }
            } else {
                if (models_delete(name, error, sizeof(error))) {
                    reply_error(req->clntFd, "blocked", error,
                        "activate another model before deleting this one");
                    return;
                }
                {
                    char body[256];
                    int len = snprintf(body, sizeof(body),
                        "{\"deleted\":true,\"name\":\"%s\"}", name);
                    reply_json(req->clntFd, body, len);
                }
            }
            return;
        }

        send_http_error(req->clntFd, 404);
        return;
    }

    if (EQUALS(req->uri, "/api/isp")) {
        if (req->query) {
            char *remain;
            while (req->query) {
                char *value = split(&req->query, "&");
                if (!value || !*value) continue;
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
                unescape_uri(value);
                if (EQUALS(key, "mirror")) {
                    if (EQUALS_CASE(value, "true") || EQUALS(value, "1"))
                        app_config.mirror = 1;
                    else if (EQUALS_CASE(value, "false") || EQUALS(value, "0"))
                        app_config.mirror = 0;
                } else if (EQUALS(key, "flip")) {
                    if (EQUALS_CASE(value, "true") || EQUALS(value, "1"))
                        app_config.flip = 1;
                    else if (EQUALS_CASE(value, "false") || EQUALS(value, "0"))
                        app_config.flip = 0;
                } else if (EQUALS(key, "antiflicker")) {
                    app_config.antiflicker = strtol(value, &remain, 10);
                }
            }
        }
        respLen = sprintf(response,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json;charset=UTF-8\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"mirror\":%s, \"flip\":%s, \"antiflicker\":%d}",
            app_config.mirror ? "true" : "false", app_config.flip ? "true" : "false",
            app_config.antiflicker);
        send_and_close(req->clntFd, response, respLen);
        return;
    }

    if (EQUALS(req->uri, "/api/jpeg")) {
        if (req->query) {
            char *remain;
            while (req->query) {
                char *value = split(&req->query, "&");
                if (!value || !*value) continue;
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
                unescape_uri(value);
                if (EQUALS(key, "width")) {
                    short result = strtol(value, &remain, 10);
                    if (remain != value)
                        app_config.jpeg_width = result;
                } else if (EQUALS(key, "height")) {
                    short result = strtol(value, &remain, 10);
                    if (remain != value)
                        app_config.jpeg_height = result;
                } else if (EQUALS(key, "qfactor")) {
                    short result = strtol(value, &remain, 10);
                    if (remain != value)
                        app_config.jpeg_qfactor = result;
                }
            }

            jpeg_deinit();
            if (app_config.jpeg_enable) jpeg_init();
        }

        respLen = sprintf(response,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json;charset=UTF-8\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"enable\":%s,\"width\":%d,\"height\":%d,\"qfactor\":%d}",
            app_config.jpeg_enable ? "true" : "false",
            app_config.jpeg_width, app_config.jpeg_height, app_config.jpeg_qfactor);
        send_and_close(req->clntFd, response, respLen);
        return;
    }

    if (EQUALS(req->uri, "/api/mjpeg")) {
        if (req->query) {
            char *remain;
            while (req->query) {
                char *value = split(&req->query, "&");
                if (!value || !*value) continue;
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
                unescape_uri(value);
                if (EQUALS(key, "enable")) {
                    if (EQUALS_CASE(value, "true") || EQUALS(value, "1"))
                        app_config.mjpeg_enable = 1;
                    else if (EQUALS_CASE(value, "false") || EQUALS(value, "0"))
                        app_config.mjpeg_enable = 0;
                } else if (EQUALS(key, "width")) {
                    short result = strtol(value, &remain, 10);
                    if (remain != value)
                        app_config.mjpeg_width = result;
                } else if (EQUALS(key, "height")) {
                    short result = strtol(value, &remain, 10);
                    if (remain != value)
                        app_config.mjpeg_height = result;
                } else if (EQUALS(key, "fps")) {
                    short result = strtol(value, &remain, 10);
                    if (remain != value)
                        app_config.mjpeg_fps = result;
                } else if (EQUALS(key, "bitrate")) {
                    short result = strtol(value, &remain, 10);
                    if (remain != value)
                        app_config.mjpeg_bitrate = result;
                } else if (EQUALS(key, "qfactor")) {
                    short result = strtol(value, &remain, 10);
                    if (remain != value)
                        app_config.mjpeg_qfactor = result;
                } else if (EQUALS(key, "mode")) {
                    if (EQUALS_CASE(value, "CBR"))
                        app_config.mjpeg_mode = HAL_VIDMODE_CBR;
                    else if (EQUALS_CASE(value, "VBR"))
                        app_config.mjpeg_mode = HAL_VIDMODE_VBR;
                    else if (EQUALS_CASE(value, "QP"))
                        app_config.mjpeg_mode = HAL_VIDMODE_QP;
                }
            }

            media_mjpeg_disable();
            if (app_config.mjpeg_enable) media_mjpeg_enable();
        }

        char mode[5] = "\0";
        switch (app_config.mjpeg_mode) {
            case HAL_VIDMODE_CBR: strcpy(mode, "CBR"); break;
            case HAL_VIDMODE_VBR: strcpy(mode, "VBR"); break;
            case HAL_VIDMODE_QP: strcpy(mode, "QP"); break;
        }
        respLen = sprintf(response,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json;charset=UTF-8\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"enable\":%s,\"width\":%d,\"height\":%d,\"fps\":%d,\"mode\":\"%s\","
            "\"bitrate\":%d,\"qfactor\":%d}",
            app_config.mjpeg_enable ? "true" : "false",
            app_config.mjpeg_width, app_config.mjpeg_height, app_config.mjpeg_fps, mode,
            app_config.mjpeg_bitrate, app_config.mjpeg_qfactor);
        send_and_close(req->clntFd, response, respLen);
        return;
    }

    if (EQUALS(req->uri, "/api/mp4")) {
        if (req->query) {
            char *remain;
            while (req->query) {
                char *value = split(&req->query, "&");
                if (!value || !*value) continue;
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
                unescape_uri(value);
                if (EQUALS(key, "enable")) {
                    if (EQUALS_CASE(value, "true") || EQUALS(value, "1"))
                        app_config.mp4_enable = 1;
                    else if (EQUALS_CASE(value, "false") || EQUALS(value, "0"))
                        app_config.mp4_enable = 0;
                } else if (EQUALS(key, "width")) {
                    short result = strtol(value, &remain, 10);
                    if (remain != value)
                        app_config.mp4_width = result;
                } else if (EQUALS(key, "height")) {
                    short result = strtol(value, &remain, 10);
                    if (remain != value)
                        app_config.mp4_height = result;
                } else if (EQUALS(key, "fps")) {
                    short result = strtol(value, &remain, 10);
                    if (remain != value)
                        app_config.mp4_fps = result;
                } else if (EQUALS(key, "bitrate")) {
                    short result = strtol(value, &remain, 10);
                    if (remain != value)
                        app_config.mp4_bitrate = result;
                } else if (EQUALS(key, "h265")) {
                    if (EQUALS_CASE(value, "true") || EQUALS(value, "1"))
                        app_config.mp4_codecH265 = 1;
                    else if (EQUALS_CASE(value, "false") || EQUALS(value, "0"))
                        app_config.mp4_codecH265 = 0;
                } else if (EQUALS(key, "mode")) {
                    if (EQUALS_CASE(value, "CBR"))
                        app_config.mp4_mode = HAL_VIDMODE_CBR;
                    else if (EQUALS_CASE(value, "VBR"))
                        app_config.mp4_mode = HAL_VIDMODE_VBR;
                    else if (EQUALS_CASE(value, "QP"))
                        app_config.mp4_mode = HAL_VIDMODE_QP;
                    else if (EQUALS_CASE(value, "ABR"))
                        app_config.mp4_mode = HAL_VIDMODE_ABR;
                    else if (EQUALS_CASE(value, "AVBR"))
                        app_config.mp4_mode = HAL_VIDMODE_AVBR;
                } else if (EQUALS(key, "profile")) {
                    if (EQUALS_CASE(value, "BP") || EQUALS_CASE(value, "BASELINE"))
                        app_config.mp4_profile = HAL_VIDPROFILE_BASELINE;
                    else if (EQUALS_CASE(value, "MP") || EQUALS_CASE(value, "MAIN"))
                        app_config.mp4_profile = HAL_VIDPROFILE_MAIN;
                    else if (EQUALS_CASE(value, "HP") || EQUALS_CASE(value, "HIGH"))
                        app_config.mp4_profile = HAL_VIDPROFILE_HIGH;
                }
            }

            media_mp4_disable();
            if (app_config.mp4_enable) media_mp4_enable();
        }

        char h265[6] = "false";
        char mode[5] = "\0";
        char profile[3] = "\0";
        if (app_config.mp4_codecH265)
            strcpy(h265, "true");
        switch (app_config.mp4_mode) {
            case HAL_VIDMODE_CBR: strcpy(mode, "CBR"); break;
            case HAL_VIDMODE_VBR: strcpy(mode, "VBR"); break;
            case HAL_VIDMODE_QP: strcpy(mode, "QP"); break;
            case HAL_VIDMODE_ABR: strcpy(mode, "ABR"); break;
            case HAL_VIDMODE_AVBR: strcpy(mode, "AVBR"); break;
        }
        switch (app_config.mp4_profile) {
            case HAL_VIDPROFILE_BASELINE: strcpy(profile, "BP"); break;
            case HAL_VIDPROFILE_MAIN: strcpy(profile, "MP"); break;
            case HAL_VIDPROFILE_HIGH: strcpy(profile, "HP"); break;
        }
        respLen = sprintf(response,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json;charset=UTF-8\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"enable\":%s,\"width\":%d,\"height\":%d,\"fps\":%d,\"gop\":%d,"
            "\"h265\":%s,\"mode\":\"%s\",\"profile\":\"%s\",\"bitrate\":%d}",
            app_config.mp4_enable ? "true" : "false", app_config.mp4_width, app_config.mp4_height,
            app_config.mp4_fps, app_config.mp4_gop, h265, mode, profile, app_config.mp4_bitrate);
        send_and_close(req->clntFd, response, respLen);
        return;
    }

    if (EQUALS(req->uri, "/api/rtsp")) {
        if (req->query) {
            char *remain;
            while (req->query) {
                char *value = split(&req->query, "&");
                if (!value || !*value) continue;
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
                unescape_uri(value);
                if (EQUALS(key, "enable"))
                    app_config.rtsp_enable = EQUALS_CASE(value, "true") || EQUALS(value, "1");
                else if (EQUALS(key, "enable_auth"))
                    app_config.rtsp_enable_auth = EQUALS_CASE(value, "true") || EQUALS(value, "1");
                else if (EQUALS(key, "port")) {
                    long result;
                    if (parse_ranged(value, 1, 65535, &result))
                        app_config.rtsp_port = result;
                } else if (EQUALS(key, "auth_user"))
                    strncpy(app_config.rtsp_auth_user, value, sizeof(app_config.rtsp_auth_user) - 1);
                else if (EQUALS(key, "auth_pass"))
                    strncpy(app_config.rtsp_auth_pass, value, sizeof(app_config.rtsp_auth_pass) - 1);
                else if (EQUALS(key, "audio_codec")) {
                    if (EQUALS(value, "pcma") || EQUALS(value, "pcmu") || EQUALS(value, "mp3")) {
                        strncpy(app_config.rtsp_audio_codec, value,
                            sizeof(app_config.rtsp_audio_codec) - 1);
                        app_config.rtsp_audio_codec[sizeof(app_config.rtsp_audio_codec) - 1] = 0;
                    }
                }
            }
        }
        char esc_user[sizeof(app_config.rtsp_auth_user) * 6 + 1];
        char esc_codec[sizeof(app_config.rtsp_audio_codec) * 6 + 1];
        escape_json(esc_user, app_config.rtsp_auth_user, sizeof(esc_user));
        escape_json(esc_codec, app_config.rtsp_audio_codec, sizeof(esc_codec));
        respLen = sprintf(response,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json;charset=UTF-8\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"enable\":%s,\"enable_auth\":%s,\"port\":%d,\"auth_user\":\"%s\",\"audio_codec\":\"%s\","
            "\"note\":\"port and codec changes apply after restart\"}",
            app_config.rtsp_enable ? "true" : "false", app_config.rtsp_enable_auth ? "true" : "false",
            app_config.rtsp_port, esc_user, esc_codec);
        send_and_close(req->clntFd, response, respLen);
        return;
    }
    if (EQUALS(req->uri, "/api/onvif")) {
        if (req->query) {
            while (req->query) {
                char *value = split(&req->query, "&");
                if (!value || !*value) continue;
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
                unescape_uri(value);
                if (EQUALS(key, "enable"))
                    app_config.onvif_enable = EQUALS_CASE(value, "true") || EQUALS(value, "1");
                else if (EQUALS(key, "enable_auth"))
                    app_config.onvif_enable_auth = EQUALS_CASE(value, "true") || EQUALS(value, "1");
                else if (EQUALS(key, "auth_user"))
                    strncpy(app_config.onvif_auth_user, value, sizeof(app_config.onvif_auth_user) - 1);
                else if (EQUALS(key, "auth_pass"))
                    strncpy(app_config.onvif_auth_pass, value, sizeof(app_config.onvif_auth_pass) - 1);
            }
        }
        char esc_onvif_user[sizeof(app_config.onvif_auth_user) * 6 + 1];
        escape_json(esc_onvif_user, app_config.onvif_auth_user, sizeof(esc_onvif_user));
        respLen = sprintf(response,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json;charset=UTF-8\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"enable\":%s,\"enable_auth\":%s,\"auth_user\":\"%s\",\"note\":\"applies after restart\"}",
            app_config.onvif_enable ? "true" : "false", app_config.onvif_enable_auth ? "true" : "false",
            esc_onvif_user);
        send_and_close(req->clntFd, response, respLen);
        return;
    }
    if (EQUALS(req->uri, "/api/night")) {
        if (req->query) {
            char *remain;
            while (req->query) {
                char *value = split(&req->query, "&");
                if (!value || !*value) continue;
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
                unescape_uri(value);
                if (EQUALS(key, "enable")) {
                    if (EQUALS_CASE(value, "true") || EQUALS(value, "1"))
                        app_config.night_mode_enable = 1;
                    else if (EQUALS_CASE(value, "false") || EQUALS(value, "0"))
                        app_config.night_mode_enable = 0;
                } else if (EQUALS(key, "adc_device")) {
                    strncpy(app_config.adc_device, value, sizeof(app_config.adc_device));
                } else if (EQUALS(key, "adc_threshold")) {
                    short result = strtol(value, &remain, 10);
                    if (remain != value)
                        app_config.adc_threshold = result;
                } else if (EQUALS(key, "grayscale")) {
                    if (EQUALS_CASE(value, "true") || EQUALS(value, "1"))
                        night_grayscale(1);
                    else if (EQUALS_CASE(value, "false") || EQUALS(value, "0"))
                        night_grayscale(0);
                } else if (EQUALS(key, "ircut")) {
                    if (EQUALS_CASE(value, "true") || EQUALS(value, "1"))
                        night_ircut(1);
                    else if (EQUALS_CASE(value, "false") || EQUALS(value, "0"))
                        night_ircut(0);
                } else if (EQUALS(key, "ircut_pin1")) {
                    short result = strtol(value, &remain, 10);
                    if (remain != value)
                        app_config.ir_cut_pin1 = result;
                } else if (EQUALS(key, "ircut_pin2")) {
                    short result = strtol(value, &remain, 10);
                    if (remain != value)
                        app_config.ir_cut_pin2 = result;
                } else if (EQUALS(key, "irled")) {
                    if (EQUALS_CASE(value, "true") || EQUALS(value, "1"))
                        night_irled(1);
                    else if (EQUALS_CASE(value, "false") || EQUALS(value, "0"))
                        night_irled(0);
                } else if (EQUALS(key, "irled_pin")) {
                    short result = strtol(value, &remain, 10);
                    if (remain != value)
                        app_config.ir_led_pin = result;
                } else if (EQUALS(key, "irsense_pin")) {
                    short result = strtol(value, &remain, 10);
                    if (remain != value)
                        app_config.ir_sensor_pin = result;
                } else if (EQUALS(key, "manual")) {
                    if (EQUALS_CASE(value, "true") || EQUALS(value, "1"))
                        night_manual(1);
                    else if (EQUALS_CASE(value, "false") || EQUALS(value, "0"))
                        night_manual(0);
                }
            }

            night_disable();
            if (app_config.night_mode_enable) night_enable();
        }
        respLen = sprintf(response,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json;charset=UTF-8\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"active\":%s,\"manual\":%s,\"grayscale\":%s,\"ircut\":%s,\"ircut_pin1\":%d,\"ircut_pin2\":%d,"
            "\"irled\":%s,\"irled_pin\":%d,\"irsense_pin\":%d,\"adc_device\":\"%s\",\"adc_threshold\":%d}",
            app_config.night_mode_enable ? "true" : "false", night_manual_on() ? "true" : "false",
            night_grayscale_on() ? "true" : "false",
            night_ircut_on() ? "true" : "false", app_config.ir_cut_pin1, app_config.ir_cut_pin2,
            night_irled_on() ? "true" : "false", app_config.ir_led_pin, app_config.ir_sensor_pin,
            app_config.adc_device, app_config.adc_threshold);
        send_and_close(req->clntFd, response, respLen);
        return;
    }

    if (app_config.osd_enable && STARTS_WITH(req->uri, "/api/osd/")) {
        char *remain;
        int respLen;
        short id = strtol(req->uri + 9, &remain, 10);
        if (remain == req->uri + 9 || id < 0 || id >= MAX_OSD) {
            send_http_error(req->clntFd, 404);
            return;
        }
        if (EMPTY(osds[id].text) && EMPTY(osds[id].img))
            region_defaults(&osds[id], id);
        if (EQUALS(req->method, "POST")) {
            char *type = request_header("Content-Type");
            bool multipart = type && STARTS_WITH(type, "multipart/form-data");
            /* OKO: a form-encoded body carries the region parameters and is
             * handled by the query loop below, like a GET. */
            bool form = type && STARTS_WITH(type, "application/x-www-form-urlencoded");

            if (multipart) {
                char *bound = strstr(type, "boundary=");
                if (bound) bound += strlen("boundary=");
                if (!bound || !*bound) {
                    send_http_error(req->clntFd, 400);
                    return;
                }

                char *payloadb = strstr(req->payload, bound);
                if (!payloadb) {
                    send_http_error(req->clntFd, 400);
                    return;
                }
                payloadb = memstr(payloadb, "\r\n\r\n", req->total - (payloadb - req->input), 4);
                if (!payloadb) {
                    send_http_error(req->clntFd, 400);
                    return;
                }
                payloadb += 4;

                char *payloade = memstr(payloadb, bound,
                    req->total - (payloadb - req->input), strlen(bound));
                if (payloade) payloade -= 4;
                if (!payloade || payloade <= payloadb) {
                    send_http_error(req->clntFd, 400);
                    return;
                }

                char path[32];

                if (!memcmp(payloadb, "\x89\x50\x4E\x47\xD\xA\x1A\xA", 8)) {
                    /* OKO: the PNG lands in a separate file, so point the region
                     * at it; an empty img would load the default BMP path. */
                    sprintf(path, "/tmp/osd%d.png", id);
                    snprintf(osds[id].img, sizeof(osds[id].img), "%s", path);
                } else {
                    sprintf(path, "/tmp/osd%d.bmp", id);
                    osds[id].img[0] = '\0';
                }

                FILE *img = fopen(path, "wb");
                if (!img) {
                    reply_error(req->clntFd, "failed",
                        "the uploaded image could not be stored",
                        "check the free space in /tmp");
                    return;
                }
                fwrite(payloadb, sizeof(char), payloade - payloadb, img);
                fclose(img);

                strcpy(osds[id].text, "");
                osds[id].updt = 1;
            } else if (!form) {
                respLen = sprintf(response,
                    "HTTP/1.1 415 Unsupported Media Type\r\n"
                    "Content-Type: text/plain\r\n"
                    "Connection: close\r\n"
                    "\r\n"
                    "The payload must be presented as multipart/form-data.\r\n"
                );
                send_and_close(req->clntFd, response, respLen);
                return;
            }
        }
        if (req->query) {
            char *remain;
            while (req->query) {
                char *value = split(&req->query, "&");
                if (!value) continue;
                char *key = split(&value, "=");
                if (!key || !*key || !value) continue;
                unescape_uri(value);
                if (EQUALS(key, "img")) {
                    strncpy(osds[id].img, value,
                        sizeof(osds[id].img) - 1);
                    osds[id].img[sizeof(osds[id].img) - 1] = '\0';
                }
                else if (EQUALS(key, "font")) {
                    strncpy(osds[id].font, !EMPTY(value) ? value : DEF_FONT,
                        sizeof(osds[id].font) - 1);
                    osds[id].font[sizeof(osds[id].font) - 1] = '\0';
                }
                else if (EQUALS(key, "text")) {
                    strncpy(osds[id].text, value,
                        sizeof(osds[id].text) - 1);
                    osds[id].text[sizeof(osds[id].text) - 1] = '\0';
                }
                else if (EQUALS(key, "size")) {
                    double result = strtod(value, &remain);
                    if (remain == value) continue;
                    osds[id].size = (result != 0 ? result : DEF_SIZE);
                }
                else if (EQUALS(key, "color")) {
                    int result = color_parse(value);
                    if (result > 0) osds[id].color = result;
                }
                else if (EQUALS(key, "opal")) {
                    short result = strtol(value, &remain, 10);
                    if (remain != value)
                        osds[id].opal = result & 0xFF;
                }
                else if (EQUALS(key, "posx")) {
                    short result = strtol(value, &remain, 10);
                    if (remain != value)
                        osds[id].posx = result;
                }
                else if (EQUALS(key, "posy")) {
                    short result = strtol(value, &remain, 10);
                    if (remain != value)
                        osds[id].posy = result;
                }
                else if (EQUALS(key, "pos")) {
                    int x, y;
                    if (sscanf(value, "%d,%d", &x, &y) == 2) {
                        osds[id].posx = x;
                        osds[id].posy = y;
                    }
                }
                else if (EQUALS(key, "outl")) {
                    int result = color_parse(value);
                    if (result > 0) osds[id].outl = result;
                }
                else if (EQUALS(key, "thick")) {
                    double result = strtod(value, &remain);
                    if (remain == value) continue;
                        osds[id].thick = result;
                }
            }
            osds[id].updt = 1;
        }
        int color = (((osds[id].color >> 10) & 0x1F) * 255 / 31) << 16 |
                    (((osds[id].color >> 5) & 0x1F) * 255 / 31) << 8 |
                    ((osds[id].color & 0x1F) * 255 / 31);
        {
            char esc_text[sizeof(osds[id].text) * 6 + 1];
            char esc_img[sizeof(osds[id].img) * 6 + 1];
            char esc_font[sizeof(osds[id].font) * 6 + 1];
            escape_json(esc_text, osds[id].text, sizeof(esc_text));
            escape_json(esc_img, osds[id].img, sizeof(esc_img));
            escape_json(esc_font, osds[id].font, sizeof(esc_font));
            respLen = sprintf(response,
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json;charset=UTF-8\r\n"
                "Connection: close\r\n"
                "\r\n"
                "{\"id\":%d,\"color\":\"#%06x\",\"opal\":%d,\"pos\":[%d,%d],"
                "\"font\":\"%s\",\"size\":%.1f,\"text\":\"%s\",\"img\":\"%s\","
                "\"outl\":\"#%06x\",\"thick\":%.1f}",
                id, color, osds[id].opal, osds[id].posx, osds[id].posy,
                esc_font, osds[id].size, esc_text, esc_img,
                osds[id].outl, osds[id].thick);
        }
        send_and_close(req->clntFd, response, respLen);
        return;
    }

    if (EQUALS(req->uri, "/api/record")) {
        if (req->query) {
            char *remain;
            while (req->query) {
                char *value = split(&req->query, "&");
                if (!value || !*value) continue;
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
                unescape_uri(value);
                if (EQUALS(key, "enable")) {
                    if (EQUALS_CASE(value, "true") || EQUALS(value, "1")) {
                        gating_result gate = gating_check("record");
                        if (!gate.ok) { gating_block(req, gate); return; }
                        app_config.record_enable = 1;
                    } else if (EQUALS_CASE(value, "false") || EQUALS(value, "0"))
                        app_config.record_enable = 0;
                }
                else if (EQUALS(key, "source")) {
                    app_config.record_sub = EQUALS(value, "sub");
                }
                else if (EQUALS(key, "max_bytes")) {
                    app_config.record_max_bytes = strtoull(value, &remain, 10);
                }
                else if (EQUALS(key, "min_free_mb")) {
                    long result;
                    if (parse_ranged(value, 0, 1048576, &result))
                        app_config.record_min_free_mb = (unsigned int)result;
                }
                else if (EQUALS(key, "continuous")) {
                    if (EQUALS_CASE(value, "true") || EQUALS(value, "1"))
                        app_config.record_continuous = 1;
                    else if (EQUALS_CASE(value, "false") || EQUALS(value, "0"))
                        app_config.record_continuous = 0;
                }
                else if (EQUALS(key, "path"))
                    strncpy(app_config.record_path, value, sizeof(app_config.record_path) - 1);
                else if (EQUALS(key, "filename"))
                    strncpy(app_config.record_filename, value, sizeof(app_config.record_filename) - 1);
                else if (EQUALS(key, "segment_duration")) {
                    short result = strtol(value, &remain, 10);
                    if (remain != value)
                        app_config.record_segment_duration = result;
                }
                else if (EQUALS(key, "segment_size")) {
                    short result = strtol(value, &remain, 10);
                    if (remain != value)
                        app_config.record_segment_size = result;
                }

                if (!app_config.record_enable) continue;
                if (app_config.record_continuous) continue;
                if (EQUALS(key, "start")) {
                    int source = app_config.record_sub ?
                        HAL_CHNROLE_SUB : HAL_CHNROLE_MAIN;
                    if (channel_by_role(source) < 0) {
                        gating_result gate = { false,
                            "the selected recording source is not running",
                            app_config.record_sub ?
                                "enable the sub stream or set record.source=main" :
                                "the main stream is disabled" };
                        gating_block(req, gate);
                        return;
                    }
                    if (!record_start()) {
                        gating_result gate = { false,
                            storageBusy ?
                                "storage is busy with another operation" :
                                "recording requires a mounted microSD card",
                            storageBusy ?
                                "wait for the format/unmount operation to finish" :
                                "insert a card formatted as vfat; local storage is not used" };
                        gating_block(req, gate);
                        return;
                    }
                } else if (EQUALS(key, "stop"))
                    record_stop();
            }
        }
        char start_time[32] = "";
        time_t startTime = record_start_time();
        if (startTime) {
            struct tm tm_buf, *tm_info = localtime_r(&startTime, &tm_buf);
            strftime(start_time, sizeof(start_time), "%Y-%m-%dT%H:%M:%SZ", tm_info);
        }

        respLen = sprintf(response,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json;charset=UTF-8\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"recording\":%s,\"start_time\":\"%s\",\"continuous\":%s,\"path\":\"%s\","
            "\"filename\":\"%s\",\"segment_duration\":%d,\"segment_size\":%d,"
            "\"source\":\"%s\",\"max_bytes\":%llu,\"min_free_mb\":%d,"
            "\"sd\":%s}",
                record_active() ? "true" : "false", start_time, app_config.record_continuous ? "true" : "false",
                app_config.record_path, app_config.record_filename,
                app_config.record_segment_duration, app_config.record_segment_size,
                app_config.record_sub ? "sub" : "main", app_config.record_max_bytes,
                app_config.record_min_free_mb,
                gating_sd_mounted() ? "true" : "false");
        send_and_close(req->clntFd, response, respLen);
        return;
    }

    if (EQUALS(req->uri, "/api/eptz")) {
        int eptzRc;
        int hadQuery = req->query != NULL;
        if (hadQuery) {
            int enableReq = -1;
            while (req->query) {
                char *value = split(&req->query, "&");
                if (!value || !*value) continue;
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
                unescape_uri(value);
                if (EQUALS(key, "enable")) {
                    enableReq = query_bool(value);
                } else if (EQUALS(key, "stream")) {
                    app_config.eptz_stream = EQUALS(value, "sub") ? 1 : 0;
                } else if (EQUALS(key, "x")) {
                    long v; if (parse_ranged(value, 0, 65535, &v)) app_config.eptz_x = (unsigned short)v;
                } else if (EQUALS(key, "y")) {
                    long v; if (parse_ranged(value, 0, 65535, &v)) app_config.eptz_y = (unsigned short)v;
                } else if (EQUALS(key, "w")) {
                    long v; if (parse_ranged(value, 16, 65535, &v)) app_config.eptz_w = (unsigned short)v;
                } else if (EQUALS(key, "h")) {
                    long v; if (parse_ranged(value, 16, 65535, &v)) app_config.eptz_h = (unsigned short)v;
                } else if (EQUALS(key, "zoom")) {
                    long zoom;
                    if (parse_ranged(value, 1, 8, &zoom) && zoom > 0) {
                        int width = app_config.mp4_width ? app_config.mp4_width : 1920;
                        int height = app_config.mp4_height ? app_config.mp4_height : 1080;
                        int w = width / zoom, h = height / zoom;
                        int cx = app_config.eptz_x + app_config.eptz_w / 2;
                        int cy = app_config.eptz_y + app_config.eptz_h / 2;
                        if (w < 16) w = 16;
                        if (h < 16) h = 16;
                        app_config.eptz_w = (unsigned short)w;
                        app_config.eptz_h = (unsigned short)h;
                        app_config.eptz_x = (unsigned short)(cx - w / 2);
                        app_config.eptz_y = (unsigned short)(cy - h / 2);
                    }
                } else if (EQUALS(key, "reset")) {
                    app_config.eptz_x = 0;
                    app_config.eptz_y = 0;
                    app_config.eptz_w = app_config.mp4_width ? app_config.mp4_width : 1920;
                    app_config.eptz_h = app_config.mp4_height ? app_config.mp4_height : 1080;
                }
            }

            if (enableReq == 1) {
                gating_result gate = gating_check("eptz");
                if (!gate.ok) { gating_block(req, gate); return; }
                app_config.eptz_enable = true;
            } else if (enableReq == 0) {
                app_config.eptz_enable = false;
            }

            /* Clamp the rectangle to the sensor frame. */
            {
                int width = app_config.mp4_width ? app_config.mp4_width : 1920;
                int height = app_config.mp4_height ? app_config.mp4_height : 1080;
                if (app_config.eptz_w > width) app_config.eptz_w = width;
                if (app_config.eptz_h > height) app_config.eptz_h = height;
                if (app_config.eptz_x + app_config.eptz_w > width)
                    app_config.eptz_x = width - app_config.eptz_w;
                if (app_config.eptz_y + app_config.eptz_h > height)
                    app_config.eptz_y = height - app_config.eptz_h;
            }

            /* Restarting the application in the middle of an AF search, a
             * home/zero job or a firmware write is not allowed. */
            if (focus_af_running() || focus_home_running() || flash_busy()) {
                char body[160];
                snprintf(body, sizeof(body),
                    "{\"error\":\"busy\",\"reason\":\"a focus or flash job is "
                    "running\"}");
                reply_status_json(req->clntFd, 409, body);
                return;
            }

            eptzRc = media_apply_eptz();
            if (eptzRc != MEDIA_APPLY_OK && eptzRc != MEDIA_APPLY_NEEDS_RESTART)
                HAL_WARNING("server", "ePTZ target stream is not running\n");
        }
        if (!hadQuery)
            eptzRc = media_apply_eptz();

        {
            /* Only the live snapshot proves that the VPSS accepted the crop:
             * `/api/eptz` reports what rk_channel_bind() applied, not what the
             * query asked for. */
            int echn, ex, ey, ew, eh, erc;
            char enote[200] = "";

            media_eptz_last(&echn, &ex, &ey, &ew, &eh, &erc);
            if (eptzRc == MEDIA_APPLY_NEEDS_RESTART) {
                snprintf(enote, sizeof(enote),
                    ",\"note\":\"settings saved; restarting the application\"");
            } else if (eptzRc == MEDIA_APPLY_NO_TARGET) {
                snprintf(enote, sizeof(enote), ",\"note\":\"stream is off\"");
            } else if (echn < 0) {
                snprintf(enote, sizeof(enote),
                    ",\"note\":\"applied when the stream starts\"");
            }

            respLen = sprintf(response,
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json;charset=UTF-8\r\n"
                "Connection: close\r\n"
                "\r\n"
                "{\"enable\":%s,\"stream\":\"%s\",\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d,"
                "\"applied\":%s,\"rc\":\"0x%x\",\"chn\":%d,"
                "\"crop\":[%d,%d,%d,%d]%s}",
                app_config.eptz_enable ? "true" : "false",
                app_config.eptz_stream ? "sub" : "main",
                app_config.eptz_x, app_config.eptz_y,
                app_config.eptz_w, app_config.eptz_h,
                (eptzRc == MEDIA_APPLY_OK && echn >= 0) ? "true" : "false",
                (unsigned)erc, echn, ex, ey, ew, eh, enote);
        }
        send_and_close(req->clntFd, response, respLen);
        if (eptzRc == MEDIA_APPLY_NEEDS_RESTART) {
            app_config_save();
            kill(getpid(), SIGHUP);
        }
        return;
    }

    if (EQUALS(req->uri, "/api/rotate")) {
        int rotRc;
        char rnote[200] = "";
        if (req->query) {
            int enableReq = -1;
            while (req->query) {
                char *value = split(&req->query, "&");
                if (!value || !*value) continue;
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
                unescape_uri(value);
                if (EQUALS(key, "enable")) {
                    enableReq = query_bool(value);
                } else if (EQUALS(key, "stream")) {
                    app_config.rotate_stream = EQUALS(value, "sub") ? 1 : 0;
                } else if (EQUALS(key, "angle")) {
                    long v;
                    if (parse_ranged(value, 0, 270, &v) &&
                        (v == 0 || v == 90 || v == 180 || v == 270))
                        app_config.rotate_angle = (unsigned short)v;
                }
            }

            if (enableReq == 1) {
                gating_result gate = gating_check("rotate");
                if (!gate.ok) { gating_block(req, gate); return; }
                app_config.rotate_enable = true;
            } else if (enableReq == 0) {
                app_config.rotate_enable = false;
            }

            if (focus_af_running() || focus_home_running() || flash_busy()) {
                char body[160];
                snprintf(body, sizeof(body),
                    "{\"error\":\"busy\",\"reason\":\"a focus or flash job is "
                    "running\"}");
                reply_status_json(req->clntFd, 409, body);
                return;
            }

            rotRc = media_apply_rotation();
        } else {
            rotRc = media_apply_rotation();
        }

        if (rotRc == MEDIA_APPLY_NEEDS_RESTART) {
            snprintf(rnote, sizeof(rnote),
                ",\"note\":\"settings saved; restarting the application\"");
        } else if (rotRc == MEDIA_APPLY_NO_TARGET) {
            snprintf(rnote, sizeof(rnote), ",\"note\":\"stream is off\"");
        }

        respLen = sprintf(response,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json;charset=UTF-8\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"enable\":%s,\"stream\":\"%s\",\"angle\":%d,\"applied\":%s%s}",
            app_config.rotate_enable ? "true" : "false",
            app_config.rotate_stream ? "sub" : "main",
            app_config.rotate_angle,
            rotRc == MEDIA_APPLY_OK ? "true" : "false", rnote);
        send_and_close(req->clntFd, response, respLen);
        if (rotRc == MEDIA_APPLY_NEEDS_RESTART) {
            app_config_save();
            kill(getpid(), SIGHUP);
        }
        return;
    }

    if (EQUALS(req->uri, "/api/venc")) {
        if (req->query) {
            int wantsExtra = 0;
            while (req->query) {
                char *value = split(&req->query, "&");
                if (!value || !*value) continue;
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
                unescape_uri(value);
                if (EQUALS(key, "smartp")) {
                    app_config.venc_smartp = query_bool(value);
                    wantsExtra |= app_config.venc_smartp;
                } else if (EQUALS(key, "svc")) {
                    app_config.venc_svc = query_bool(value);
                    wantsExtra |= app_config.venc_svc;
                } else if (EQUALS(key, "motion_deblur")) {
                    app_config.venc_motion_deblur = query_bool(value);
                    wantsExtra |= app_config.venc_motion_deblur;
                } else if (EQUALS(key, "vir_idr_len")) {
                    long v; if (parse_ranged(value, 0, 255, &v)) app_config.venc_vir_idr = (unsigned char)v;
                } else if (EQUALS(key, "min_qp")) {
                    long v; if (parse_ranged(value, 0, 51, &v)) { app_config.venc_min_qp = (unsigned char)v; wantsExtra = 1; }
                } else if (EQUALS(key, "max_qp")) {
                    long v; if (parse_ranged(value, 0, 51, &v)) { app_config.venc_max_qp = (unsigned char)v; wantsExtra = 1; }
                } else if (EQUALS(key, "min_iqp")) {
                    long v; if (parse_ranged(value, 0, 51, &v)) { app_config.venc_min_iqp = (unsigned char)v; wantsExtra = 1; }
                } else if (EQUALS(key, "max_iqp")) {
                    long v; if (parse_ranged(value, 0, 51, &v)) { app_config.venc_max_iqp = (unsigned char)v; wantsExtra = 1; }
                }
            }

            if (wantsExtra) {
                gating_result gate = gating_check("venc");
                if (!gate.ok) { gating_block(req, gate); return; }
            }

            /* SVC, motion deblur and the QP window can be changed on a running
             * channel; SmartP changes the GOP attribute and is applied by the
             * next start without dropping the current viewers. */
            if (media_apply_venc_extras())
                HAL_WARNING("server", "Some encoder extensions were rejected\n");
            request_idr(channel_by_role(HAL_CHNROLE_MAIN));
        }

        respLen = sprintf(response,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json;charset=UTF-8\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"smartp\":%s,\"vir_idr_len\":%d,\"svc\":%s,"
            "\"motion_deblur\":%s,\"min_qp\":%d,\"max_qp\":%d,"
            "\"min_iqp\":%d,\"max_iqp\":%d,"
            "\"note\":\"SVC/deblur/QP applied live; SmartP is applied after restart\"}",
            app_config.venc_smartp ? "true" : "false", app_config.venc_vir_idr,
            app_config.venc_svc ? "true" : "false",
            app_config.venc_motion_deblur ? "true" : "false",
            app_config.venc_min_qp, app_config.venc_max_qp,
            app_config.venc_min_iqp, app_config.venc_max_iqp);
        send_and_close(req->clntFd, response, respLen);
        return;
    }

    if (EQUALS(req->uri, "/api/substream")) {
        int pathChanged = 0, paramsChanged = 0, restart = 0;
        char oldPath[sizeof(app_config.substream_path)];
        strncpy(oldPath, app_config.substream_path, sizeof(oldPath) - 1);
        oldPath[sizeof(oldPath) - 1] = 0;

        if (req->query) {
            int enableReq = -1;
            while (req->query) {
                char *value = split(&req->query, "&");
                if (!value || !*value) continue;
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
                unescape_uri(value);
                if (EQUALS(key, "enable")) {
                    enableReq = query_bool(value);
                } else if (EQUALS(key, "path")) {
                    if (value[0] == '/') {
                        strncpy(app_config.substream_path, value,
                            sizeof(app_config.substream_path) - 1);
                        app_config.substream_path[sizeof(app_config.substream_path) - 1] = 0;
                    }
                } else if (EQUALS(key, "codec")) {
                    app_config.substream_codecH265 = !(EQUALS(value, "H.264") ||
                        EQUALS(value, "h264") || EQUALS(value, "AVC"));
                    paramsChanged = 1;
                } else if (EQUALS(key, "mode")) {
                    if (EQUALS_CASE(value, "CBR")) app_config.substream_mode = HAL_VIDMODE_CBR;
                    else if (EQUALS_CASE(value, "VBR")) app_config.substream_mode = HAL_VIDMODE_VBR;
                    else if (EQUALS_CASE(value, "QP")) app_config.substream_mode = HAL_VIDMODE_QP;
                    else if (EQUALS_CASE(value, "AVBR")) app_config.substream_mode = HAL_VIDMODE_AVBR;
                    paramsChanged = 1;
                } else if (EQUALS(key, "width")) {
                    long v; if (parse_ranged(value, 160, 4096, &v)) { app_config.substream_width = (unsigned int)v; paramsChanged = 1; }
                } else if (EQUALS(key, "height")) {
                    long v; if (parse_ranged(value, 120, 4096, &v)) { app_config.substream_height = (unsigned int)v; paramsChanged = 1; }
                } else if (EQUALS(key, "fps")) {
                    long v;
                    if (parse_ranged(value, 1, 60, &v)) { app_config.substream_fps = (unsigned int)v; paramsChanged = 1; }
                } else if (EQUALS(key, "gop")) {
                    long v; if (parse_ranged(value, 1, 600, &v)) { app_config.substream_gop = (unsigned int)v; paramsChanged = 1; }
                } else if (EQUALS(key, "bitrate")) {
                    long v; if (parse_ranged(value, 32, 65535, &v)) { app_config.substream_bitrate = (unsigned int)v; paramsChanged = 1; }
                } else if (EQUALS(key, "profile")) {
                    long v; if (parse_ranged(value, 0, 2, &v)) { app_config.substream_profile = (unsigned int)v; paramsChanged = 1; }
                }
            }

            if (enableReq == 1) {
                gating_result gate = gating_check("substream");
                if (!gate.ok) { gating_block(req, gate); return; }
                app_config.substream_enable = true;
            } else if (enableReq == 0) {
                app_config.substream_enable = false;
            }

            pathChanged = strcmp(oldPath, app_config.substream_path) != 0;
            int existing = channel_by_role(HAL_CHNROLE_SUB);

            if (rtspHandle && pathChanged)
                rtsp_set_stream_path(rtspHandle, 1, app_config.substream_path);

            if (enableReq == 0)
                app_config.substream_enable = false;

            if (app_config.substream_enable) {
                if (rtspHandle) {
                    int id = rtsp_add_stream(rtspHandle, app_config.substream_path);
                    if (id < 0) app_config.substream_enable = false;
                }
                if (app_config.substream_enable) {
                    if (existing < 0 && paramsChanged) {
                        /* The channel does not exist yet; create it at boot so
                         * the main/sub/JPEG order stays RGN-safe. */
                        restart = 1;
                    } else if (existing >= 0 && paramsChanged) {
                        /* Encoder parameters are channel attributes; rebuilding
                         * them live invalidates the RGN overlays, so apply the
                         * saved config with a graceful restart. */
                        restart = 1;
                    } else if (existing < 0) {
                        /* Creating a second VENC channel after the JPEG channel
                         * broke the main OSD on RV1106 (SetBitmap 0xfffffffe).
                         * Boot order main->sub->JPEG is safe, so restart. */
                        restart = 1;
                    } else if (media_substream_enable()) {
                        app_config.substream_enable = false;
                    }
                }
            } else {
                media_substream_disable();
            }
        }

        respLen = sprintf(response,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json;charset=UTF-8\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"enable\":%s,\"path\":\"%s\",\"codec\":\"%s\",\"mode\":%d,"
            "\"width\":%d,\"height\":%d,\"fps\":%d,\"gop\":%d,\"bitrate\":%d,"
            "\"profile\":%d,\"note\":\"%s\"}",
            app_config.substream_enable ? "true" : "false",
            app_config.substream_path,
            app_config.substream_codecH265 ? "H.265" : "H.264",
            app_config.substream_mode, app_config.substream_width,
            app_config.substream_height, app_config.substream_fps,
            app_config.substream_gop, app_config.substream_bitrate,
            app_config.substream_profile,
            restart ? "settings saved; restarting the application" :
            (pathChanged ? "path applies to new connections" :
                "sub stream updated"));
        send_and_close(req->clntFd, response, respLen);
        if (restart) {
            app_config_save();
            kill(getpid(), SIGHUP);
        }
        return;
    }

    if (EQUALS(req->uri, "/api/aiisp")) {
        if (req->query) {
            int enableReq = -1;
            while (req->query) {
                char *value = split(&req->query, "&");
                if (!value || !*value) continue;
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
                unescape_uri(value);
                if (EQUALS(key, "enable")) {
                    if (EQUALS_CASE(value, "off")) { enableReq = 0; }
                    else if (EQUALS_CASE(value, "auto")) { enableReq = 2; }
                    else { enableReq = query_bool(value); }
                } else if (EQUALS(key, "model_dir")) {
                    strncpy(app_config.aiisp_model_dir, value,
                        sizeof(app_config.aiisp_model_dir) - 1);
                    app_config.aiisp_model_dir[sizeof(app_config.aiisp_model_dir) - 1] = 0;
                } else if (EQUALS(key, "buf_cnt")) {
                    long v; if (parse_ranged(value, 1, 8, &v)) app_config.aiisp_buf_cnt = (unsigned int)v;
                }
            }

            if (enableReq != 0 && enableReq >= 0) {
                gating_result gate = gating_check("aiisp");
                if (!gate.ok) { gating_block(req, gate); return; }
                app_config.aiisp_enable = enableReq == 2 ? HAL_AIISP_AUTO : HAL_AIISP_ON;
            } else if (enableReq == 0) {
                app_config.aiisp_enable = HAL_AIISP_OFF;
            }

            if (media_apply_aiisp())
                HAL_WARNING("server", "AIISP could not be applied\n");
        }

        respLen = sprintf(response,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json;charset=UTF-8\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"enable\":\"%s\",\"model_dir\":\"%s\",\"buf_cnt\":%d,"
            "\"fps_enabled\":%d,\"fps_disabled\":%d,"
            "\"note\":\"AIISP lowers fps while the AINR model is active\"}",
            app_config.aiisp_enable == HAL_AIISP_OFF ? "off" :
                (app_config.aiisp_enable == HAL_AIISP_AUTO ? "auto" : "on"),
            app_config.aiisp_model_dir, app_config.aiisp_buf_cnt,
            app_config.aiisp_fps_enabled, app_config.aiisp_fps_disabled);
        send_and_close(req->clntFd, response, respLen);
        return;
    }

    if (EQUALS(req->uri, "/api/yolo")) {
        int restartNeeded = 0;
        int liveOk = 1;

        if (req->query) {
            int enableReq = -1;
            while (req->query) {
                char *value = split(&req->query, "&");
                char *key;
                if (!value || !*value) continue;
                key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
                unescape_uri(value);

                if (EQUALS(key, "enable")) {
                    enableReq = query_bool(value);
                } else if (EQUALS(key, "source")) {
                    unsigned int source;
                    if (EQUALS_CASE(value, "vpss3")) {
                        source = 1;
                    } else if (EQUALS_CASE(value, "vi0")) {
                        source = 2;
                    } else if (EQUALS_CASE(value, "vi1")) {
                        /* Enabling VI channel 1 locks the RV1106 board until
                         * a power cycle, so it is never accepted over HTTP. */
                        reply_bad_request(req->clntFd,
                            "yolo.source=vi1 is not supported",
                            "VI selfpath hangs RV1106; use vpss3 or vi0");
                        return;
                    } else {
                        reply_bad_request(req->clntFd,
                            "unknown yolo.source value",
                            "use vpss3 (default) or vi0");
                        return;
                    }
                    if (source != app_config.yolo_source) {
                        app_config.yolo_source = source;
                        restartNeeded = 1;
                        liveOk = 0;
                    }
                } else if (EQUALS(key, "runtime")) {
                    long v;
                    if (!parse_ranged(value, 1, 2, &v)) {
                        reply_bad_request(req->clntFd,
                            "unknown yolo.runtime value",
                            "use 1 (librknnmrt.so 1.6.0) or 2 (librknnmrt2.so)");
                        return;
                    }
                    /* Applies to the next model activation; an already
                     * loaded context keeps running on its runtime. */
                    app_config.yolo_runtime = (unsigned int)v;
                } else if (EQUALS(key, "convert")) {
                    if (EQUALS_CASE(value, "rga")) {
                        app_config.yolo_convert = 1;
                    } else if (EQUALS_CASE(value, "cpu")) {
                        app_config.yolo_convert = 0;
                    } else {
                        reply_bad_request(req->clntFd,
                            "unknown yolo.convert value",
                            "use cpu (default) or rga (im2d fd)");
                        return;
                    }
                } else if (EQUALS(key, "boxes")) {
                    if (EQUALS(value, "1") || EQUALS_CASE(value, "true") ||
                        EQUALS_CASE(value, "on") || EQUALS_CASE(value, "yes")) {
                        app_config.yolo_boxes = true;
                        yolo_boxes_request(1);
                    } else if (EQUALS(value, "0") ||
                        EQUALS_CASE(value, "false") ||
                        EQUALS_CASE(value, "off") ||
                        EQUALS_CASE(value, "no")) {
                        app_config.yolo_boxes = false;
                        yolo_boxes_request(0);
                    } else {
                        reply_bad_request(req->clntFd,
                            "unknown yolo.boxes value",
                            "use 0/1 (or on/off)");
                        return;
                    }
                } else if (EQUALS(key, "input_width")) {
                    long v;
                    if (parse_ranged(value, 64, 4096, &v) &&
                        (unsigned int)v != app_config.yolo_input_width) {
                        app_config.yolo_input_width = (unsigned int)v;
                        restartNeeded = 1;
                        liveOk = 0;
                    }
                } else if (EQUALS(key, "input_height")) {
                    long v;
                    if (parse_ranged(value, 64, 4096, &v) &&
                        (unsigned int)v != app_config.yolo_input_height) {
                        app_config.yolo_input_height = (unsigned int)v;
                        restartNeeded = 1;
                        liveOk = 0;
                    }
                } else if (EQUALS(key, "fps")) {
                    long v;
                    if (parse_ranged(value, 1, 30, &v) &&
                        (unsigned int)v != app_config.yolo_fps) {
                        app_config.yolo_fps = (unsigned int)v;
                        restartNeeded = 1;
                        liveOk = 0;
                    }
                } else if (EQUALS(key, "conf_threshold")) {
                    double d = strtod(value, NULL);
                    if (d >= 0.01 && d <= 0.99)
                        app_config.yolo_conf_threshold = d;
                } else if (EQUALS(key, "nms_threshold")) {
                    double d = strtod(value, NULL);
                    if (d >= 0.01 && d <= 0.99)
                        app_config.yolo_nms_threshold = d;
                } else if (EQUALS(key, "classes")) {
                    long v;
                    if (parse_ranged(value, 1, 255, &v) &&
                        (unsigned int)v != app_config.yolo_classes) {
                        app_config.yolo_classes = (unsigned int)v;
                        restartNeeded = 1;
                        liveOk = 0;
                    }
                } else if (EQUALS(key, "pre_seconds")) {
                    long v;
                    if (parse_ranged(value, 0, 30, &v) &&
                        (unsigned int)v != app_config.yolo_pre_seconds) {
                        app_config.yolo_pre_seconds = (unsigned int)v;
                        restartNeeded = 1;
                        liveOk = 0;
                    }
                } else if (EQUALS(key, "post_seconds")) {
                    long v;
                    if (parse_ranged(value, 0, 30, &v) &&
                        (unsigned int)v != app_config.yolo_post_seconds) {
                        app_config.yolo_post_seconds = (unsigned int)v;
                        restartNeeded = 1;
                        liveOk = 0;
                    }
                } else if (EQUALS(key, "max_frames")) {
                    long v;
                    if (parse_ranged(value, 1, 60, &v) &&
                        (unsigned int)v != app_config.yolo_max_frames) {
                        app_config.yolo_max_frames = (unsigned int)v;
                        restartNeeded = 1;
                        liveOk = 0;
                    }
                }
            }

            if (enableReq >= 0) {
                if (enableReq != app_config.yolo_enable) {
                    app_config.yolo_enable = enableReq;
                    restartNeeded = 1;
                }
                /* Structural changes are applied by the next start: the
                 * frame source, ring buffers and model context are already
                 * allocated with the old values. */
                if (!liveOk && app_config.yolo_enable &&
                    !yolo_running()) {
                    restartNeeded = 1;
                } else if (app_config.yolo_enable && !yolo_running()) {
                    gating_result gate = gating_check("yolo");
                    if (!gate.ok) {
                        app_config.yolo_enable = false;
                        gating_block(req, gate);
                        return;
                    }
                    if (yolo_start()) {
                        app_config.yolo_enable = false;
                        reply_error(req->clntFd, "blocked",
                            "the detector could not be started",
                            "check the model and the kernel log");
                        return;
                    }
                } else if (!app_config.yolo_enable) {
                    yolo_stop();
                }
            }
        }

        respLen = sprintf(response,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json;charset=UTF-8\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"enable\":%s,\"source\":\"%s\",\"runtime\":%d,"
            "\"convert\":\"%s\",\"boxes\":%s,\"model_dir\":\"%s\","
            "\"model\":\"%s\","
            "\"input_width\":%d,\"input_height\":%d,\"fps\":%d,"
            "\"conf_threshold\":%.2f,\"nms_threshold\":%.2f,"
            "\"classes\":%d,\"pre_seconds\":%d,\"post_seconds\":%d,"
            "\"max_frames\":%d,\"running\":%s%s}",
            app_config.yolo_enable ? "true" : "false",
            app_config.yolo_source == 2 ? "vi0" :
                (app_config.yolo_source == 1 ? "vpss3" : "vi1"),
            app_config.yolo_runtime,
            app_config.yolo_convert ? "rga" : "cpu",
            app_config.yolo_boxes ? "true" : "false",
            app_config.yolo_model_dir, app_config.yolo_model,
            app_config.yolo_input_width, app_config.yolo_input_height,
            app_config.yolo_fps, app_config.yolo_conf_threshold,
            app_config.yolo_nms_threshold, app_config.yolo_classes,
            app_config.yolo_pre_seconds, app_config.yolo_post_seconds,
            app_config.yolo_max_frames, yolo_running() ? "true" : "false",
            restartNeeded ? ",\"note\":\"restart required for the new settings\""
                : "");
        send_and_close(req->clntFd, response, respLen);
        return;
    }

    /* OKO: focus assistant — AF statistics metric and optional STM8 motor. */
    if (EQUALS(req->uri, "/api/focus")) {
        int withMap = 0;
        int mutating = 0;
        if (req->query && *req->query) {
            int enableReq = -1, motorReq = -1, stopReq = 0, resetReq = 0;
            int mode = -1;
            long rx = -1, ry = -1, rw = -1, rh = -1, fps = -1;
            long speed = -1, move = 0, position = 0;
            int haveMove = 0, havePosition = 0;
            int homeReq = 0, zeroReq = 0;   /* 0 none, 1 focus, 2 zoom, 3 both */
            long zeroValue = 0;
            int haveZeroValue = 0;
            int motorSel = FOCUS_MOTOR_FOCUS; /* scale for move/speed/position */
            int afReq = 0;              /* 0 none, 1 full, 2 cancel, 3 fine */

            while (req->query) {
                char *value = split(&req->query, "&");
                char *key;
                if (!value || !*value) continue;
                key = split(&value, "=");
                if (!key || !*key || !value) continue;
                unescape_uri(value);

                if (EQUALS(key, "enable")) {
                    mutating = 1;
                    enableReq = query_bool(value);
                } else if (EQUALS(key, "mode")) {
                    mutating = 1;
                    if (EQUALS_CASE(value, "full")) mode = 0;
                    else if (EQUALS_CASE(value, "roi")) mode = 1;
                    else {
                        reply_bad_request(req->clntFd, "unknown focus.mode",
                            "use full or roi");
                        return;
                    }
                } else if (EQUALS(key, "rx") || EQUALS(key, "ry") ||
                           EQUALS(key, "rw") || EQUALS(key, "rh")) {
                    long v;
                    mutating = 1;
                    if (!parse_ranged(value, 0, 10000, &v)) {
                        reply_bad_request(req->clntFd,
                            "ROI values must be 0..10000", "basis points");
                        return;
                    }
                    if (EQUALS(key, "rx")) rx = v;
                    else if (EQUALS(key, "ry")) ry = v;
                    else if (EQUALS(key, "rw")) rw = v;
                    else rh = v;
                } else if (EQUALS(key, "fps")) {
                    long v;
                    mutating = 1;
                    if (!parse_ranged(value, 1, 10, &v)) {
                        reply_bad_request(req->clntFd, "fps must be 1..10", "");
                        return;
                    }
                    fps = v;
                } else if (EQUALS(key, "motor")) {
                    mutating = 1;
                    /* motor=focus|zoom selects a scale for move/speed/position;
                       the legacy motor=true|false arms or disarms the driver. */
                    if (EQUALS_CASE(value, "focus"))
                        motorSel = FOCUS_MOTOR_FOCUS;
                    else if (EQUALS_CASE(value, "zoom"))
                        motorSel = FOCUS_MOTOR_ZOOM;
                    else if (EQUALS_CASE(value, "both"))
                        motorSel = 2;
                    else if (query_bool(value) ||
                             EQUALS(value, "0") || EQUALS_CASE(value, "false") ||
                             EQUALS_CASE(value, "off") || EQUALS_CASE(value, "no"))
                        motorReq = query_bool(value);
                    else {
                        reply_bad_request(req->clntFd,
                            "motor must be focus, zoom, both, true or false", "");
                        return;
                    }
                } else if (EQUALS(key, "scale")) {
                    mutating = 1;
                    /* WebUI spelling: `motor=1` arms the driver and `scale`
                       selects the scale for move/speed/position. */
                    if (EQUALS_CASE(value, "focus"))
                        motorSel = FOCUS_MOTOR_FOCUS;
                    else if (EQUALS_CASE(value, "zoom"))
                        motorSel = FOCUS_MOTOR_ZOOM;
                    else if (EQUALS_CASE(value, "both"))
                        motorSel = 2;
                    else {
                        reply_bad_request(req->clntFd,
                            "scale must be focus, zoom or both", "");
                        return;
                    }
                } else if (EQUALS(key, "af")) {
                    mutating = 1;
                    if (EQUALS_CASE(value, "cancel"))
                        afReq = 2;
                    else if (EQUALS_CASE(value, "fine"))
                        afReq = 3;
                    else if (query_bool(value))
                        afReq = 1;
                    else if (EQUALS(value, "0") ||
                             EQUALS_CASE(value, "false") ||
                             EQUALS_CASE(value, "off") ||
                             EQUALS_CASE(value, "no"))
                        afReq = 0;
                    else {
                        reply_bad_request(req->clntFd,
                            "af must be 0, 1, fine or cancel", "");
                        return;
                    }
                } else if (EQUALS(key, "home") || EQUALS(key, "zero")) {
                    int target;
                    mutating = 1;
                    if (EQUALS_CASE(value, "focus")) target = 1;
                    else if (EQUALS_CASE(value, "zoom")) target = 2;
                    else if (EQUALS_CASE(value, "both")) target = 3;
                    else {
                        reply_bad_request(req->clntFd,
                            "home/zero must be focus, zoom or both", "");
                        return;
                    }
                    if (EQUALS(key, "home")) homeReq = target;
                    else zeroReq = target;
                } else if (EQUALS(key, "value")) {
                    long v;
                    mutating = 1;
                    if (!parse_ranged(value, -1000000, 1000000, &v)) {
                        reply_bad_request(req->clntFd,
                            "value is out of range", "-1000000..1000000");
                        return;
                    }
                    zeroValue = v;
                    haveZeroValue = 1;
                } else if (EQUALS(key, "speed")) {
                    long v;
                    mutating = 1;
                    if (!parse_ranged(value, 1, 5000, &v)) {
                        reply_bad_request(req->clntFd,
                            "speed must be 1..5000 steps/s", "");
                        return;
                    }
                    speed = v;
                } else if (EQUALS(key, "move")) {
                    long v;
                    mutating = 1;
                    if (!parse_ranged(value, -32768, 32767, &v)) {
                        reply_bad_request(req->clntFd,
                            "move must be -32768..32767", "");
                        return;
                    }
                    move = v;
                    haveMove = 1;
                } else if (EQUALS(key, "position")) {
                    long v;
                    mutating = 1;
                    if (!parse_ranged(value, -1000000, 1000000, &v)) {
                        reply_bad_request(req->clntFd,
                            "position is out of range", "");
                        return;
                    }
                    position = v;
                    havePosition = 1;
                } else if (EQUALS(key, "stop")) {
                    mutating = 1;
                    stopReq = query_bool(value);
                } else if (EQUALS(key, "reset")) {
                    mutating = 1;
                    resetReq = query_bool(value);
                } else if (EQUALS(key, "map")) {
                    withMap = query_bool(value);
                }
            }

            if (mutating && !EQUALS(req->method, "POST")) {
                reply_bad_request(req->clntFd,
                    "focus changes require POST", "GET is read-only");
                return;
            }
            if (rw >= 0 && rw < FOCUS_BP_MIN_ROI) {
                reply_bad_request(req->clntFd, "ROI width is too small",
                    "the minimum is 1000 basis points");
                return;
            }
            if (rh >= 0 && rh < FOCUS_BP_MIN_ROI) {
                reply_bad_request(req->clntFd, "ROI height is too small",
                    "the minimum is 1000 basis points");
                return;
            }

            /* Flag the worker before the motor=0/stop path disarms the driver,
               so a running search stops at its next check instead of fighting
               the disarm.  A plain stop=1 also cancels it. */
            if (afReq == 2 || stopReq)
                focus_af_cancel();

            if (mode >= 0) app_config.focus_mode = (unsigned int)mode;
            if (rx >= 0) app_config.focus_roi_x = (unsigned int)rx;
            if (ry >= 0) app_config.focus_roi_y = (unsigned int)ry;
            if (rw >= 0) app_config.focus_roi_w = (unsigned int)rw;
            if (rh >= 0) app_config.focus_roi_h = (unsigned int)rh;
            if (fps >= 0) app_config.focus_fps = (unsigned int)fps;

            /* The AF metric and its ROI do not need the motor, so apply them
             * before the arm block: a missing/busy STM8 must not block the
             * measurement (KI-043). */
            if (enableReq == 1) {
                gating_result gate = gating_check("focus_metric");
                if (!gate.ok) {
                    gating_block(req, gate);
                    return;
                }
                app_config.focus_enable = true;
                if (focus_start()) {
                    app_config.focus_enable = false;
                    reply_error(req->clntFd, "blocked",
                        "the focus statistics thread could not be started",
                        "check the kernel log and RKAIQ_ENABLE_AF");
                    return;
                }
            } else if (enableReq == 0) {
                app_config.focus_enable = false;
                focus_stop();
            }

            if (mode >= 0 || rx >= 0 || ry >= 0 || rw >= 0 || rh >= 0 ||
                fps >= 0) {
                int curMode;
                unsigned int cx, cy, cw, ch, cfps;
                focus_get_config(&curMode, &cx, &cy, &cw, &ch, &cfps);
                focus_configure(mode >= 0 ? mode : curMode,
                    rx >= 0 ? (unsigned int)rx : cx,
                    ry >= 0 ? (unsigned int)ry : cy,
                    rw >= 0 ? (unsigned int)rw : cw,
                    rh >= 0 ? (unsigned int)rh : ch,
                    fps >= 0 ? (unsigned int)fps : cfps);
            }

            {
                /* A metric/ROI request must still succeed when the motor is
                 * missing or busy; only pure motor requests keep the old
                 * blocked/409/502 responses (KI-043). */
                int haveSettings = enableReq >= 0 || mode >= 0 || fps >= 0 ||
                    rx >= 0 || ry >= 0 || rw >= 0 || rh >= 0;

            if (motorReq >= 0) {
                if (motorReq) {
                    gating_result gate;
                    app_config.focus_motor = true;
                    gate = gating_check("focus_motor");
                    if (!gate.ok) {
                        app_config.focus_motor = false;
                        if (!haveSettings) {
                            gating_block(req, gate);
                            return;
                        }
                        HAL_WARNING("server", "focus metric applied without "
                            "the motor: %s\n", gate.reason);
                    } else {
                        focus_motor_set_enabled(1);
                        if (focus_motor_probe() != FM_OK) {
                            focus_motor_set_enabled(0);
                            app_config.focus_motor = false;
                            if (!haveSettings) {
                                reply_error(req->clntFd, "blocked",
                                    "the STM8 controller does not answer",
                                    "check focus.device, focus.address and the pull-ups");
                                return;
                            }
                            HAL_WARNING("server", "focus metric applied: the "
                                "STM8 controller does not answer\n");
                        } else if (focus_motor_set_speed(FOCUS_MOTOR_FOCUS,
                                app_config.focus_speed) != FM_OK ||
                            focus_motor_set_speed(FOCUS_MOTOR_ZOOM,
                                app_config.focus_zoom_speed) != FM_OK) {
                            focus_motor_set_enabled(0);
                            app_config.focus_motor = false;
                            if (!haveSettings) {
                                reply_error(req->clntFd, "blocked",
                                    "the STM8 controller did not accept the speed",
                                    "check focus.speed and the I2C bus");
                                return;
                            }
                            HAL_WARNING("server", "focus metric applied: the "
                                "STM8 controller did not accept the speed\n");
                        }
                    }
                } else {
                    if (focus_home_running()) {
                        char body[160];
                        snprintf(body, sizeof(body),
                            "{\"error\":\"busy\",\"reason\":\"a home/zero job "
                            "is running\"}");
                        reply_status_json(req->clntFd, 409, body);
                        return;
                    }
                    if (focus_af_running() && afReq != 2 && !stopReq) {
                        char body[160];
                        snprintf(body, sizeof(body),
                            "{\"error\":\"busy\",\"reason\":\"an auto-focus "
                            "search is running\"}");
                        reply_status_json(req->clntFd, 409, body);
                        return;
                    }
                    int rc = focus_motor_stop_all();
                    if (rc != FM_OK) {
                        char body[320];
                        int len = snprintf(body, sizeof(body),
                            "{\"error\":\"i2c\",\"reason\":\"%s\"}",
                            focus_motor_error(rc));
                        reply_status_json(req->clntFd, 502, body);
                        return;
                    }
                    focus_motor_set_enabled(0);
                    app_config.focus_motor = false;
                }
            }
            }

            if (homeReq || zeroReq) {
                fm_snapshot snap;
                unsigned int mask;
                int target = homeReq ? homeReq : zeroReq;
                int supported = 0;
                int rc;

                if (homeReq && zeroReq) {
                    reply_bad_request(req->clntFd,
                        "home and zero cannot be combined", "use one job");
                    return;
                }
                if (!focus_motor_enabled()) {
                    reply_error(req->clntFd, "blocked",
                        "the focus motor is disabled", "set motor=true first");
                    return;
                }
                if (flash_busy()) {
                    char body[160];
                    snprintf(body, sizeof(body),
                        "{\"error\":\"busy\",\"reason\":\"a firmware upload "
                        "is running\"}");
                    reply_status_json(req->clntFd, 409, body);
                    return;
                }
                if (focus_home_running()) {
                    char body[160];
                    snprintf(body, sizeof(body),
                        "{\"error\":\"busy\",\"reason\":\"another home/zero "
                        "job is running\"}");
                    reply_status_json(req->clntFd, 409, body);
                    return;
                }
                if (focus_af_running()) {
                    char body[160];
                    snprintf(body, sizeof(body),
                        "{\"error\":\"busy\",\"reason\":\"an auto-focus "
                        "search is running\"}");
                    reply_status_json(req->clntFd, 409, body);
                    return;
                }
                if (focus_motor_probe() != FM_OK) {
                    char body[320];
                    snprintf(body, sizeof(body),
                        "{\"error\":\"i2c\",\"reason\":\"%s\"}",
                        "the STM8 controller does not answer");
                    reply_status_json(req->clntFd, 502, body);
                    return;
                }
                focus_motor_snapshot(&snap);
                if (!snap.present || snap.bootloader) {
                    reply_error(req->clntFd, "blocked",
                        "the STM8 application is not running",
                        "leave the bootloader and retry");
                    return;
                }
                /* SET_POS exists in 2.0.2+ and 2.1.1+; 2.1.0 is a test build without it,
                   so the minor line is checked separately. */
                if (snap.has_app_version)
                    supported = focus_proto_version_supported(
                        snap.app_version);
                if (!supported) {
                    char reason[192];
                    snprintf(reason, sizeof(reason),
                        "home/zero need STM8 firmware 2.0.2 or 2.1.1 "
                        "(controller reports %u.%u.%u)",
                        snap.has_app_version ? snap.app_version[0] : 0,
                        snap.has_app_version ? snap.app_version[1] : 0,
                        snap.has_app_version ? snap.app_version[2] : 0);
                    reply_error(req->clntFd, "unsupported", reason,
                        "update the controller firmware first");
                    return;
                }

                mask = target == 1 ? FOCUS_HOME_MOTOR_FOCUS :
                       (target == 2 ? FOCUS_HOME_MOTOR_ZOOM :
                        (FOCUS_HOME_MOTOR_FOCUS | FOCUS_HOME_MOTOR_ZOOM));
                rc = homeReq ? focus_home_start(mask) :
                    focus_zero_start(mask, haveZeroValue ? (int32_t)zeroValue
                                                         : 0);
                if (rc == -1) {
                    char body[160];
                    snprintf(body, sizeof(body),
                        "{\"error\":\"busy\",\"reason\":\"another home/zero "
                        "job is running\"}");
                    reply_status_json(req->clntFd, 409, body);
                    return;
                }
                if (rc != 0) {
                    reply_error(req->clntFd, "failed",
                        "the homing worker could not be started", "");
                    return;
                }
            }

            if (stopReq || resetReq || speed >= 0 || haveMove || havePosition) {
                int rc = FM_OK;
                int manual = speed >= 0 || haveMove || havePosition;
                int minp = motorSel == FOCUS_MOTOR_ZOOM
                    ? app_config.focus_zoom_min_pos : app_config.focus_min_pos;
                int maxp = motorSel == FOCUS_MOTOR_ZOOM
                    ? app_config.focus_zoom_max_pos : app_config.focus_max_pos;

                /* The WebUI STOP sends motor=0&stop=1: the arm block above
                   already stopped and disarmed the driver, so do not reject
                   the redundant stop with "disabled". */
                if (!focus_motor_enabled() && !(stopReq && motorReq == 0)) {
                    reply_error(req->clntFd, "blocked",
                        "the focus motor is disabled", "set motor=true first");
                    return;
                }
                if ((haveMove || havePosition) && motorSel == 2) {
                    reply_bad_request(req->clntFd,
                        "motor=both is only valid for speed/home/zero", "");
                    return;
                }
                if (manual && focus_home_running()) {
                    char body[160];
                    snprintf(body, sizeof(body),
                        "{\"error\":\"busy\",\"reason\":\"a home/zero job is "
                        "running\"}");
                    reply_status_json(req->clntFd, 409, body);
                    return;
                }
                if (manual && focus_af_running()) {
                    char body[160];
                    snprintf(body, sizeof(body),
                        "{\"error\":\"busy\",\"reason\":\"an auto-focus "
                        "search is running\"}");
                    reply_status_json(req->clntFd, 409, body);
                    return;
                }
                if (resetReq)
                    rc = focus_motor_reset();
                else if (stopReq)
                    rc = focus_motor_stop_all();
                else if (speed >= 0) {
                    if (motorSel == 2) {
                        rc = focus_motor_set_speed(FOCUS_MOTOR_FOCUS,
                            (uint16_t)speed);
                        if (rc == FM_OK)
                            rc = focus_motor_set_speed(FOCUS_MOTOR_ZOOM,
                                (uint16_t)speed);
                    } else
                        rc = focus_motor_set_speed((uint8_t)motorSel,
                            (uint16_t)speed);
                }
                else if (havePosition) {
                    int32_t current = 0;
                    rc = focus_motor_get_position((uint8_t)motorSel, &current);
                    if (rc == FM_OK) {
                        long delta = (long)focus_proto_clamp(position, minp,
                            maxp) - current;
                        if (delta < -32768) delta = -32768;
                        if (delta > 32767) delta = 32767;
                        rc = focus_motor_move((uint8_t)motorSel, (int16_t)delta);
                    }
                } else if (haveMove) {
                    int32_t current = 0;
                    rc = focus_motor_get_position((uint8_t)motorSel, &current);
                    if (rc == FM_OK) {
                        long delta = (long)focus_proto_clamp(
                            (int64_t)current + move, minp, maxp) - current;
                        if (delta < -32768) delta = -32768;
                        if (delta > 32767) delta = 32767;
                        rc = focus_motor_move((uint8_t)motorSel, (int16_t)delta);
                    }
                }

                if (rc != FM_OK) {
                    char body[320];
                    int len = snprintf(body, sizeof(body),
                        "{\"error\":\"i2c\",\"reason\":\"%s\"}",
                        focus_motor_error(rc));
                    reply_status_json(req->clntFd, 502, body);
                    return;
                }
            }

            if (afReq == 1 || afReq == 3) {
                fm_snapshot snap;
                int rc;

                if (!focus_motor_enabled()) {
                    reply_error(req->clntFd, "blocked",
                        "the focus motor is disabled", "set motor=true first");
                    return;
                }
                if (flash_busy()) {
                    char body[160];
                    snprintf(body, sizeof(body),
                        "{\"error\":\"busy\",\"reason\":\"a firmware upload "
                        "is running\"}");
                    reply_status_json(req->clntFd, 409, body);
                    return;
                }
                if (focus_home_running()) {
                    char body[160];
                    snprintf(body, sizeof(body),
                        "{\"error\":\"busy\",\"reason\":\"a home/zero job is "
                        "running\"}");
                    reply_status_json(req->clntFd, 409, body);
                    return;
                }
                if (focus_af_running()) {
                    char body[160];
                    snprintf(body, sizeof(body),
                        "{\"error\":\"busy\",\"reason\":\"an auto-focus "
                        "search is already running\"}");
                    reply_status_json(req->clntFd, 409, body);
                    return;
                }
                if (!focus_running()) {
                    reply_error(req->clntFd, "blocked",
                        "the focus statistics are not running",
                        "set enable=1 before starting the auto-focus");
                    return;
                }
                if (focus_motor_probe() != FM_OK) {
                    char body[320];
                    snprintf(body, sizeof(body),
                        "{\"error\":\"i2c\",\"reason\":\"%s\"}",
                        "the STM8 controller does not answer");
                    reply_status_json(req->clntFd, 502, body);
                    return;
                }
                focus_motor_snapshot(&snap);
                if (!snap.present || snap.bootloader) {
                    reply_error(req->clntFd, "blocked",
                        "the STM8 application is not running",
                        "leave the bootloader and retry");
                    return;
                }
                rc = focus_af_start(FOCUS_MOTOR_FOCUS, afReq == 3 ? 1 : 0);
                if (rc == -1) {
                    char body[160];
                    snprintf(body, sizeof(body),
                        "{\"error\":\"busy\",\"reason\":\"an auto-focus "
                        "search is already running\"}");
                    reply_status_json(req->clntFd, 409, body);
                    return;
                }
                if (rc != 0) {
                    reply_error(req->clntFd, "failed",
                        "the auto-focus worker could not be started", "");
                    return;
                }
            }
        }

        {
            char *body;
            body = focus_json(withMap);
            if (!body)
                reply_error(req->clntFd, "failed", "out of memory", "");
            else {
                reply_json(req->clntFd, body, strlen(body));
                free(body);
            }
        }
        return;
    }

    /* OKO: controller firmware upload and cached versions. */
    if (EQUALS(req->uri, "/api/flash")) {
        if (EQUALS(req->method, "GET")) {
            char *body = flash_json();
            if (!body)
                reply_error(req->clntFd, "failed", "out of memory", "");
            else {
                reply_json(req->clntFd, body, strlen(body));
                free(body);
            }
            return;
        }
        if (EQUALS(req->method, "POST")) {
            int code = 500;
            char *type = request_header("Content-Type");
            char *body = flash_post(type, req->payload,
                req->total - req->header_len, &code);
            if (!body)
                reply_error(req->clntFd, "failed", "out of memory", "");
            else {
                reply_status_json(req->clntFd, code, body);
                free(body);
            }
            return;
        }
    }

    /* OKO: software update through the microSD card.
     *   GET  /api/update             — snapshot, staged package, apply token
     *   POST /api/update/upload      — streamed raw body (server_thread)
     *   POST /api/update/fetch?url=  — curl download, then stage
     *   POST /api/update/apply       — confirm=APPLY&token=...
     *   POST /api/update?clear=1     — drop the staged files
     *   POST /api/update?cancel=1    — stop the current worker/upload */
    if (STARTS_WITH(req->uri, "/api/update")) {
        if (EQUALS(req->uri, "/api/update")) {
            if (EQUALS(req->method, "GET")) {
                char *body = update_json();
                if (!body)
                    reply_error(req->clntFd, "failed", "out of memory", "");
                else {
                    reply_json(req->clntFd, body, strlen(body));
                    free(body);
                }
                return;
            }
            if (EQUALS(req->method, "POST")) {
                int clear = 0, cancel = 0;

                if (req->query) {
                    while (req->query) {
                        char *value = split(&req->query, "&");
                        if (!value || !*value) continue;
                        char *key = split(&value, "=");
                        if (!key || !*key || !value || !*value) continue;
                        unescape_uri(value);
                        if (EQUALS(key, "clear") && query_bool(value))
                            clear = 1;
                        else if (EQUALS(key, "cancel") && query_bool(value))
                            cancel = 1;
                    }
                }
                if (cancel) {
                    if (update_cancel() != 0) {
                        reply_status_json(req->clntFd, 409,
                            "{\"error\":\"busy\",\"reason\":\"the update is "
                            "already armed and cannot be cancelled\"}");
                    } else {
                        static const char cancelled[] =
                            "{\"cancelled\":true}";
                        reply_json(req->clntFd, cancelled,
                            sizeof(cancelled) - 1);
                    }
                    return;
                }
                if (clear) {
                    char reason[192];
                    if (update_clear(reason, sizeof(reason))) {
                        char body[320];
                        int len = snprintf(body, sizeof(body),
                            "{\"error\":\"blocked\",\"reason\":\"%s\"}",
                            reason);
                        reply_status_json(req->clntFd, 409, body);
                    } else {
                        {
                            static const char cleared[] = "{\"cleared\":true}";
                            reply_json(req->clntFd, cleared,
                                sizeof(cleared) - 1);
                        }
                    }
                    return;
                }
                {
                    char *body = update_json();
                    if (!body)
                        reply_error(req->clntFd, "failed", "out of memory",
                            "");
                    else {
                        reply_json(req->clntFd, body, strlen(body));
                        free(body);
                    }
                }
                return;
            }
            send_http_error(req->clntFd, 405);
            return;
        }
        if (EQUALS(req->uri, "/api/update/upload")) {
            if (!EQUALS(req->method, "POST")) {
                send_http_error(req->clntFd, 405);
                return;
            }
            reply_bad_request(req->clntFd,
                "the package body must be streamed",
                "send the .okp file as the raw request body");
            return;
        }
        if (EQUALS(req->uri, "/api/update/fetch")) {
            char url[1024] = "";
            char reason[192];

            if (!EQUALS(req->method, "POST")) {
                send_http_error(req->clntFd, 405);
                return;
            }
            if (req->query) {
                while (req->query) {
                    char *value = split(&req->query, "&");
                    if (!value || !*value) continue;
                    char *key = split(&value, "=");
                    if (!key || !*key || !value || !*value) continue;
                    unescape_uri(value);
                    if (EQUALS(key, "url"))
                        snprintf(url, sizeof(url), "%s", value);
                }
            }
            if (update_fetch_start(url, reason, sizeof(reason))) {
                char body[320];
                int len = snprintf(body, sizeof(body),
                    "{\"error\":\"blocked\",\"reason\":\"%s\"}", reason);
                reply_status_json(req->clntFd, 409, body);
            } else {
                reply_status_json(req->clntFd, 202,
                    "{\"state\":\"fetching\"}");
            }
            return;
        }
        if (EQUALS(req->uri, "/api/update/apply")) {
            char confirm[16] = "", token[64] = "";
            char reason[192];

            if (!EQUALS(req->method, "POST")) {
                send_http_error(req->clntFd, 405);
                return;
            }
            if (req->query) {
                while (req->query) {
                    char *value = split(&req->query, "&");
                    if (!value || !*value) continue;
                    char *key = split(&value, "=");
                    if (!key || !*key || !value || !*value) continue;
                    unescape_uri(value);
                    if (EQUALS(key, "confirm"))
                        snprintf(confirm, sizeof(confirm), "%s", value);
                    else if (EQUALS(key, "token"))
                        snprintf(token, sizeof(token), "%s", value);
                }
            }
            if (update_apply_start(confirm, token, reason, sizeof(reason))) {
                char body[320];
                int len = snprintf(body, sizeof(body),
                    "{\"error\":\"blocked\",\"reason\":\"%s\"}", reason);
                int status = strstr(reason, "another update job") ? 409 : 400;
                reply_status_json(req->clntFd, status, body);
            } else {
                reply_status_json(req->clntFd, 202,
                    "{\"state\":\"applying\"}");
            }
            return;
        }
        send_http_error(req->clntFd, 404);
        return;
    }

    if (EQUALS(req->uri, "/api/firmware")) {
        char *body;
        if (!EQUALS(req->method, "GET")) {
            send_http_error(req->clntFd, 405);
            return;
        }
        body = firmware_status_json();
        if (!body)
            reply_error(req->clntFd, "failed", "out of memory", "");
        else {
            reply_json(req->clntFd, body, strlen(body));
            free(body);
        }
        return;
    }

    if (EQUALS(req->uri, "/api/ivs")) {
        if (req->query) {
            int enableReq = -1;
            while (req->query) {
                char *value = split(&req->query, "&");
                if (!value || !*value) continue;
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
                unescape_uri(value);
                if (EQUALS(key, "enable")) {
                    enableReq = query_bool(value);
                } else if (EQUALS(key, "mode")) {
                    if (EQUALS_CASE(value, "md")) app_config.ivs_mode = HAL_IVS_MD;
                    else if (EQUALS_CASE(value, "od")) app_config.ivs_mode = HAL_IVS_OD;
                    else if (EQUALS_CASE(value, "md_od")) app_config.ivs_mode = HAL_IVS_MD_OD;
                } else if (EQUALS(key, "width")) {
                    long v; if (parse_ranged(value, 160, 1920, &v)) app_config.ivs_width = (unsigned int)v;
                } else if (EQUALS(key, "height")) {
                    long v; if (parse_ranged(value, 120, 1080, &v)) app_config.ivs_height = (unsigned int)v;
                } else if (EQUALS(key, "sensitivity")) {
                    long v; if (parse_ranged(value, 1, 3, &v)) app_config.ivs_sensitivity = (unsigned int)v;
                } else if (EQUALS(key, "md_interval")) {
                    long v; if (parse_ranged(value, 1, 100, &v)) app_config.ivs_md_interval = (unsigned int)v;
                } else if (EQUALS(key, "od_interval")) {
                    long v; if (parse_ranged(value, 1, 100, &v)) app_config.ivs_od_interval = (unsigned int)v;
                } else if (EQUALS(key, "od_percent")) {
                    long v; if (parse_ranged(value, 5, 8, &v)) app_config.ivs_od_percent = (unsigned int)v;
                } else if (EQUALS(key, "act_http_post")) {
                    app_config.ivs_act_http_post = query_bool(value);
                } else if (EQUALS(key, "act_record")) {
                    if (query_bool(value)) {
                        gating_result gate = gating_check("record");
                        if (!gate.ok) { gating_block(req, gate); return; }
                    }
                    app_config.ivs_act_record = query_bool(value);
                } else if (EQUALS(key, "act_osd")) {
                    app_config.ivs_act_osd = query_bool(value);
                } else if (EQUALS(key, "record_seconds")) {
                    long v; if (parse_ranged(value, 1, 600, &v)) app_config.ivs_record_seconds = (unsigned int)v;
                } else if (EQUALS(key, "record_source")) {
                    app_config.ivs_record_sub = EQUALS(value, "sub");
                }
            }

            if (enableReq == 1) {
                gating_result gate = gating_check("ivs");
                if (!gate.ok) { gating_block(req, gate); return; }
            }

            ivs_events_stop();
            if (enableReq == 1) {
                app_config.ivs_enable = true;
                ivs_events_start();
            } else if (enableReq == 0) {
                app_config.ivs_enable = false;
            } else if (app_config.ivs_enable) {
                ivs_events_start();
            }
        }

        char events[2048];
        ivs_events_json(events, sizeof(events));
        respLen = sprintf(response,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json;charset=UTF-8\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"enable\":%s,\"mode\":\"%s\",\"width\":%d,\"height\":%d,"
            "\"sensitivity\":%d,\"md_interval\":%d,\"od_interval\":%d,"
            "\"od_percent\":%d,\"act_http_post\":%s,\"act_record\":%s,"
            "\"act_osd\":%s,\"record_seconds\":%d,\"record_source\":\"%s\","
            "\"events\":%s}",
            app_config.ivs_enable ? "true" : "false",
            app_config.ivs_mode == HAL_IVS_OD ? "od" :
                (app_config.ivs_mode == HAL_IVS_MD_OD ? "md_od" : "md"),
            app_config.ivs_width, app_config.ivs_height,
            app_config.ivs_sensitivity, app_config.ivs_md_interval,
            app_config.ivs_od_interval, app_config.ivs_od_percent,
            app_config.ivs_act_http_post ? "true" : "false",
            app_config.ivs_act_record ? "true" : "false",
            app_config.ivs_act_osd ? "true" : "false",
            app_config.ivs_record_seconds,
            app_config.ivs_record_sub ? "sub" : "main", events);
        send_and_close(req->clntFd, response, respLen);
        return;
    }

    if (EQUALS(req->uri, "/api/iq")) {
        if (req->query) {
            while (req->query) {
                char *value = split(&req->query, "&");
                if (!value || !*value) continue;
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
                unescape_uri(value);
                if (EQUALS(key, "main_scene")) {
                    strncpy(app_config.iq_main_scene, value, sizeof(app_config.iq_main_scene) - 1);
                    app_config.iq_main_scene[sizeof(app_config.iq_main_scene) - 1] = 0;
                } else if (EQUALS(key, "sub_scene")) {
                    strncpy(app_config.iq_sub_scene, value, sizeof(app_config.iq_sub_scene) - 1);
                    app_config.iq_sub_scene[sizeof(app_config.iq_sub_scene) - 1] = 0;
                } else if (EQUALS(key, "file")) {
                    strncpy(app_config.iq_file, value, sizeof(app_config.iq_file) - 1);
                    app_config.iq_file[sizeof(app_config.iq_file) - 1] = 0;
                }
            }

            if (*app_config.iq_file || *app_config.iq_main_scene) {
                gating_result gate = gating_check("iq");
                if (!gate.ok) { gating_block(req, gate); return; }
            }

            if (media_apply_iq(app_config.iq_main_scene, app_config.iq_sub_scene,
                    app_config.iq_file))
                HAL_WARNING("server", "IQ switch could not be applied\n");
        }

        {
            char scenes[512] = "";
            char files[1024] = "";
            int sceneCount = 0, fileCount = 0;
            DIR *dir = opendir("/oem/usr/share/iqfiles");
            struct dirent *entry;
            size_t used = 0;

            if (dir) {
                while ((entry = readdir(dir)) && sceneCount + fileCount < 32) {
                    if (entry->d_name[0] == '.') continue;
                    if (entry->d_type == DT_DIR && sceneCount < 8) {
                        used += snprintf(scenes + used, sizeof(scenes) - used,
                            "%s\"%s\"", sceneCount ? "," : "", entry->d_name);
                        sceneCount++;
                    } else if ((strstr(entry->d_name, ".xml") ||
                        strstr(entry->d_name, ".json")) && fileCount < 16) {
                        used = strlen(files);
                        used += snprintf(files + used, sizeof(files) - used,
                            "%s\"%s\"", fileCount ? "," : "", entry->d_name);
                        fileCount++;
                    }
                }
                closedir(dir);
            }

            respLen = sprintf(response,
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json;charset=UTF-8\r\n"
                "Connection: close\r\n"
                "\r\n"
                "{\"main_scene\":\"%s\",\"sub_scene\":\"%s\",\"file\":\"%s\","
                "\"scenes\":[%s],\"files\":[%s]}",
                app_config.iq_main_scene, app_config.iq_sub_scene,
                app_config.iq_file, scenes, files);
            send_and_close(req->clntFd, response, respLen);
            return;
        }
    }

    if (EQUALS(req->uri, "/api/hdr")) {
        int restart = 0, applyRestart = 0;
        unsigned int oldMode = app_config.hdr_mode;
        if (req->query) {
            while (req->query) {
                char *value = split(&req->query, "&");
                if (!value || !*value) continue;
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
                unescape_uri(value);
                if (EQUALS(key, "mode")) {
                    unsigned int mode = 0;
                    if (EQUALS_CASE(value, "hdr2")) mode = 1;
                    else if (EQUALS_CASE(value, "hdr3")) mode = 2;
                    if (mode != 0) {
                        gating_result gate = gating_check("hdr");
                        if (!gate.ok) { gating_block(req, gate); return; }
                    }
                    app_config.hdr_mode = mode;
                } else if (EQUALS(key, "apply")) {
                    if (EQUALS_CASE(value, "restart")) {
                        app_config.hdr_apply = 2;
                        applyRestart = 1;
                    } else if (EQUALS_CASE(value, "dyn")) {
                        app_config.hdr_apply = 1;
                    } else {
                        app_config.hdr_apply = 0;
                    }
                }
            }
            restart = applyRestart || app_config.hdr_mode != oldMode;
        }

        respLen = sprintf(response,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json;charset=UTF-8\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"mode\":\"%s\",\"apply\":\"%s\",\"note\":\"HDR is applied by restarting the application\"}",
            app_config.hdr_mode == 1 ? "hdr2" : (app_config.hdr_mode == 2 ? "hdr3" : "normal"),
            app_config.hdr_apply == 2 ? "restart" :
                (app_config.hdr_apply == 1 ? "dyn" : "auto"));
        send_and_close(req->clntFd, response, respLen);
        if (restart) {
            /* The mode is part of the AIQ prepare call, so persist it before
             * the graceful restart re-reads divinus.yaml. */
            app_config_save();
            kill(getpid(), SIGHUP);
        }
        return;
    }

    if (EQUALS(req->uri, "/api/streams")) {
        int mainChn = rtspHandle ? rtsp_get_stream_channel(rtspHandle, 0) : -1;
        int subChn = rtspHandle ? rtsp_get_stream_channel(rtspHandle, 1) : -1;
        int subEnabled = app_config.substream_enable && subChn >= 0;
        int connections = rtspHandle ? rtsp_get_connection_count(rtspHandle) : 0;
        const char *mainCodec = "none", *subCodec = "none";

        if (mainChn >= 0)
            mainCodec = app_config.mp4_codecH265 ? "H.265" : "H.264";
        if (subChn >= 0)
            subCodec = app_config.substream_codecH265 ? "H.265" : "H.264";

        respLen = sprintf(response,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json;charset=UTF-8\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"connections\":%d,\"max_connections\":%d,\"streams\":["
            "{\"id\":0,\"path\":\"/live/0\",\"channel\":%d,\"codec\":\"%s\",\"enabled\":%s},"
            "{\"id\":1,\"path\":\"%s\",\"channel\":%d,\"codec\":\"%s\",\"enabled\":%s}]}",
            connections, RTSP_MAXIMUM_CONNECTIONS,
            mainChn, mainCodec, mainChn >= 0 ? "true" : "false",
            app_config.substream_path, subChn, subCodec,
            subEnabled ? "true" : "false");
        send_and_close(req->clntFd, response, respLen);
        return;
    }

    if (EQUALS(req->uri, "/api/network")) {
        char mode[8] = "", ipaddr[16] = "", netmask[16] = "";
        char gateway[16] = "", dns[16] = "";
        bool haveMode = false;
        char fieldMode[8] = "dhcp", fieldIp[16] = "", fieldMask[16] = "";
        char fieldGateway[16] = "", fieldDns[16] = "", fieldMac[18] = "";
        char currentIp[16] = "", currentMac[18] = "", dhcpRunning[8] = "false";
        char commandOut[1024];

        if (req->query) {
            while (req->query) {
                char *value = split(&req->query, "&");
                if (!value || !*value) continue;
                char *key = split(&value, "=");
                if (!key || !*key || !value) continue;
                unescape_uri(value);
                if (EQUALS(key, "mode")) {
                    snprintf(mode, sizeof(mode), "%s", value);
                    haveMode = true;
                } else if (EQUALS(key, "ipaddr")) {
                    snprintf(ipaddr, sizeof(ipaddr), "%s", value);
                } else if (EQUALS(key, "netmask")) {
                    snprintf(netmask, sizeof(netmask), "%s", value);
                } else if (EQUALS(key, "gateway")) {
                    snprintf(gateway, sizeof(gateway), "%s", value);
                } else if (EQUALS(key, "dns")) {
                    snprintf(dns, sizeof(dns), "%s", value);
                }
            }
        }

        if (haveMode) {
            const char *error = NULL;
            if (strcmp(mode, "dhcp") && strcmp(mode, "static")) {
                error = "mode must be dhcp or static";
            } else if (!strcmp(mode, "static") &&
                (!valid_ipv4(ipaddr) || !valid_ipv4(netmask))) {
                error = "static mode requires a valid IPv4 address and netmask";
            } else if (*gateway && !valid_ipv4(gateway)) {
                error = "invalid gateway address";
            } else if (*dns && !valid_ipv4(dns)) {
                error = "invalid DNS address";
            }

            if (error) {
                char errorResponse[320];
                int errorLen = sprintf(errorResponse,
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/json;charset=UTF-8\r\n"
                    "Connection: close\r\n"
                    "\r\n"
                    "{\"error\":\"blocked\",\"reason\":\"%s\"}", error);
                send_and_close(req->clntFd, errorResponse, errorLen);
                return;
            }

            snprintf(commandOut, sizeof(commandOut),
                "/usr/bin/oko-net set '%s' '%s' '%s' '%s' '%s'",
                mode, ipaddr, netmask, gateway, dns);
            if (system(commandOut) != 0) {
                char errorResponse[320];
                int errorLen = sprintf(errorResponse,
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/json;charset=UTF-8\r\n"
                    "Connection: close\r\n"
                    "\r\n"
                    "{\"error\":\"failed\",\"reason\":\""
                    "cannot write the U-Boot environment\"}");
                send_and_close(req->clntFd, errorResponse, errorLen);
                return;
            }
        }

        if (capture_command("/usr/bin/oko-net env", commandOut,
                sizeof(commandOut)) == 0) {
            char *line = strtok(commandOut, "\n");
            while (line) {
                char *value = strchr(line, '=');
                if (value) {
                    *value++ = '\0';
                    if (!strcmp(line, "mode"))
                        snprintf(fieldMode, sizeof(fieldMode), "%s", value);
                    else if (!strcmp(line, "ipaddr"))
                        snprintf(fieldIp, sizeof(fieldIp), "%s", value);
                    else if (!strcmp(line, "netmask"))
                        snprintf(fieldMask, sizeof(fieldMask), "%s", value);
                    else if (!strcmp(line, "gateway"))
                        snprintf(fieldGateway, sizeof(fieldGateway), "%s", value);
                    else if (!strcmp(line, "dns"))
                        snprintf(fieldDns, sizeof(fieldDns), "%s", value);
                    else if (!strcmp(line, "mac"))
                        snprintf(fieldMac, sizeof(fieldMac), "%s", value);
                    else if (!strcmp(line, "current_ip"))
                        snprintf(currentIp, sizeof(currentIp), "%s", value);
                    else if (!strcmp(line, "current_mac"))
                        snprintf(currentMac, sizeof(currentMac), "%s", value);
                    else if (!strcmp(line, "dhcp_running"))
                        snprintf(dhcpRunning, sizeof(dhcpRunning), "%s", value);
                }
                line = strtok(NULL, "\n");
            }
        }

        respLen = sprintf(response,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json;charset=UTF-8\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"mode\":\"%s\",\"ipaddr\":\"%s\",\"netmask\":\"%s\","
            "\"gateway\":\"%s\",\"dns\":\"%s\",\"mac\":\"%s\","
            "\"current_ip\":\"%s\",\"current_mac\":\"%s\","
            "\"dhcp_running\":%s}",
            fieldMode, fieldIp, fieldMask, fieldGateway, fieldDns, fieldMac,
            currentIp, currentMac, dhcpRunning);
        send_and_close(req->clntFd, response, respLen);

        /* The static IP change can drop this TCP connection; apply after the
         * response has been sent. */
        if (haveMode)
            system("(sleep 1; /usr/bin/oko-net apply) >/dev/null 2>&1 &");
        return;
    }

    /* OKO: VPN profiles (WireGuard and AmneziaWG).
     *   GET  /api/vpn          — profiles, live counters, availability, token
     *   POST /api/vpn          — action=up|down|enable|disable|delete
     *   POST /api/vpn/import   — multipart "file" or form field "text"
     *   GET  /api/vpn/export   — download a profile (it contains the keys) */
    if (STARTS_WITH(req->uri, "/api/vpn")) {
        if (EQUALS(req->uri, "/api/vpn")) {
            if (EQUALS(req->method, "GET")) {
                char body[16384];
                size_t len = vpn_status_json(body, sizeof(body));

                if (!len) {
                    reply_error(req->clntFd, "failed",
                        "cannot read the VPN state",
                        "check /var/log/vpn.log and the profile files");
                    return;
                }
                reply_json(req->clntFd, body, len);
                return;
            }

            if (!EQUALS(req->method, "POST")) {
                send_http_error(req->clntFd, 405);
                return;
            }

            {
                char action[16] = "", profile[64] = "", token[32] = "";
                char error[VPN_ERROR_MAX];
                char body[320];
                int len;

                if (req->query) {
                    while (req->query) {
                        char *value = split(&req->query, "&");
                        char *key;

                        if (!value || !*value) continue;
                        key = split(&value, "=");
                        if (!key || !*key || !value) continue;
                        unescape_uri(value);
                        if (EQUALS(key, "action"))
                            snprintf(action, sizeof(action), "%s", value);
                        else if (EQUALS(key, "profile"))
                            snprintf(profile, sizeof(profile), "%s", value);
                        else if (EQUALS(key, "token"))
                            snprintf(token, sizeof(token), "%s", value);
                    }
                }
                if (!action[0]) {
                    reply_bad_request(req->clntFd,
                        "the 'action' parameter is missing",
                        "use up, down, enable, disable or delete");
                    return;
                }
                if (vpn_action(action, profile, token, error,
                        sizeof(error))) {
                    reply_error(req->clntFd, "blocked", error,
                        "the profile stays in its current state");
                    return;
                }
                if (EQUALS(action, "up"))
                    len = snprintf(body, sizeof(body),
                        "{\"action\":\"%s\",\"profile\":\"%s\","
                        "\"queued\":true,\"note\":"
                        "\"connecting; the state is polled by the WebUI\"}",
                        action, profile);
                else
                    len = snprintf(body, sizeof(body),
                        "{\"action\":\"%s\",\"profile\":\"%s\","
                        "\"applied\":true}", action, profile);
                HAL_INFO("server", "VPN action '%s' for '%s'\n", action,
                    profile);
                reply_json(req->clntFd, body, (size_t)len);
                return;
            }
        }

        if (EQUALS(req->uri, "/api/vpn/export")) {
            char profile[64] = "", path[256], filename[96];
            char error[VPN_ERROR_MAX];

            if (!EQUALS(req->method, "GET")) {
                send_http_error(req->clntFd, 405);
                return;
            }
            if (req->query) {
                while (req->query) {
                    char *value = split(&req->query, "&");
                    char *key;

                    if (!value || !*value) continue;
                    key = split(&value, "=");
                    if (!key || !*key || !value) continue;
                    unescape_uri(value);
                    if (EQUALS(key, "profile"))
                        snprintf(profile, sizeof(profile), "%s", value);
                }
            }
            if (vpn_export_path(profile, path, sizeof(path), error,
                    sizeof(error))) {
                reply_error(req->clntFd, "blocked", error,
                    "reload the VPN section");
                return;
            }
            snprintf(filename, sizeof(filename), "%s.conf", profile);
            send_stream_file(req->clntFd, path, "application/octet-stream",
                filename, true, 0);
            return;
        }

        if (EQUALS(req->uri, "/api/vpn/import")) {
            char *type = request_header("Content-Type");
            int body_len = req->total - req->header_len;
            char hint[8] = "";
            char user[128] = "", pass[128] = "";
            char name[VPN_PROFILE_NAME_MAX];
            char error[VPN_ERROR_MAX];
            char body[320];
            int len;

            if (!EQUALS(req->method, "POST")) {
                send_http_error(req->clntFd, 405);
                return;
            }

            if (type && STARTS_WITH(type, "multipart/form-data")) {
                char bound[96], marker[104];
                char *boundary, *start, *stop;
                size_t uploaded_len;

                if (body_len <= 0) {
                    reply_bad_request(req->clntFd,
                        "the uploaded configuration is empty", "");
                    return;
                }
                if (req->query) {
                    while (req->query) {
                        char *value = split(&req->query, "&");
                        char *key;

                        if (!value || !*value) continue;
                        key = split(&value, "=");
                        if (!key || !*key || !value) continue;
                        unescape_uri(value);
                        if (EQUALS(key, "type"))
                            snprintf(hint, sizeof(hint), "%s", value);
                        else if (EQUALS(key, "user"))
                            snprintf(user, sizeof(user), "%s", value);
                        else if (EQUALS(key, "pass"))
                            snprintf(pass, sizeof(pass), "%s", value);
                    }
                }
                boundary = strstr(type, "boundary=");
                if (!boundary) {
                    reply_bad_request(req->clntFd,
                        "the multipart boundary is missing",
                        "select a .conf file");
                    return;
                }
                boundary += strlen("boundary=");
                {
                    size_t bound_len = strcspn(boundary, ";\r\n");
                    if (bound_len >= 2 && boundary[0] == '"' &&
                        boundary[bound_len - 1] == '"') {
                        boundary++;
                        bound_len -= 2;
                    }
                    if (bound_len == 0 || bound_len >= sizeof(bound)) {
                        reply_bad_request(req->clntFd,
                            "the multipart boundary is invalid", "");
                        return;
                    }
                    snprintf(bound, sizeof(bound), "%.*s", (int)bound_len,
                        boundary);
                }
                start = memstr(req->payload, "\r\n\r\n", body_len, 4);
                if (!start) {
                    reply_bad_request(req->clntFd,
                        "the multipart body has no part header",
                        "send the configuration as the 'file' field");
                    return;
                }
                start += 4;
                snprintf(marker, sizeof(marker), "--%s", bound);
                stop = memstr(start, marker, body_len - (int)(start -
                    req->payload), (char)strlen(marker));
                if (stop) {
                    if (stop - start >= 2) stop -= 2;
                    else stop = start;
                } else {
                    stop = req->payload + body_len;
                }
                uploaded_len = (size_t)(stop - start);
                multipart_field(req->payload, body_len, bound, "user",
                    user, sizeof(user));
                multipart_field(req->payload, body_len, bound, "pass",
                    pass, sizeof(pass));
                if (vpn_import_text(start, uploaded_len, hint, user, pass,
                        name, sizeof(name), error, sizeof(error))) {
                    reply_error(req->clntFd, "blocked", error,
                        "the file must be a wg-quick or an OpenVPN profile");
                    return;
                }
            } else if (type && STARTS_WITH(type,
                    "application/x-www-form-urlencoded")) {
                char text[VPN_CONF_MAX_BYTES + 1];
                size_t text_len = 0;

                text[0] = '\0';
                if (req->query) {
                    while (req->query) {
                        char *value = split(&req->query, "&");
                        char *key;

                        if (!value || !*value) continue;
                        key = split(&value, "=");
                        if (!key || !*key || !value) continue;
                        unescape_uri(value);
                        if (EQUALS(key, "text")) {
                            if (strlen(value) > VPN_CONF_MAX_BYTES) {
                                reply_bad_request(req->clntFd,
                                    "the configuration is larger than 8 KiB",
                                    "");
                                return;
                            }
                            snprintf(text, sizeof(text), "%s", value);
                            text_len = strlen(text);
                        } else if (EQUALS(key, "type")) {
                            snprintf(hint, sizeof(hint), "%s", value);
                        } else if (EQUALS(key, "user")) {
                            snprintf(user, sizeof(user), "%s", value);
                        } else if (EQUALS(key, "pass")) {
                            snprintf(pass, sizeof(pass), "%s", value);
                        }
                    }
                }
                if (text_len == 0) {
                    reply_bad_request(req->clntFd,
                        "the 'text' field is empty",
                        "paste the configuration into the text area");
                    return;
                }
                if (vpn_import_text(text, text_len, hint, user, pass, name,
                        sizeof(name), error, sizeof(error))) {
                    reply_error(req->clntFd, "blocked", error,
                        "script hooks, missing keys and bad endpoints are "
                        "rejected");
                    return;
                }
            } else {
                reply_error(req->clntFd, "unsupported",
                    "the upload must be multipart/form-data or a form",
                    "send the configuration as a file or as text");
                return;
            }

            len = snprintf(body, sizeof(body),
                "{\"imported\":true,\"name\":\"%s\","
                "\"note\":\"connect the profile to start the tunnel\"}",
                name);
            HAL_INFO("server", "VPN profile '%s' imported\n", name);
            reply_json(req->clntFd, body, (size_t)len);
            return;
        }

        send_http_error(req->clntFd, 404);
        return;
    }

if (EQUALS(req->uri, "/api/storage")) {
        unsigned long long total = 0, freeBytes = 0;
        char action[16] = "";
        char formatFs[16] = "fat32";
        char confirm[16] = "";
        char requestToken[64] = "";
        char device[64] = "";
        char stateFs[16] = "";
        const char *error = NULL, *reason = NULL, *hint = NULL;
        bool formatted = false;
        bool supported = storage_format_supported();
        bool mounted;
        bool space;

        if (req->query) {
            while (req->query) {
                char *value = split(&req->query, "&");
                if (!value || !*value) continue;
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
                unescape_uri(value);
                if (EQUALS(key, "action"))
                    snprintf(action, sizeof(action), "%s", value);
                else if (EQUALS(key, "fs"))
                    snprintf(formatFs, sizeof(formatFs), "%s", value);
                else if (EQUALS(key, "confirm"))
                    snprintf(confirm, sizeof(confirm), "%s", value);
                else if (EQUALS(key, "token"))
                    snprintf(requestToken, sizeof(requestToken), "%s", value);
            }
        }

        if (action[0]) {
            if (EQUALS(action, "format")) {
                if (!supported) {
                    error = "blocked";
                    reason = "format tools are missing from the image";
                    hint = "rebuild rootfs with util-linux (sfdisk, wipefs)";
                } else if (!EQUALS(confirm, "FORMAT") ||
                    strcmp(requestToken, storage_token_get())) {
                    error = "blocked";
                    reason = "format confirmation or token is invalid";
                    hint = "reload the Storage section and try again";
                } else if (!EQUALS(formatFs, "fat32")) {
                    error = "blocked";
                    reason = "only FAT32 formatting is supported";
                    hint = "use fs=fat32";
                } else if (record_active()) {
                    error = "blocked";
                    reason = "stop recording first";
                    hint = "the microSD cannot be formatted while recording";
                } else {
                    int rc;

                    /* Close the start=1 race: record_open_locked() refuses to
                     * open a segment while storageBusy is set. */
                    storageBusy = 1;
                    if (record_active()) {
                        storageBusy = 0;
                        error = "blocked";
                        reason = "stop recording first";
                        hint = "the microSD cannot be formatted while recording";
                    } else {
                        rc = system("/usr/bin/oko-sdcard format fat32");
                        storageBusy = 0;
                        if (rc != 0) {
                            error = "failed";
                            reason = "microSD format failed";
                            hint = "check /tmp/oko-sdcard.log";
                        } else {
                            formatted = true;
                            storage_token_generate();
                        }
                    }
                }
            } else if (EQUALS(action, "mount")) {
                int rc = system("/usr/bin/oko-sdcard mount");
                if (rc != 0) {
                    error = "failed";
                    reason = "microSD mount failed";
                    hint = "insert a FAT32/exFAT/ext4 card and check /tmp/oko-sdcard.log";
                }
            } else if (EQUALS(action, "unmount")) {
                if (strcmp(requestToken, storage_token_get())) {
                    error = "blocked";
                    reason = "unmount token is invalid";
                    hint = "reload the Storage section and try again";
                } else if (record_active()) {
                    error = "blocked";
                    reason = "stop recording first";
                    hint = "the microSD cannot be unmounted while recording";
                } else {
                    int rc;

                    storageBusy = 1;
                    if (record_active()) {
                        storageBusy = 0;
                        error = "blocked";
                        reason = "stop recording first";
                        hint = "the microSD cannot be unmounted while recording";
                    } else {
                        rc = system("/usr/bin/oko-sdcard unmount");
                        storageBusy = 0;
                        if (rc != 0) {
                            error = "failed";
                            reason = "microSD unmount failed";
                            hint = "check /tmp/oko-sdcard.log";
                        }
                    }
                }
            } else {
                error = "blocked";
                reason = "unknown storage action";
                hint = "use format, mount or unmount";
            }
        }

        if (error) {
            char errorResponse[640];
            int errorLen = sprintf(errorResponse,
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json;charset=UTF-8\r\n"
                "Connection: close\r\n"
                "\r\n"
                "{\"error\":\"%s\",\"reason\":\"%s\",\"hint\":\"%s\"}",
                error, reason ? reason : "", hint ? hint : "");
            send_and_close(req->clntFd, errorResponse, errorLen);
            return;
        }

        storage_read_state(device, sizeof(device), stateFs, sizeof(stateFs));
        mounted = gating_sd_mounted();
        space = gating_sd_space(&total, &freeBytes);

        respLen = sprintf(response,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json;charset=UTF-8\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"formatted\":%s,\"action\":\"%s\","
            "\"sd_mounted\":%s,\"total_mb\":%llu,\"free_mb\":%llu,"
            "\"path\":\"%s\",\"max_bytes\":%llu,\"min_free_mb\":%d,"
            "\"recording\":%s,\"blocked\":%s,\"reason\":\"%s\","
            "\"device\":\"%s\",\"fs\":\"%s\","
            "\"format_supported\":%s,\"format_busy\":%s,"
            "\"format_token\":\"%s\"}",
            formatted ? "true" : "false", action,
            mounted ? "true" : "false",
            space ? total / (1024 * 1024) : 0,
            space ? freeBytes / (1024 * 1024) : 0,
            app_config.record_path, app_config.record_max_bytes,
            app_config.record_min_free_mb,
            record_active() ? "true" : "false",
            mounted ? "false" : "true",
            mounted ? "" : "recording requires a mounted microSD card",
            device, stateFs,
            supported ? "true" : "false",
            (record_active() || storageBusy) ? "true" : "false",
            storage_token_get());
        send_and_close(req->clntFd, response, respLen);
        return;
    }

    /* OKO: change the WebUI credentials.  Basic auth has no session, so the
     * browser picks up the new credentials on the next request. */
    if (EQUALS(req->uri, "/api/web-auth")) {
        char user[64] = "", passOld[64] = "", passNew[64] = "";

        if (!EQUALS(req->method, "POST")) {
            send_http_error(req->clntFd, 405);
            return;
        }
        if (req->query) {
            while (req->query) {
                char *value = split(&req->query, "&");
                if (!value || !*value) continue;
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
                unescape_uri(value);
                if (EQUALS(key, "user"))
                    snprintf(user, sizeof(user), "%s", value);
                else if (EQUALS(key, "pass_old"))
                    snprintf(passOld, sizeof(passOld), "%s", value);
                else if (EQUALS(key, "pass_new"))
                    snprintf(passNew, sizeof(passNew), "%s", value);
            }
        }

        if (!EQUALS(user, app_config.web_auth_user) ||
            !EQUALS(passOld, app_config.web_auth_pass)) {
            reply_error(req->clntFd, "forbidden",
                "the current user name or password is invalid",
                "reload the page and try again");
            return;
        }
        if (!*user || !*passNew) {
            reply_error(req->clntFd, "blocked",
                "the user name and the new password must not be empty", "");
            return;
        }
        if (strlen(app_config.web_auth_user) >= sizeof(app_config.web_auth_user) ||
            strlen(passNew) >= sizeof(app_config.web_auth_pass)) {
            reply_error(req->clntFd, "blocked",
                "the new credentials are too long",
                "the limit is 31 characters");
            return;
        }

        snprintf(app_config.web_auth_user, sizeof(app_config.web_auth_user), "%s", user);
        snprintf(app_config.web_auth_pass, sizeof(app_config.web_auth_pass), "%s", passNew);
        if (app_config_save()) {
            reply_error(req->clntFd, "failed",
                "the credentials were changed but could not be saved",
                "press Save in the System section and restart");
            return;
        }

        {
            char esc_user[sizeof(app_config.web_auth_user) * 6 + 1];
            char body[256];
            escape_json(esc_user, app_config.web_auth_user, sizeof(esc_user));
            int len = snprintf(body, sizeof(body),
                "{\"ok\":true,\"user\":\"%s\","
                "\"note\":\"reload the page and sign in with the new credentials\"}",
                esc_user);
            HAL_INFO("server", "WebUI credentials updated\n");
            reply_json(req->clntFd, body, len);
        }
        return;
    }

    /* OKO: recording archive of the microSD card. */
    if (EQUALS(req->uri, "/api/recordings")) {
        DIR *dir = opendir(app_config.record_path);
        rec_entry *items;
        int count = 0;
        unsigned long long total = 0;

        items = malloc(sizeof(*items) * REC_ENTRY_LIMIT);
        if (!dir || !items) {
            if (dir) closedir(dir);
            free(items);
            reply_error(req->clntFd, "unavailable",
                "the recording directory is not available",
                "insert and mount a microSD card");
            return;
        }

        {
            struct dirent *entry;
            while ((entry = readdir(dir))) {
                char path[RECORD_PATH_MAX + 256];
                struct stat st;
                const char *ext;

                if (entry->d_name[0] == '.') continue;
                ext = get_extension(entry->d_name);
                if (strcasecmp(ext, "mp4") && strcasecmp(ext, "jpg")) continue;

                snprintf(path, sizeof(path), "%s/%s",
                    app_config.record_path, entry->d_name);
                if (stat(path, &st) || !S_ISREG(st.st_mode)) continue;

                total += (unsigned long long)st.st_size;
                if (count >= REC_ENTRY_LIMIT) continue;
                snprintf(items[count].name, sizeof(items[count].name), "%s",
                    entry->d_name);
                items[count].size = (unsigned long long)st.st_size;
                items[count].mtime = st.st_mtime;
                count++;
            }
            closedir(dir);
        }

        qsort(items, count, sizeof(*items), rec_entry_cmp);

        {
            size_t cap = 512 + (size_t)count * 1700;
            char *body = malloc(cap);
            char esc_path[sizeof(app_config.record_path) * 6 + 1];
            size_t used;

            if (!body) {
                free(items);
                reply_error(req->clntFd, "failed", "out of memory", "");
                return;
            }
            escape_json(esc_path, app_config.record_path, sizeof(esc_path));
            used = (size_t)snprintf(body, cap,
                "{\"path\":\"%s\",\"count\":%d,\"total_bytes\":%llu,\"files\":[",
                esc_path, count, total);
            for (int i = 0; i < count && used < cap; i++) {
                char esc_name[sizeof(items[i].name) * 6 + 1];
                escape_json(esc_name, items[i].name, sizeof(esc_name));
                used += (size_t)snprintf(body + used, cap - used,
                    "%s{\"name\":\"%s\",\"size\":%llu,\"mtime\":%ld}",
                    i ? "," : "", esc_name, items[i].size,
                    (long)items[i].mtime);
            }
            if (used < cap)
                used += (size_t)snprintf(body + used, cap - used, "]}");
            reply_json(req->clntFd, body, used < cap ? used : cap - 1);
            free(body);
        }
        free(items);
        return;
    }

    if (EQUALS(req->uri, "/api/recordings/file") ||
        EQUALS(req->uri, "/api/recordings/delete")) {
        char name[96] = "";
        bool doRemove = EQUALS(req->uri, "/api/recordings/delete");

        if (req->query) {
            while (req->query) {
                char *value = split(&req->query, "&");
                if (!value || !*value) continue;
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
                unescape_uri(value);
                if (EQUALS(key, "name"))
                    snprintf(name, sizeof(name), "%s", value);
            }
        }

        if (!valid_basename(name)) {
            reply_error(req->clntFd, "blocked",
                "the file name is not valid",
                "use a name from the archive list");
            return;
        }

        {
            char path[RECORD_PATH_MAX + 128];
            char active[RECORD_PATH_MAX];
            struct stat st;

            snprintf(path, sizeof(path), "%s/%s", app_config.record_path, name);
            if (stat(path, &st) || !S_ISREG(st.st_mode)) {
                reply_error(req->clntFd, "notfound",
                    "the file does not exist", "refresh the archive list");
                return;
            }

            if (!doRemove) {
                send_stream_file(req->clntFd, path, mime_for_name(name),
                    name, true, 0);
                return;
            }

            if (!EQUALS(req->method, "POST")) {
                send_http_error(req->clntFd, 405);
                return;
            }
            record_active_path(active, sizeof(active));
            if (*active && EQUALS(active, path)) {
                reply_error(req->clntFd, "blocked",
                    "the segment is being written right now",
                    "stop the recording and try again");
                return;
            }
            if (unlink(path)) {
                reply_error(req->clntFd, "failed",
                    "the file could not be deleted", strerror(errno));
                return;
            }
            {
                char body[192];
                int len = snprintf(body, sizeof(body),
                    "{\"deleted\":true,\"name\":\"%s\"}", name);
                HAL_INFO("server", "Deleted recording '%s'\n", name);
                reply_json(req->clntFd, body, len);
            }
        }
        return;
    }

    /* OKO: telemetry written by the S21oko-ec companion daemon. */
    if (EQUALS(req->uri, "/api/telemetry")) {
        struct stat st;
        FILE *file = fopen("/tmp/oko-ec.json", "rb");
        time_t now = time(NULL);
        long age = -1;
        int len = 0;
        char *body = NULL;

        if (!file || fstat(fileno(file), &st) || !S_ISREG(st.st_mode)) {
            if (file) fclose(file);
            reply_error(req->clntFd, "unavailable",
                "no telemetry data is available",
                "the oko-ec daemon is not running");
            return;
        }

        age = (long)(now - st.st_mtime);
        if (age < 0) age = 0;
        if (age > 30 || st.st_size <= 0 || st.st_size > 65536) {
            fclose(file);
            {
                char body_stale[96];
                int stale_len = snprintf(body_stale, sizeof(body_stale),
                    "{\"available\":false,\"age\":%ld}", age);
                reply_json(req->clntFd, body_stale, stale_len);
            }
            return;
        }

        body = malloc((size_t)st.st_size + 128);
        if (!body) {
            fclose(file);
            reply_error(req->clntFd, "failed", "out of memory", "");
            return;
        }
        len = snprintf(body, 128, "{\"available\":true,\"age\":%ld,\"data\":", age);
        {
            int prefix = len;
            size_t n = fread(body + len, 1, (size_t)st.st_size, file);
            len += (int)n;
            fclose(file);
            while (len > prefix && (body[len - 1] == '\n' || body[len - 1] == '\r' ||
                    body[len - 1] == ' '))
                len--;
            if (len <= prefix || body[len - 1] != '}') {
                /* The daemon may be mid-write; treat it as stale rather than
                 * as an error so the next poll recovers by itself. */
                free(body);
                {
                    char stale[96];
                    int stale_len = snprintf(stale, sizeof(stale),
                        "{\"available\":false,\"age\":%ld}", age);
                    reply_json(req->clntFd, stale, stale_len);
                }
                return;
            }
        }
        body[len++] = '}';
        reply_json(req->clntFd, body, (size_t)len);
        free(body);
        return;
    }

    if (EQUALS(req->uri, "/api/status")) {
        struct sysinfo si;
        sysinfo(&si);
        char memory[16], uptime[48], ipaddr[16];
        unsigned long long sdTotal = 0, sdFree = 0;
        bool sdSpace;
        short free = (si.freeram + si.bufferram) / 1024 / 1024;
        short total = si.totalram / 1024 / 1024;
        sprintf(memory, "%d/%dMB", total - free, total);
        local_ipv4(ipaddr, sizeof(ipaddr));
        sdSpace = gating_sd_space(&sdTotal, &sdFree);
        if (si.uptime > 86400)
            sprintf(uptime, "%ld days, %ld:%02ld:%02ld", si.uptime / 86400, (si.uptime % 86400) / 3600, (si.uptime % 3600) / 60, si.uptime % 60);
        else if (si.uptime > 3600)
            sprintf(uptime, "%ld:%02ld:%02ld", si.uptime / 3600, (si.uptime % 3600) / 60, si.uptime % 60);
        else
            sprintf(uptime, "%ld:%02ld", si.uptime / 60, si.uptime % 60);
        respLen = sprintf(response,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json;charset=UTF-8\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"chip\":\"%s\",\"loadavg\":[%.2f,%.2f,%.2f],\"memory\":\"%s\","
            "\"sensor\":\"%s\",\"temp\":\"%.1f\u00B0C\",\"uptime\":\"%s\","
            "\"ip\":\"%s\",\"sd_free_mb\":%llu,\"sd_total_mb\":%llu,"
            "\"features\":{\"eptz\":%s,\"rotate\":%s,\"substream\":%s,"
            "\"aiisp\":%s,\"ivs\":%s,\"hdr\":%s,\"sd\":%s,"
            "\"yolo\":%s,\"vpn\":%s}}",
            chip, si.loads[0] / 65536.0, si.loads[1] / 65536.0, si.loads[2] / 65536.0,
            memory, sensor, hal_temperature_read(), uptime,
            ipaddr,
            sdSpace ? sdFree / (1024 * 1024) : 0,
            sdSpace ? sdTotal / (1024 * 1024) : 0,
            (halCapabilities & HAL_CAP_EPTZ) ? "true" : "false",
            (halCapabilities & HAL_CAP_ROTATE) ? "true" : "false",
            app_config.substream_enable ? "true" : "false",
            (halCapabilities & HAL_CAP_AIISP) ? "true" : "false",
            (halCapabilities & HAL_CAP_IVS) ? "true" : "false",
            app_config.hdr_mode ? "true" : "false",
            gating_sd_mounted() ? "true" : "false",
            (halCapabilities & HAL_CAP_RKNN) ? "true" : "false",
            vpn_available() ? "true" : "false");
        send_and_close(req->clntFd, response, respLen);
        return;
    }

    if (EQUALS(req->uri, "/api/time")) {
        struct timespec t;
        int ntpChanged = 0, ntpFailed = 0, ntpInvalid = 0;
        int tzChanged = 0, tzInvalid = 0;
        char tzNew[sizeof(app_config.tz)] = "";
        if (req->query) {
            char *remain;
            while (req->query) {
                char *value = split(&req->query, "&");
                if (!value) continue;
                char *key = split(&value, "=");
                if (!key || !*key || !value) continue;
                unescape_uri(value);
                if (EQUALS(key, "fmt")) {
                    if (!*value) continue;
                    strncpy(timefmt, value, sizeof(timefmt) - 1);
                    timefmt[sizeof(timefmt) - 1] = '\0';
                } else if (EQUALS(key, "ts")) {
                    /* OKO: epoch seconds do not fit in short; the old code
                     * truncated them and set a random time. */
                    long result = strtol(value, &remain, 10);
                    if (remain == value || result < 1000000) continue;
                    t.tv_sec = (time_t)result;
                    t.tv_nsec = 0;
                    clock_settime(CLOCK_REALTIME, &t);
                } else if (EQUALS(key, "ntp")) {
                    if (!*value) continue;
                    app_config.ntp_enable = query_bool(value);
                    ntpChanged = 1;
                } else if (EQUALS(key, "ntp_server")) {
                    /* OKO: an empty value clears the custom server (and the
                     * watcher falls back to /etc/ntp.conf). */
                    if (!*value) {
                        app_config.ntp_server[0] = '\0';
                        ntpChanged = 1;
                    } else if (valid_hostname(value)) {
                        strncpy(app_config.ntp_server, value,
                            sizeof(app_config.ntp_server) - 1);
                        app_config.ntp_server[sizeof(app_config.ntp_server) - 1] = '\0';
                        ntpChanged = 1;
                    } else {
                        ntpInvalid = 1;
                    }
                } else if (EQUALS(key, "tz")) {
                    /* OKO: /etc/TZ is a link into the installed uClibc set;
                     * an unknown or unsafe name is refused, not ignored. */
                    if (!valid_tz(value)) {
                        tzInvalid = 1;
                    } else if (!EQUALS(value, app_config.tz)) {
                        snprintf(tzNew, sizeof(tzNew), "%s", value);
                        tzChanged = 1;
                    }
                }
            }

            if (ntpInvalid) {
                reply_error(req->clntFd, "blocked",
                    "the NTP server name is not valid",
                    "use a host name such as pool.ntp.org or an IP address");
                return;
            }

            if (tzInvalid) {
                reply_error(req->clntFd, "blocked",
                    "the time zone is not valid",
                    "use an IANA name such as Europe/Moscow");
                return;
            }

            if (ntpChanged) {
                if (!ntp_tool_available())
                    ntpFailed = 1;
                else if (ntp_apply())
                    ntpFailed = 1;
            }

            if (tzChanged) {
                if (!set_tz_link(tzNew)) {
                    reply_error(req->clntFd, "unavailable",
                        "the time zone could not be applied",
                        "check that /etc is writable and zoneinfo is installed");
                    return;
                }
                snprintf(app_config.tz, sizeof(app_config.tz), "%s", tzNew);
                /* uClibc caches the zone per process; the restart below is
                 * what actually makes the daemon use the new one. */
                tzset();
            }
        }

        if (ntpFailed) {
            reply_error(req->clntFd, "unavailable",
                "ntpd could not be restarted with the requested server",
                "check /tmp/divinus-ntp.conf and the system log");
            return;
        }

        clock_gettime(CLOCK_REALTIME, &t);
        {
            char esc_server[sizeof(app_config.ntp_server) * 6 + 1];
            char esc_tz[sizeof(app_config.tz) * 6 + 1];
            char cur_tz[128], esc_cur_tz[sizeof(cur_tz) * 6 + 1];
            current_tz(cur_tz, sizeof(cur_tz));
            escape_json(esc_server, app_config.ntp_server, sizeof(esc_server));
            escape_json(esc_tz, app_config.tz, sizeof(esc_tz));
            escape_json(esc_cur_tz, cur_tz, sizeof(esc_cur_tz));
            respLen = sprintf(response,
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json;charset=UTF-8\r\n"
                "Connection: close\r\n"
                "\r\n"
                "{\"fmt\":\"%s\",\"ts\":%ld,\"ntp_enabled\":%s,"
                "\"ntp_server\":\"%s\",\"ntp_running\":%s,"
                "\"tz\":\"%s\",\"tz_current\":\"%s\",\"note\":\"%s\"}",
                timefmt, (long)t.tv_sec,
                app_config.ntp_enable ? "true" : "false",
                esc_server, ntp_running() ? "true" : "false",
                esc_tz, esc_cur_tz,
                tzChanged ?
                    "time zone applied; configuration saved, restarting the application" : "");
            send_and_close(req->clntFd, response, respLen);
        }
        if (tzChanged) {
            /* Persist the zone before the graceful restart re-reads the yaml;
             * the restart is also what makes uClibc pick up /etc/TZ. */
            app_config_save();
            kill(getpid(), SIGHUP);
        }
        return;
    }

    if (app_config.web_enable_static && send_file(req->clntFd, req->uri))
        return;

    send_http_error(req->clntFd, 400);
}

void *server_thread(void *vargp) {
    int ret, server_fd = *((int *)vargp);
    int enable = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        HAL_WARNING("server", "setsockopt(SO_REUSEADDR) failed");
        fflush(stdout);
    }

    fcntl(server_fd, F_SETFL, fcntl(server_fd, F_GETFL, 0) | O_NONBLOCK);

    struct sockaddr_in client, server = {
        .sin_family = AF_INET,
        .sin_port = htons(app_config.web_port),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };
    if (ret = bind(server_fd, (struct sockaddr *)&server, sizeof(server))) {
        HAL_DANGER("server", "%s (%d)\n", strerror(errno), errno);
        keepRunning = 0;
        close_socket_fd(server_fd);
        return NULL;
    }
    listen(server_fd, 128);

    struct pollfd fds[HTTP_MAX_CLIENTS + 1];
    http_request_t reqs[HTTP_MAX_CLIENTS];

    for (int i = 0; i < HTTP_MAX_CLIENTS; i++) {
        fds[i + 1].fd = -1;
        reqs[i].clntFd = -1;
        reqs[i].input = NULL;
    }

    fds[0].fd = server_fd;
    fds[0].events = POLLIN;

    while (keepRunning) {
        int poll_count = poll(fds, HTTP_MAX_CLIENTS + 1, 1000);
        if (poll_count < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (fds[0].revents & POLLIN) {
            struct sockaddr_in client_sock;
            socklen_t client_sock_len = sizeof(client_sock);
            int clntFd = accept(server_fd, (struct sockaddr *)&client_sock, &client_sock_len);
            if (clntFd >= 0) {
                fcntl(clntFd, F_SETFL, fcntl(clntFd, F_GETFL, 0) | O_NONBLOCK);
                int i = 0;
                for (i = 0; i < HTTP_MAX_CLIENTS; i++) {
                    if (fds[i+1].fd == -1) {
                        fds[i+1].fd = clntFd;
                        fds[i+1].events = POLLIN;
                        reqs[i].clntFd = clntFd;
                        reqs[i].total = 0;
                        reqs[i].buf_size = HTTP_MIN_BUF_SIZE;
                        reqs[i].input = malloc(reqs[i].buf_size + 1);
                        reqs[i].header_len = 0;
                        reqs[i].paysize = 0;
                        reqs[i].prepared = 0;
                        reqs[i].uploading = 0;
                        reqs[i].upload_total = 0;
                        reqs[i].upload_received = 0;
                        reqs[i].last_activity = 0;
                        break;
                    }
                }
                if (i == HTTP_MAX_CLIENTS) {
                    close_socket_fd(clntFd);
                }
            }
        }

        for (int i = 0; i < HTTP_MAX_CLIENTS; i++) {
            if (fds[i + 1].fd != -1) {
                if (fds[i + 1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                    if (reqs[i].uploading)
                        update_upload_abort("the upload connection was lost");
                    close_socket_fd(fds[i + 1].fd);
                    fds[i + 1].fd = -1;
                    free(reqs[i].input);
                    reqs[i].input = NULL;
                    continue;
                }
                if (fds[i + 1].revents & POLLIN) {
                    http_request_t *req = &reqs[i];

                    /* Streamed update upload: the body never enters req->input,
                     * so the 4 MiB request limit does not apply. */
                    if (req->uploading) {
                        char chunk[16384];
                        long long remaining =
                            req->upload_total - req->upload_received;
                        int want = remaining < (long long)sizeof(chunk)
                            ? (int)remaining : (int)sizeof(chunk);
                        int received;

                        if (want <= 0) {
                            finish_update_upload(req);
                            fds[i + 1].fd = -1;
                            free(req->input);
                            req->input = NULL;
                            continue;
                        }
                        received = recv(req->clntFd, chunk, want, 0);
                        if (received < 0) {
                            if (errno == EAGAIN || errno == EWOULDBLOCK ||
                                errno == EINTR)
                                continue;
                            update_upload_abort(
                                "the upload connection was lost");
                            close_socket_fd(req->clntFd);
                            fds[i + 1].fd = -1;
                            free(req->input);
                            req->input = NULL;
                            continue;
                        } else if (received == 0) {
                            update_upload_abort("the upload was interrupted");
                            close_socket_fd(req->clntFd);
                            fds[i + 1].fd = -1;
                            free(req->input);
                            req->input = NULL;
                            continue;
                        }
                        req->last_activity = time(NULL);
                        if (update_upload_write(chunk, received) < 0) {
                            update_upload_abort(
                                "cannot write to the microSD card");
                            close_socket_fd(req->clntFd);
                            fds[i + 1].fd = -1;
                            free(req->input);
                            req->input = NULL;
                            continue;
                        }
                        req->upload_received += received;
                        if (update_upload_cancelled()) {
                            update_upload_abort("cancelled by the operator");
                            close_socket_fd(req->clntFd);
                            fds[i + 1].fd = -1;
                            free(req->input);
                            req->input = NULL;
                            continue;
                        }
                        if (req->upload_received >= req->upload_total) {
                            finish_update_upload(req);
                            fds[i + 1].fd = -1;
                            free(req->input);
                            req->input = NULL;
                        }
                        continue;
                    }

                    if (req->total == req->buf_size) {
                        req->buf_size *= 2;
                        if (req->buf_size > HTTP_MAX_BUF_SIZE) {
                            close_socket_fd(req->clntFd);
                            fds[i + 1].fd = -1;
                            free(req->input);
                            req->input = NULL;
                            continue;
                        }
                        req->input = realloc(req->input, req->buf_size + 1);
                    }

                    int received = recv(req->clntFd, req->input + req->total, req->buf_size - req->total, 0);
                    if (received < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                            continue;
                        close_socket_fd(req->clntFd);
                        fds[i + 1].fd = -1;
                        free(req->input);
                        req->input = NULL;
                        continue;
                    } else if (received == 0) {
                        close_socket_fd(req->clntFd);
                        fds[i + 1].fd = -1;
                        free(req->input);
                        req->input = NULL;
                        continue;
                    }
                    req->total += received;

                    if (req->header_len == 0) {
                        char *hdr_end = memstr(req->input, "\r\n\r\n", req->total, 4);
                        if (hdr_end) {
                            req->header_len = (hdr_end + 4) - req->input;

                            char *cl = req->input;
                            req->paysize = 0;
                            while (cl < hdr_end) {
                                if (!strncasecmp(cl, "Content-Length:", 15)) {
                                    req->paysize = atoi(cl + 15);
                                    break;
                                }
                                cl = strchr(cl, '\n');
                                if (!cl) break;
                                cl++;
                            }
                        }
                    }

                    if (!req->prepared && req->header_len > 0) {
                        req->prepared = 1;
                        req->last_activity = time(NULL);
                        if (is_update_upload_request(req->input,
                                req->header_len)) {
                            if (begin_update_upload(req) != 0) {
                                fds[i + 1].fd = -1;
                                free(req->input);
                                req->input = NULL;
                            }
                            continue;
                        }
                    }

                    if (!req->uploading && req->header_len > 0 &&
                        req->total >= req->header_len + req->paysize) {
                        parse_request(req);
                        if (req->clntFd != -1) {
                            fcntl(req->clntFd, F_SETFL, fcntl(req->clntFd, F_GETFL, 0) & ~O_NONBLOCK);
                            {
                                /* A client that stops reading a stream (e.g. a browser that gave
                                 * up on the MP4) must not block the whole server on send() */
                                struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
                                setsockopt(req->clntFd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
                            }
                            respond_request(req);
                        }
                        fds[i + 1].fd = -1;
                        free(req->input);
                        req->input = NULL;
                    }
                }

                if (fds[i + 1].fd != -1 && reqs[i].uploading &&
                    (update_upload_cancelled() ||
                     (reqs[i].last_activity &&
                      time(NULL) - reqs[i].last_activity > 120))) {
                    update_upload_abort(update_upload_cancelled()
                        ? "cancelled by the operator" : "the upload timed out");
                    close_socket_fd(fds[i + 1].fd);
                    fds[i + 1].fd = -1;
                    free(reqs[i].input);
                    reqs[i].input = NULL;
                }
            }
        }
    }

    for (int i = 0; i < HTTP_MAX_CLIENTS; i++) {
        if (fds[i + 1].fd != -1) {
            close_socket_fd(fds[i + 1].fd);
            free(reqs[i].input);
        }
    }

    close_socket_fd(server_fd);
    HAL_INFO("server", "Thread has exited\n");
    return NULL;
}

int server_start() {
    flash_init();
    update_init();
    focus_home_init();
    focus_af_init();
    for (int i = 0; i < HTTP_MAX_CLIENTS; i++) {
        client_fds[i].sockFd = -1;
        client_fds[i].type = -1;
    }
    pthread_mutex_init(&client_fds_mutex, NULL);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    {
        pthread_attr_t thread_attr;
        pthread_attr_init(&thread_attr);
        size_t stacksize;
        pthread_attr_getstacksize(&thread_attr, &stacksize);
        size_t new_stacksize = app_config.web_server_thread_stack_size;
        if (pthread_attr_setstacksize(&thread_attr, new_stacksize))
            HAL_WARNING("server", "Can't set stack size %zu\n", new_stacksize);
        if (pthread_create(
            &server_thread_id, &thread_attr, server_thread, (void *)&server_fd))
            HAL_ERROR("server", "Starting the server thread failed!\n");
        if (pthread_attr_setstacksize(&thread_attr, stacksize))
            HAL_DANGER("server", "Can't set stack size %zu\n", stacksize);
        pthread_attr_destroy(&thread_attr);
    }

    return EXIT_SUCCESS;
}

int server_stop() {
    keepRunning = 0;
    focus_af_shutdown();
    flash_shutdown();
    focus_home_shutdown();
    update_shutdown();

    close_socket_fd(server_fd);
    pthread_join(server_thread_id, NULL);

    pthread_mutex_destroy(&client_fds_mutex);
    HAL_INFO("server", "Shutting down server...\n");

    return EXIT_SUCCESS;
}
