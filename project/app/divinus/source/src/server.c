#include "server.h"

#include <dirent.h>

#include "gating.h"
#include "ivs_events.h"

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
    if (!access(path, F_OK)) {
        const char *mime = (path);
        FILE *file = fopen(path, "r");
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

void parse_request(http_request_t *req) {
    struct sockaddr_in client_sock;
    socklen_t client_sock_len = sizeof(client_sock);
    memset(&client_sock, 0, client_sock_len);

    getpeername(req->clntFd,
        (struct sockaddr *)&client_sock, &client_sock_len);
    char *client_ip = inet_ntoa(client_sock.sin_addr);

    if (!EMPTY(*app_config.web_whitelist)) {
        for (int i = 0; app_config.web_whitelist[i] && *app_config.web_whitelist[i]; i++)
            if (ip_in_cidr(client_ip, app_config.web_whitelist[i])) goto grant_access;
        close_socket_fd(req->clntFd);
        req->clntFd = -1;
        return;
    }

grant_access:
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
            char cred[66], valid[256];

            strcpy(cred, app_config.web_auth_user);
            strcpy(cred + strlen(app_config.web_auth_user), ":");
            strcpy(cred + strlen(app_config.web_auth_user) + 1, app_config.web_auth_pass);
            strcpy(valid, "Basic ");
            base64_encode(valid + 6, cred, strlen(cred));

            if (!auth || !EQUALS(auth, valid)) {
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
                unescape_uri(value);
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
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
        int result = -1;
        if (req->query) {
            char *remain;
            while (req->query) {
                char *value = split(&req->query, "&");
                if (!value || !*value) continue;
                unescape_uri(value);
                char *key = split(&value, "=");
                if (!key || !*key) continue;
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
                }
            }
        }

        respLen = sprintf(response,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json;charset=UTF-8\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"code\":%d}", result);
        send_and_close(req->clntFd, response, respLen);
        return;
    }

    if (EQUALS(req->uri, "/api/isp")) {
        if (req->query) {
            char *remain;
            while (req->query) {
                char *value = split(&req->query, "&");
                if (!value || !*value) continue;
                unescape_uri(value);
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
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
                unescape_uri(value);
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
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
                unescape_uri(value);
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
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
                unescape_uri(value);
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
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
                unescape_uri(value);
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
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
                unescape_uri(value);
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
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
                unescape_uri(value);
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
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
            if (type && STARTS_WITH(type, "multipart/form-data")) {
                char *bound = strstr(type, "boundary=") + strlen("boundary=");

                char *payloadb = strstr(req->payload, bound);
                payloadb = memstr(payloadb, "\r\n\r\n", req->total - (payloadb - req->input), 4);
                if (payloadb) payloadb += 4;

                char *payloade = memstr(payloadb, bound,
                    req->total - (payloadb - req->input), strlen(bound));
                if (payloade) payloade -= 4;

                char path[32];

                if (!memcmp(payloadb, "\x89\x50\x4E\x47\xD\xA\x1A\xA", 8))
                    sprintf(path, "/tmp/osd%d.png", id);
                else
                    sprintf(path, "/tmp/osd%d.bmp", id);

                FILE *img = fopen(path, "wb");
                fwrite(payloadb, sizeof(char), payloade - payloadb, img);
                fclose(img);

                strcpy(osds[id].text, "");
                osds[id].updt = 1;
            } else {
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
                if (!value || !*value) continue;
                unescape_uri(value);
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
                if (EQUALS(key, "img"))
                    strncpy(osds[id].img, value,
                        sizeof(osds[id].img) - 1);
                else if (EQUALS(key, "font"))
                    strncpy(osds[id].font, !EMPTY(value) ? value : DEF_FONT,
                        sizeof(osds[id].font) - 1);
                else if (EQUALS(key, "text"))
                    strncpy(osds[id].text, value,
                        sizeof(osds[id].text) - 1);
                else if (EQUALS(key, "size")) {
                    double result = strtod(value, &remain);
                    if (remain == value) continue;
                    osds[id].size = (result != 0 ? result : DEF_SIZE);
                }
                else if (EQUALS(key, "color")) {
                    int result = color_parse(value);
                    osds[id].color = result;
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
                    osds[id].outl = result;
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
        respLen = sprintf(response,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json;charset=UTF-8\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"id\":%d,\"color\":\"#%x\",\"opal\":%d,\"pos\":[%d,%d],"
            "\"font\":\"%s\",\"size\":%.1f,\"text\":\"%s\",\"img\":\"%s\","
            "\"outl\":\"#%x\",\"thick\":%.1f}",
            id, color, osds[id].opal, osds[id].posx, osds[id].posy,
            osds[id].font, osds[id].size, osds[id].text, osds[id].img,
            osds[id].outl, osds[id].thick);
        send_and_close(req->clntFd, response, respLen);
        return;
    }

    if (EQUALS(req->uri, "/api/record")) {
        if (req->query) {
            char *remain;
            while (req->query) {
                char *value = split(&req->query, "&");
                if (!value || !*value) continue;
                unescape_uri(value);
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
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
        if (req->query) {
            int enableReq = -1;
            while (req->query) {
                char *value = split(&req->query, "&");
                if (!value || !*value) continue;
                unescape_uri(value);
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
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

            if (media_apply_eptz())
                HAL_WARNING("server", "ePTZ could not be applied\n");
        }

        respLen = sprintf(response,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json;charset=UTF-8\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"enable\":%s,\"stream\":\"%s\",\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d}",
            app_config.eptz_enable ? "true" : "false",
            app_config.eptz_stream ? "sub" : "main",
            app_config.eptz_x, app_config.eptz_y,
            app_config.eptz_w, app_config.eptz_h);
        send_and_close(req->clntFd, response, respLen);
        return;
    }

    if (EQUALS(req->uri, "/api/rotate")) {
        if (req->query) {
            int enableReq = -1;
            while (req->query) {
                char *value = split(&req->query, "&");
                if (!value || !*value) continue;
                unescape_uri(value);
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
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

            if (media_apply_rotation())
                HAL_WARNING("server", "Rotation could not be applied\n");
        }

        respLen = sprintf(response,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json;charset=UTF-8\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"enable\":%s,\"stream\":\"%s\",\"angle\":%d}",
            app_config.rotate_enable ? "true" : "false",
            app_config.rotate_stream ? "sub" : "main",
            app_config.rotate_angle);
        send_and_close(req->clntFd, response, respLen);
        return;
    }

    if (EQUALS(req->uri, "/api/venc")) {
        if (req->query) {
            int wantsExtra = 0;
            while (req->query) {
                char *value = split(&req->query, "&");
                if (!value || !*value) continue;
                unescape_uri(value);
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
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
                unescape_uri(value);
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
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
                    long v; if (parse_ranged(value, 1, 60, &v)) { app_config.substream_fps = (unsigned int)v; paramsChanged = 1; }
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
                unescape_uri(value);
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
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

    if (EQUALS(req->uri, "/api/ivs")) {
        if (req->query) {
            int enableReq = -1;
            while (req->query) {
                char *value = split(&req->query, "&");
                if (!value || !*value) continue;
                unescape_uri(value);
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
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
                unescape_uri(value);
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
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
                unescape_uri(value);
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
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
                unescape_uri(value);
                char *key = split(&value, "=");
                if (!key || !*key || !value) continue;
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
                unescape_uri(value);
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
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

    if (EQUALS(req->uri, "/api/status")) {
        struct sysinfo si;
        sysinfo(&si);
        char memory[16], uptime[48];
        short free = (si.freeram + si.bufferram) / 1024 / 1024;
        short total = si.totalram / 1024 / 1024;
        sprintf(memory, "%d/%dMB", total - free, total);
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
            "\"features\":{\"eptz\":%s,\"rotate\":%s,\"substream\":%s,"
            "\"aiisp\":%s,\"ivs\":%s,\"hdr\":%s,\"sd\":%s}}",
            chip, si.loads[0] / 65536.0, si.loads[1] / 65536.0, si.loads[2] / 65536.0,
            memory, sensor, hal_temperature_read(), uptime,
            (halCapabilities & HAL_CAP_EPTZ) ? "true" : "false",
            (halCapabilities & HAL_CAP_ROTATE) ? "true" : "false",
            app_config.substream_enable ? "true" : "false",
            (halCapabilities & HAL_CAP_AIISP) ? "true" : "false",
            (halCapabilities & HAL_CAP_IVS) ? "true" : "false",
            app_config.hdr_mode ? "true" : "false",
            gating_sd_mounted() ? "true" : "false");
        send_and_close(req->clntFd, response, respLen);
        return;
    }

    if (EQUALS(req->uri, "/api/time")) {
        struct timespec t;
        if (req->query) {
            char *remain;
            while (req->query) {
                char *value = split(&req->query, "&");
                if (!value || !*value) continue;
                unescape_uri(value);
                char *key = split(&value, "=");
                if (!key || !*key || !value || !*value) continue;
                if (EQUALS(key, "fmt")) {
                    strncpy(timefmt, value, 32);
                } else if (EQUALS(key, "ts")) {
                    short result = strtol(value, &remain, 10);
                    if (remain == value) continue;
                    t.tv_sec = result;
                    clock_settime(CLOCK_REALTIME, &t);
                }
            }
        }
        clock_gettime(CLOCK_REALTIME, &t);
        respLen = sprintf(response,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json;charset=UTF-8\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"fmt\":\"%s\",\"ts\":%zu}", timefmt, t.tv_sec);
        send_and_close(req->clntFd, response, respLen);
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
                    close_socket_fd(fds[i + 1].fd);
                    fds[i + 1].fd = -1;
                    free(reqs[i].input);
                    reqs[i].input = NULL;
                    continue;
                }
                if (fds[i + 1].revents & POLLIN) {
                    http_request_t *req = &reqs[i];

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

                    if (req->header_len > 0 && req->total >= req->header_len + req->paysize) {
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

    close_socket_fd(server_fd);
    pthread_join(server_thread_id, NULL);

    pthread_mutex_destroy(&client_fds_mutex);
    HAL_INFO("server", "Shutting down server...\n");

    return EXIT_SUCCESS;
}
