#include "http_post.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>

pthread_t httpPostPid = 0;

/* Sends the whole buffer, retrying short writes. */
static int http_post_write(int fd, const void *data, size_t length)
{
    const char *cursor = data;

    while (length) {
        ssize_t written = write(fd, cursor, length);
        if (written < 0) {
            if (errno == EINTR) continue;
            return EXIT_FAILURE;
        }
        cursor += written;
        length -= (size_t)written;
    }
    return EXIT_SUCCESS;
}

static int http_post_connect(const char *host, int timeoutMs)
{
    struct addrinfo hints, *server = NULL, *addr;
    char address[128];
    char port[8] = "80";
    char *colon;
    int fd = -1;

    /* Accept "host" and "host:port"; the bench receiver uses a high port
     * because the default one may already be taken on the server. */
    snprintf(address, sizeof(address), "%s", host);
    colon = strrchr(address, ':');
    if (colon && colon[1] && !strchr(colon + 1, ':')) {
        *colon = 0;
        snprintf(port, sizeof(port), "%s", colon + 1);
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(address, port, &hints, &server))
        return -1;

    for (addr = server; addr; addr = addr->ai_next) {
        int flags;
        fd_set writeFds;
        struct timeval tv = { .tv_sec = timeoutMs / 1000,
            .tv_usec = (timeoutMs % 1000) * 1000 };

        fd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
        if (fd < 0) continue;

        flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        if (connect(fd, addr->ai_addr, addr->ai_addrlen) == 0) {
            fcntl(fd, F_SETFL, flags);
            break;
        }
        if (errno == EINPROGRESS) {
            int error = 0;
            socklen_t len = sizeof(error);
            FD_ZERO(&writeFds);
            FD_SET(fd, &writeFds);
            if (select(fd + 1, NULL, &writeFds, NULL, &tv) > 0 &&
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) == 0 &&
                error == 0) {
                fcntl(fd, F_SETFL, flags);
                break;
            }
        }
        close(fd);
        fd = -1;
    }

    freeaddrinfo(server);
    return fd;
}

/* One PUT with the standard fields and an optional extra header block. */
static int http_post_put(hal_jpegdata *jpeg, const char *extra)
{
    char *host = app_config.http_post_host;
    struct timeval timeout = { .tv_sec = 3, .tv_usec = 0 };
    int fd, ret = EXIT_FAILURE;
    char request[2048];
    int length;
    char url[256];
    time_t timer = time(NULL);
    struct tm tm_buf, *tm_info = localtime_r(&timer, &tm_buf);

    if (!host || !*host || !jpeg || !jpeg->data || !jpeg->jpegSize)
        return EXIT_FAILURE;

    fd = http_post_connect(host, 3000);
    if (fd < 0) {
        HAL_WARNING("http_post", "Connection to %s failed\n", host);
        return EXIT_FAILURE;
    }

    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    url[0] = 0;
    {
        size_t used = strftime(url, sizeof(url), app_config.http_post_url,
            tm_info);
        if (used >= sizeof(url)) url[0] = 0;
    }
    if (!url[0])
        snprintf(url, sizeof(url), "%s", app_config.http_post_url);

    length = snprintf(request, sizeof(request),
        "PUT %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: Camera openipc.org\r\n"
        "Accept: */*\r\n"
        "Content-Type: image/jpeg\r\n"
        "Content-Length: %u\r\n",
        url, host, jpeg->jpegSize);

    if (length < 0 || length >= (int)sizeof(request) - 1) {
        close(fd);
        return EXIT_FAILURE;
    }

    if (strlen(app_config.http_post_login) > 0 &&
        strlen(app_config.http_post_password) > 0) {
        char log_pass[128];
        char base64buf[1024];
        int log_pass_len = snprintf(log_pass, sizeof(log_pass), "%s:%s",
            app_config.http_post_login, app_config.http_post_password);
        int base64_len = base64_encode(base64buf, log_pass, log_pass_len);
        if (base64_len > 0 && base64_len < (int)sizeof(base64buf)) {
            base64buf[base64_len] = 0;
            length += snprintf(request + length, sizeof(request) - length,
                "Authorization: Basic %s\r\n", base64buf);
        }
    }

    if (length < 0 || length >= (int)sizeof(request) - 1) {
        close(fd);
        return EXIT_FAILURE;
    }

    if (extra && *extra) {
        int left = (int)sizeof(request) - length;
        length += snprintf(request + length, (size_t)left, "%s", extra);
    }

    if (length >= (int)sizeof(request) - 3) {
        close(fd);
        return EXIT_FAILURE;
    }
    request[length++] = '\r';
    request[length++] = '\n';

    if (!http_post_write(fd, request, (size_t)length) &&
        !http_post_write(fd, jpeg->data, jpeg->jpegSize)) {
        char reply[256];
        /* The status line is not parsed: a request may be answered with a
         * body-less 204, and the receiver is the source of truth for errors.
         * Reading keeps the close orderly and bounds the wait with SO_RCVTIMEO. */
        (void)read(fd, reply, sizeof(reply));
        ret = EXIT_SUCCESS;
    } else {
        HAL_WARNING("http_post", "Sending to %s failed\n", host);
    }

    close(fd);
    return ret;
}

int http_post_send(hal_jpegdata *jpeg) {
    return http_post_put(jpeg, NULL);
}

/* One frame of an event package.  The receiver stitches the frames together
 * by X-Event-Id and orders them by X-Frame-Index. */
int http_post_event(hal_jpegdata *jpeg, const char *eventId, int index,
    int total, unsigned long long timestampMs, const char *cause,
    const char *model)
{
    char extra[512];
    char safeModel[96];
    size_t used = 0;

    if (!eventId || !cause || !model) return EXIT_FAILURE;

    for (const char *p = model; *p && used < sizeof(safeModel) - 1; p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '.' || *p == '_' || *p == '-')
            safeModel[used++] = *p;
    }
    safeModel[used] = 0;

    snprintf(extra, sizeof(extra),
        "X-Event-Id: %s\r\n"
        "X-Frame-Index: %d\r\n"
        "X-Frame-Total: %d\r\n"
        "X-Frame-Ts: %llu\r\n"
        "X-Event-Cause: %s\r\n"
        "X-Model: %s\r\n",
        eventId, index, total, timestampMs, cause, safeModel);

    return http_post_put(jpeg, extra);
}

void *http_post_thread(void) {
    hal_jpegdata jpeg = {0};
    jpeg.data = NULL;
    jpeg.length = 0;
    jpeg.jpegSize = 0;
    sleep(3);

    while (keepRunning) {
        static time_t last_time = 0;
        time_t current_time = time(NULL);
        if (current_time - last_time < app_config.http_post_interval) {
            sleep(1);
            continue;
        }

        if (jpeg_get(app_config.http_post_width, app_config.http_post_height,
                app_config.http_post_qfactor, 3, &jpeg)) {
            HAL_WARNING("http_post", "Grabbing the JPEG image has failed!\n");
            continue;
        }
        last_time = current_time;

        if (http_post_send(&jpeg)) {
            HAL_WARNING("http_post", "Sending the picture has failed!\n");
            continue;
        }
    }

    if (jpeg.data) {
        free(jpeg.data);
        jpeg.data = NULL;
    }

    return NULL;
}

void http_post_start() {
    pthread_attr_t thread_attr;
    pthread_attr_init(&thread_attr);
    size_t stacksize;
    pthread_attr_getstacksize(&thread_attr, &stacksize);
    size_t new_stacksize = 16 * 1024;
    if (pthread_attr_setstacksize(&thread_attr, new_stacksize))
        HAL_DANGER("http_post", "Can't set stack size %zu\n", new_stacksize);
    if (pthread_create(
            &httpPostPid, &thread_attr, (void *(*)(void *))http_post_thread, NULL))
        HAL_DANGER("http_post", "Starting the sender thread failed!\n");
    if (pthread_attr_setstacksize(&thread_attr, stacksize))
        HAL_DANGER("http_post", "Can't set stack size %zu\n", stacksize);
    pthread_attr_destroy(&thread_attr);
}

void http_post_stop() {
    pthread_join(httpPostPid, NULL);
}