#define _GNU_SOURCE

#include "flash.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "app_config.h"
#include "firmware_status.h"
#include "focus_home.h"
#include "focus_motor.h"

#define FLASH_LOG_LINES  20
#define FLASH_LINE_MAX   160

/* The installed STM32 is an STM32F103CBT6: 128 KiB total, 8 KiB boot,
 * 2 KiB metadata.  The loader accepts a 60 KiB test image, which confirms the
 * 128 KiB layout; a C8T6 build would reject it.  Reject larger images before
 * the job starts, because the bootloader reports ERR_PARAM only after the EC
 * has already switched to update mode. */
#define FLASH_STM32_APP_MAX (120u * 1024u)

typedef struct {
    char id[32];
    char target[8];
    char state[16];
    int progress;
    char message[128];
    char file[96];
    char sha256[65];
    long started;
    long finished;
    char log[FLASH_LOG_LINES][FLASH_LINE_MAX];
    int log_count;
} flash_job;

static pthread_mutex_t flash_lock = PTHREAD_MUTEX_INITIALIZER;
static flash_job job;
static char pending_file[PATH_MAX];
static char pending_version[16];
static int busy;
static int thread_valid;
static pthread_t worker_thread;

static void set_state(const char *state, const char *message)
{
    pthread_mutex_lock(&flash_lock);
    snprintf(job.state, sizeof(job.state), "%s", state);
    if (message)
        snprintf(job.message, sizeof(job.message), "%s", message);
    pthread_mutex_unlock(&flash_lock);
}

static void append_log(const char *line)
{
    int i;

    pthread_mutex_lock(&flash_lock);
    if (job.log_count < FLASH_LOG_LINES)
    {
        snprintf(job.log[job.log_count], FLASH_LINE_MAX, "%s", line);
        job.log_count++;
    }
    else
    {
        for (i = 1; i < FLASH_LOG_LINES; i++)
            memcpy(job.log[i - 1], job.log[i], FLASH_LINE_MAX);
        snprintf(job.log[FLASH_LOG_LINES - 1], FLASH_LINE_MAX, "%s", line);
    }
    snprintf(job.message, sizeof(job.message), "%s", line);
    pthread_mutex_unlock(&flash_lock);
}

static void set_progress(int value)
{
    if (value < 0) value = 0;
    if (value > 100) value = 100;
    pthread_mutex_lock(&flash_lock);
    if (value > job.progress)
        job.progress = value;
    pthread_mutex_unlock(&flash_lock);
}

/* Presence comes from the firmware_status cache, which owns the only safe
 * polling path (read-only protocol commands under the motor/EC locks).  A
 * raw I2C write would disturb the EC/STM8 protocols. */
static int target_present(const char *target)
{
    int present = 0;
    char state[8], version[20], boot[20];
    long age;

    if (!strcmp(target, "stm32"))
        firmware_status_stm32(&present, state, sizeof(state), version,
                              sizeof(version), &age);
    else
        firmware_status_stm8(&present, state, sizeof(state), version,
                             sizeof(version), boot, sizeof(boot), &age);
    return present;
}

/* Only async-signal-safe calls are made between fork() and execv(): divinus
 * has media/focus/telemetry threads, so popen()/system()/execvp() can deadlock
 * on a libc lock held by another thread. */
/* Do not leak divinus sockets (RTSP/HTTP) into helper processes: the EC
 * daemon would otherwise inherit the RTSP listening socket. */
static void close_extra_fds(int keep_fd)
{
    int fd;

    for (fd = 3; fd < 1024; fd++)
        if (fd != keep_fd)
            close(fd);
}

static int wait_child(pid_t pid, int *status)
{
    pid_t waited;

    do {
        waited = waitpid(pid, status, 0);
    } while (waited < 0 && errno == EINTR);
    return waited == pid ? 0 : -1;
}

static int run_quiet(char *const argv[])
{
    pid_t pid = fork();
    int status;

    if (pid < 0)
        return -1;
    if (pid == 0)
    {
        int nullfd = open("/dev/null", O_WRONLY);
        if (nullfd >= 0)
        {
            dup2(nullfd, STDOUT_FILENO);
            dup2(nullfd, STDERR_FILENO);
            if (nullfd > STDERR_FILENO)
                close(nullfd);
        }
        close_extra_fds(-1);
        execv(argv[0], argv);
        _exit(127);
    }
    if (wait_child(pid, &status) < 0)
        return -1;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}

static int run_shell_quiet(const char *command)
{
    char *argv[4];

    argv[0] = "/bin/sh";
    argv[1] = "-c";
    argv[2] = (char *)command;
    argv[3] = NULL;
    return run_quiet(argv);
}

static int run_capture(char *const argv[], char *out, size_t out_size)
{
    int fds[2];
    pid_t pid;
    ssize_t n;
    size_t used = 0;
    char tmp[128];
    int status;

    out[0] = '\0';
    if (out_size == 0)
        return -1;
    if (pipe(fds))
        return -1;
    pid = fork();
    if (pid < 0)
    {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }
    if (pid == 0)
    {
        close(fds[0]);
        dup2(fds[1], STDOUT_FILENO);
        if (fds[1] > STDOUT_FILENO)
            close(fds[1]);
        close_extra_fds(-1);
        execv(argv[0], argv);
        _exit(127);
    }
    close(fds[1]);
    while ((n = read(fds[0], tmp, sizeof(tmp))) > 0)
    {
        size_t copy = (size_t)n;
        if (copy > out_size - 1 - used)
            copy = out_size - 1 - used;
        memcpy(out + used, tmp, copy);
        used += copy;
    }
    close(fds[0]);
    if (wait_child(pid, &status) < 0 || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0)
        return -1;
    out[used] = '\0';
    return 0;
}

static int compute_sha256(const char *path, char out[65])
{
    char *argv[3];
    char text[128];

    out[0] = '\0';
    if (access("/usr/bin/sha256sum", X_OK))
        return -1;
    argv[0] = "/usr/bin/sha256sum";
    argv[1] = (char *)path;
    argv[2] = NULL;
    if (run_capture(argv, text, sizeof(text)) < 0)
        return -1;
    {
        size_t n = strspn(text, "0123456789abcdefABCDEF");
        if (n != 64)
        {
            out[0] = '\0';
            return -1;
        }
        memcpy(out, text, 64);
        out[64] = '\0';
    }
    return 0;
}

/* The daemon owns /tmp/oko-ec.pid; kill(pid, 0) is cheaper and safer than
 * walking /proc from a worker thread. */
static int ec_running(void)
{
    FILE *file = fopen("/tmp/oko-ec.pid", "r");
    char text[32];
    long pid = 0;

    if (!file)
        return 0;
    if (fgets(text, sizeof(text), file))
        pid = strtol(text, NULL, 10);
    fclose(file);
    return pid > 0 && kill((pid_t)pid, 0) == 0;
}

static int create_temp_file(const char *target, const char *extension,
                            char *path, size_t path_size)
{
    char base[256];
    int fd;

    if (snprintf(base, sizeof(base), "/tmp/fw-%s-XXXXXX", target) >=
        (int)sizeof(base))
        return -1;
    fd = mkstemp(base);
    if (fd < 0)
        return -1;
    close(fd);
    if (snprintf(path, path_size, "%s%s", base, extension) >= (int)path_size)
    {
        unlink(base);
        return -1;
    }
    /* rename() replaces a pre-created symlink itself, never its target. */
    if (rename(base, path) != 0)
    {
        unlink(base);
        return -1;
    }
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
    if (fd < 0)
    {
        unlink(path);
        return -1;
    }
    return fd;
}

/* Validate an STM8 image before a job is created.  "oko-motor check" parses
 * the HEX/BIN, the OKO8 header, the address range and CRC32 without touching
 * I2C, so a bad upload is rejected with 400 instead of failing mid-flash. */
static int offline_check(const char *file, const char *version)
{
    char *argv[5];
    const char *tool = "/oem/usr/bin/oko-motor";

    if (access(tool, X_OK))
    {
        tool = "/usr/bin/oko-motor";
        if (access(tool, X_OK))
            return -1;
    }
    argv[0] = (char *)tool;
    argv[1] = "check";
    argv[2] = (char *)file;
    argv[3] = (char *)version;
    argv[4] = NULL;
    return run_quiet(argv);
}

static const char *find_bytes(const char *hay, size_t hay_len,
                              const char *needle, size_t needle_len)
{
    size_t i;

    if (needle_len == 0 || hay_len < needle_len)
        return NULL;
    for (i = 0; i + needle_len <= hay_len; i++)
        if (!memcmp(hay + i, needle, needle_len))
            return hay + i;
    return NULL;
}

static int header_value(const char *headers, size_t len, const char *key,
                        char *out, size_t out_size)
{
    char pattern[32];
    const char *p;
    const char *end;

    snprintf(pattern, sizeof(pattern), "%s=\"", key);
    p = find_bytes(headers, len, pattern, strlen(pattern));
    if (!p)
        return -1;
    p += strlen(pattern);
    end = memchr(p, '"', (size_t)(headers + len - p));
    if (!end)
        return -1;
    if ((size_t)(end - p) >= out_size)
        return -1;
    memcpy(out, p, (size_t)(end - p));
    out[end - p] = '\0';
    return 0;
}

static int copy_value(const char *data, size_t len, char *out, size_t out_size)
{
    if (len >= out_size)
        return -1;
    memcpy(out, data, len);
    out[len] = '\0';
    return 0;
}

/* The upload carries its version in the file name: exactly one dotted triple
 * such as app-1.0.1.bin or factory-2.0.2.ihx.  A missing, malformed or
 * repeated triple is rejected, because there is no other side channel for the
 * STM32 metadata version and an ambiguous name would flash the wrong value. */
static int parse_version_from_name(const char *name, unsigned *major,
                                   unsigned *minor, unsigned *patch)
{
    const char *p = name;
    unsigned found = 0, vmaj = 0, vmin = 0, vpat = 0;

    while (*p)
    {
        unsigned parts[4];
        int count = 0;

        if (*p < '0' || *p > '9')
        {
            p++;
            continue;
        }
        while (*p >= '0' && *p <= '9')
        {
            char *end = NULL;
            if (count >= 4)
                break;
            parts[count++] = (unsigned)strtoul(p, &end, 10);
            p = end;
            if (*p != '.')
                break;
            p++;
            if (*p < '0' || *p > '9')
                break;          /* a trailing dot is not a version */
        }
        if (count > 3)
            return -1;          /* four or more components: ambiguous */
        if (count == 3)
        {
            if (found || parts[0] > 255 || parts[1] > 255 || parts[2] > 255)
                return -1;
            vmaj = parts[0];
            vmin = parts[1];
            vpat = parts[2];
            found = 1;
        }
    }
    if (!found)
        return -1;
    *major = vmaj;
    *minor = vmin;
    *patch = vpat;
    return 0;
}

static int safe_name(const char *in, char *out, size_t out_size)
{
    size_t i, n = 0;

    for (i = 0; in && in[i] && n + 1 < out_size; i++)
    {
        char c = in[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')
            out[n++] = c;
    }
    out[n] = '\0';
    return n > 0;
}

static void spawn_worker(void);

static char *flash_post_impl(const char *content_type, char *payload,
                             int body_len, int *http_status)
{
    char boundary[96], marker[104];
    char target[16] = {0}, confirm[32] = {0}, version[16] = {0};
    char filename[96] = {0};
    unsigned major = 0, minor = 0, patch = 0;
    char extension[8] = {0};
    const char *file_data = NULL;
    size_t file_len = 0;
    const char *type = content_type;
    const char *b;
    size_t bound_len;
    char *p, *end;
    char path[256];
    struct stat st;
    char response[512];
    int code = 400;

    *http_status = 400;
    if (!type || strncmp(type, "multipart/form-data", 19) != 0 || body_len <= 0)
        goto reject;

    b = strstr(type, "boundary=");
    if (!b)
        goto reject;
    b += strlen("boundary=");
    bound_len = strcspn(b, ";\r\n");
    if (bound_len >= 2 && b[0] == '"' && b[bound_len - 1] == '"')
    {
        b++;
        bound_len -= 2;
    }
    if (bound_len == 0 || bound_len >= sizeof(boundary))
        goto reject;
    snprintf(boundary, sizeof(boundary), "%.*s", (int)bound_len, b);
    snprintf(marker, sizeof(marker), "--%s", boundary);

    p = payload;
    end = payload + body_len;
    while (p < end)
    {
        const char *m = find_bytes(p, (size_t)(end - p), marker,
                                   strlen(marker));
        const char *hdr_end, *body, *next, *body_end;
        char headers[1024];
        size_t headers_len;

        if (!m)
            break;
        p = (char *)m + strlen(marker);
        if (p + 2 <= end && p[0] == '-' && p[1] == '-')
            break;
        if (p + 2 <= end && p[0] == '\r' && p[1] == '\n')
            p += 2;
        hdr_end = find_bytes(p, (size_t)(end - p), "\r\n\r\n", 4);
        if (!hdr_end)
            break;
        body = hdr_end + 4;
        next = find_bytes(body, (size_t)(end - body), marker, strlen(marker));
        body_end = next ? next : end;
        if (next && body_end - body >= 2 &&
            body_end[-2] == '\r' && body_end[-1] == '\n')
            body_end -= 2;

        headers_len = (size_t)(hdr_end - p);
        if (headers_len >= sizeof(headers))
            headers_len = sizeof(headers) - 1;
        memcpy(headers, p, headers_len);
        headers[headers_len] = '\0';

        {
            char name[32] = {0};
            char fn[96] = {0};

            if (header_value(headers, headers_len, "name", name,
                             sizeof(name)) == 0)
            {
                if (!strcmp(name, "file"))
                {
                    if (header_value(headers, headers_len, "filename", fn,
                                     sizeof(fn)) == 0)
                        safe_name(fn, filename, sizeof(filename));
                    file_data = body;
                    file_len = (size_t)(body_end - body);
                }
                else if (!strcmp(name, "target"))
                    copy_value(body, (size_t)(body_end - body), target,
                               sizeof(target));
                else if (!strcmp(name, "confirm"))
                    copy_value(body, (size_t)(body_end - body), confirm,
                               sizeof(confirm));
            }
        }
        p = (char *)(next ? next : end);
    }

    if (!target[0] || !confirm[0] || !file_data || file_len == 0)
        goto reject;
    if (strcmp(confirm, "FLASH") != 0)
        goto reject;
    if (strcmp(target, "stm32") != 0 && strcmp(target, "stm8") != 0)
        goto reject;
    if (!app_config.flash_enable)
    {
        snprintf(response, sizeof(response),
            "{\"error\":\"blocked\",\"reason\":\"firmware upload is disabled\","
            "\"hint\":\"set flash.enable to true\"}");
        *http_status = 200;
        return strdup(response);
    }
    if (flash_busy())
    {
        snprintf(response, sizeof(response),
            "{\"error\":\"busy\",\"reason\":\"another flash job is running\","
            "\"hint\":\"wait for it to finish\"}");
        *http_status = 409;
        return strdup(response);
    }
    if (focus_home_running())
    {
        snprintf(response, sizeof(response),
            "{\"error\":\"busy\",\"reason\":\"a home/zero job is running\","
            "\"hint\":\"wait for it to finish\"}");
        *http_status = 409;
        return strdup(response);
    }

    if (parse_version_from_name(filename, &major, &minor, &patch) != 0)
    {
        snprintf(response, sizeof(response),
            "{\"error\":\"invalid\",\"reason\":\"the version must appear once "
            "in the file name as app-1.0.1.bin\"}");
        *http_status = 400;
        return strdup(response);
    }

    if (!strcmp(target, "stm32"))
    {
        const char *ext = strrchr(filename, '.');
        if (!ext || strcasecmp(ext, ".bin"))
            goto reject;
        snprintf(extension, sizeof(extension), ".bin");
        snprintf(version, sizeof(version), "0x%08X",
                 (major << 16) | (minor << 8) | patch);
    }
    else
    {
        const char *ext = strrchr(filename, '.');
        if (!ext || (strcasecmp(ext, ".ihx") && strcasecmp(ext, ".hex") &&
                     strcasecmp(ext, ".bin")))
            goto reject;
        /* Keep the format: oko-motor/oko-flash select HEX vs BIN by suffix. */
        if (!strcasecmp(ext, ".ihx"))
            snprintf(extension, sizeof(extension), ".ihx");
        else if (!strcasecmp(ext, ".hex"))
            snprintf(extension, sizeof(extension), ".hex");
        else
            snprintf(extension, sizeof(extension), ".bin");
        snprintf(version, sizeof(version), "%u.%u.%u", major, minor, patch);
    }

    if (file_len > app_config.flash_max_bytes)
    {
        snprintf(response, sizeof(response),
            "{\"error\":\"invalid\",\"reason\":\"the file is larger than "
            "flash.max_bytes\",\"hint\":\"the limit is %u bytes\"}",
            app_config.flash_max_bytes);
        *http_status = 400;
        return strdup(response);
    }
    if (!strcmp(target, "stm32") && file_len > FLASH_STM32_APP_MAX)
    {
        snprintf(response, sizeof(response),
            "{\"error\":\"invalid\",\"reason\":\"the STM32 image does not "
            "fit the CBT6 application slot\",\"hint\":\"the limit is "
            "%u bytes\"}", FLASH_STM32_APP_MAX);
        *http_status = 400;
        return strdup(response);
    }
    if (strcmp(target, "stm32") == 0 && !target_present("stm32"))
    {
        snprintf(response, sizeof(response),
            "{\"error\":\"blocked\",\"reason\":\"the STM32 controller does "
            "not answer\",\"hint\":\"check I2C3 and 0x42\"}");
        *http_status = 200;
        return strdup(response);
    }
    if (strcmp(target, "stm8") == 0 && !target_present("stm8"))
    {
        snprintf(response, sizeof(response),
            "{\"error\":\"blocked\",\"reason\":\"the STM8 controller does "
            "not answer\",\"hint\":\"flash factory.ihx with ST-Link first\"}");
        *http_status = 200;
        return strdup(response);
    }

    if (!extension[0])
        snprintf(extension, sizeof(extension), ".bin");
    {
        int tfd = create_temp_file(target, extension, path, sizeof(path));
        FILE *file;
        if (tfd < 0)
            goto reject;
        file = fdopen(tfd, "wb");
        if (!file)
        {
            close(tfd);
            unlink(path);
            goto reject;
        }
        if (fwrite(file_data, 1, file_len, file) != file_len)
        {
            fclose(file);
            unlink(path);
            goto reject;
        }
        fclose(file);
    }
    if (stat(path, &st) || !S_ISREG(st.st_mode) ||
        (unsigned long long)st.st_size != file_len)
    {
        unlink(path);
        goto reject;
    }

    if (!strcmp(target, "stm8"))
    {
        /* First validate the image itself, then compare the version that the
         * file name advertises with the one in the OKO8 header.  The device
         * still trusts the header; this only rejects a mislabelled upload. */
        if (offline_check(path, NULL) != 0)
        {
            unlink(path);
            snprintf(response, sizeof(response),
                "{\"error\":\"invalid\",\"reason\":\"the STM8 image failed the "
                "OKO8/CRC/address check\",\"hint\":\"use app-2.0.2.ihx or "
                "app-2.0.2.bin from tools/mkapp.py\"}");
            *http_status = 400;
            return strdup(response);
        }
        if (offline_check(path, version) != 0)
        {
            unlink(path);
            snprintf(response, sizeof(response),
                "{\"error\":\"invalid\",\"reason\":\"the file name version "
                "does not match the OKO8 header\"}");
            *http_status = 400;
            return strdup(response);
        }
    }

    {
        char sha[65];
        compute_sha256(path, sha);
        pthread_mutex_lock(&flash_lock);
        memset(&job, 0, sizeof(job));
        snprintf(job.sha256, sizeof(job.sha256), "%s", sha);
    }
    job.started = (long)time(NULL);
    job.progress = 0;
    snprintf(job.state, sizeof(job.state), "queued");
    snprintf(job.target, sizeof(job.target), "%s", target);
    snprintf(job.file, sizeof(job.file), "%s",
             filename[0] ? filename : "firmware");
    {
        time_t now = time(NULL);
        struct tm tmv;
        localtime_r(&now, &tmv);
        strftime(job.id, sizeof(job.id), "%Y%m%d-%H%M%S", &tmv);
    }
    snprintf(pending_file, sizeof(pending_file), "%s", path);
    snprintf(pending_version, sizeof(pending_version), "%s", version);
    busy = 1;
    pthread_mutex_unlock(&flash_lock);

    spawn_worker();

    {
        char *body = flash_json();
        *http_status = 202;
        return body ? body : strdup("{\"error\":\"failed\",\"reason\":\"OOM\"}");
    }

reject:
    *http_status = code;
    return strdup("{\"error\":\"invalid\",\"reason\":\"invalid request\","
                  "\"hint\":\"send target, file and confirm=FLASH\"}");
}

char *flash_post(const char *content_type, char *payload, int body_len,
                 int *http_status)
{
    return flash_post_impl(content_type, payload, body_len, http_status);
}

/* ------------------------------------------------------------------ worker */

static int parse_progress(const char *line)
{
    const char *p;
    int value = -1;

    if ((p = strstr(line, "PROGRESS ")) != NULL)
    {
        if (sscanf(p + 9, "%d", &value) == 1)
            return value;
    }
    if ((p = strstr(line, "Progress:")) != NULL)
    {
        const char *paren = strchr(p, '(');
        if (paren && sscanf(paren + 1, "%d%%", &value) == 1)
            return value;
    }
    return -1;
}

static void *flash_worker(void *arg)
{
    char *args[8];
    char line[FLASH_LINE_MAX];
    char tool[256];
    char file[256];
    int pipefd[2];
    pid_t pid;
    int status = 0;
    int rc = 0;
    int ec_was_running;
    FILE *output;

    (void)arg;
    /* Snapshot the job's path: a later upload may overwrite pending_file. */
    snprintf(file, sizeof(file), "%s", pending_file);
    set_state("flashing", "stopping the telemetry daemon");

    ec_was_running = ec_running();
    if (ec_was_running)
        run_shell_quiet("/etc/init.d/S21oko-ec stop >/dev/null 2>&1");
    focus_motor_stop_all();
    focus_motor_suspend(1);

    snprintf(tool, sizeof(tool), "%s", "/oem/usr/bin/oko-flash");
    if (access(tool, X_OK))
    {
        snprintf(tool, sizeof(tool), "%s", "/usr/bin/oko-flash");
        if (access(tool, X_OK))
        {
            rc = -1;
            goto restore;
        }
    }

    args[0] = tool;
    args[1] = "--target";
    args[2] = job.target;
    args[3] = "--file";
    args[4] = file;
    args[5] = NULL;
    if (!strcmp(job.target, "stm32") && pending_version[0])
    {
        args[5] = "--version";
        args[6] = pending_version;
        args[7] = NULL;
    }

    if (pipe(pipefd))
    {
        rc = -1;
        goto restore;
    }
    pid = fork();
    if (pid < 0)
    {
        close(pipefd[0]);
        close(pipefd[1]);
        rc = -1;
        goto restore;
    }
    if (pid == 0)
    {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close_extra_fds(pipefd[1]);
        close(pipefd[1]);
        execv(args[0], args);
        _exit(127);
    }
    close(pipefd[1]);
    output = fdopen(pipefd[0], "r");
    if (output)
    {
        while (fgets(line, sizeof(line), output))
        {
            size_t n = strlen(line);
            int pct;

            while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
                line[--n] = '\0';
            if (!line[0])
                continue;
            append_log(line);
            pct = parse_progress(line);
            if (pct >= 0)
                set_progress(pct);
        }
        fclose(output);
    }
    {
        pid_t waited;
        do {
            waited = waitpid(pid, &status, 0);
        } while (waited < 0 && errno == EINTR);
        rc = (waited == pid && WIFEXITED(status) &&
              WEXITSTATUS(status) == 0) ? 0 : -1;
    }

restore:
    focus_motor_suspend(0);
    focus_motor_probe();
    if (ec_was_running)
        run_shell_quiet("/etc/init.d/S21oko-ec start >/dev/null 2>&1");
    sleep(3);
    firmware_status_refresh();

    pthread_mutex_lock(&flash_lock);
    job.finished = (long)time(NULL);
    if (rc == 0)
    {
        snprintf(job.state, sizeof(job.state), "done");
        snprintf(job.message, sizeof(job.message), "firmware flashed");
        job.progress = 100;
    }
    else
    {
        snprintf(job.state, sizeof(job.state), "error");
        snprintf(job.message, sizeof(job.message),
                 "flash failed; the bootloader is still available");
    }
    busy = 0;
    pthread_mutex_unlock(&flash_lock);

    unlink(file);
    return NULL;
}

static void spawn_worker(void)
{
    if (thread_valid)
        pthread_join(worker_thread, NULL);
    thread_valid = 0;
    if (pthread_create(&worker_thread, NULL, flash_worker, NULL) == 0)
        thread_valid = 1;
    else
    {
        unlink(pending_file);
        busy = 0;
        set_state("error", "cannot start the flash worker");
    }
}

/* ------------------------------------------------------------------- public */

int flash_init(void)
{
    memset(&job, 0, sizeof(job));
    busy = 0;
    return 0;
}

void flash_shutdown(void)
{
    if (thread_valid)
    {
        pthread_join(worker_thread, NULL);
        thread_valid = 0;
    }
}

int flash_busy(void)
{
    int value;
    pthread_mutex_lock(&flash_lock);
    value = busy;
    pthread_mutex_unlock(&flash_lock);
    return value;
}

static void json_escape(char *out, size_t out_size, const char *in)
{
    size_t n = 0;

    for (; in && *in && n + 2 < out_size; in++)
    {
        unsigned char c = (unsigned char)*in;
        if (c == '"' || c == '\\')
        {
            out[n++] = '\\';
            out[n++] = (char)c;
        }
        else if (c < 0x20)
            out[n++] = ' ';
        else
            out[n++] = (char)c;
    }
    out[n] = '\0';
}

char *flash_json(void)
{
    char *buffer = malloc(8192);
    char state[16], target[8], jobid[32], message[256], file[96], sha[65];
    char logline[FLASH_LINE_MAX * 2];
    int progress, log_count, present32, present8;
    long started, finished, age32, age8;
    char v32[16], s32[8], v8[16], b8[16], s8[8];
    size_t used;
    int i;

    if (!buffer)
        return NULL;

    pthread_mutex_lock(&flash_lock);
    snprintf(state, sizeof(state), "%s", job.state);
    snprintf(target, sizeof(target), "%s", job.target);
    snprintf(jobid, sizeof(jobid), "%s", job.id);
    snprintf(message, sizeof(message), "%s", job.message);
    snprintf(file, sizeof(file), "%s", job.file);
    snprintf(sha, sizeof(sha), "%s", job.sha256);
    progress = job.progress;
    started = job.started;
    finished = job.finished;
    log_count = job.log_count;
    pthread_mutex_unlock(&flash_lock);

    firmware_status_stm32(&present32, s32, sizeof(s32), v32, sizeof(v32),
                          &age32);
    firmware_status_stm8(&present8, s8, sizeof(s8), v8, sizeof(v8), b8,
                         sizeof(b8), &age8);

    used = (size_t)snprintf(buffer, 8192,
        "{\"enable\":%s,\"busy\":%s,\"job\":",
        app_config.flash_enable ? "true" : "false",
        flash_busy() ? "true" : "false");

    if (!jobid[0])
        used += (size_t)snprintf(buffer + used, 8192 - used, "null");
    else
    {
        char esc_message[512], esc_file[192], esc_target[32], esc_state[32];
        json_escape(esc_message, sizeof(esc_message), message);
        json_escape(esc_file, sizeof(esc_file), file);
        json_escape(esc_target, sizeof(esc_target), target);
        json_escape(esc_state, sizeof(esc_state), state);
        used += (size_t)snprintf(buffer + used, 8192 - used,
            "{\"id\":\"%s\",\"target\":\"%s\",\"state\":\"%s\","
            "\"progress\":%d,\"message\":\"%s\",\"file\":\"%s\","
            "\"sha256\":\"%s\",\"started\":%ld,\"finished\":%ld,\"log\":[",
            jobid, esc_target, esc_state, progress, esc_message, esc_file,
            sha, started, finished);
        for (i = 0; i < log_count && used < 7900; i++)
        {
            char line[FLASH_LINE_MAX];
            pthread_mutex_lock(&flash_lock);
            snprintf(line, sizeof(line), "%s", job.log[i]);
            pthread_mutex_unlock(&flash_lock);
            json_escape(logline, sizeof(logline), line);
            used += (size_t)snprintf(buffer + used, 8192 - used,
                "%s\"%s\"", i ? "," : "", logline);
        }
        used += (size_t)snprintf(buffer + used, 8192 - used, "]}");
    }

    if (used >= 8192 - 256)
    {
        free(buffer);
        return NULL;
    }

    {
        char q32[20], q8[20];

        if (v32[0])
            snprintf(q32, sizeof(q32), "\"%s\"", v32);
        else
            snprintf(q32, sizeof(q32), "null");
        if (v8[0])
            snprintf(q8, sizeof(q8), "\"%s\"", v8);
        else
            snprintf(q8, sizeof(q8), "null");

        used += (size_t)snprintf(buffer + used, 8192 - used,
            ",\"targets\":["
            "{\"name\":\"stm32\",\"present\":%s,\"address\":66,"
            "\"state\":\"%s\",\"version\":%s},"
            "{\"name\":\"stm8\",\"present\":%s,\"address\":%u,"
            "\"state\":\"%s\",\"version\":%s}]}",
            present32 ? "true" : "false", s32, q32,
            present8 ? "true" : "false", app_config.focus_address, s8, q8);
    }
    return buffer;
}