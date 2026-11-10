#define _GNU_SOURCE

#include "update.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "app_config.h"
#include "gating.h"

#define UPDATE_DIR        "/mnt/sdcard/oko-update"
#define UPDATE_UPLOAD     UPDATE_DIR "/upload.okp"
#define UPDATE_STAGE      "/run/oko-update.stage"
#define UPDATE_TOKEN_FILE "/run/oko-update.token"
#define UPDATE_PENDING    "/userdata/oko-update.pending"
#define UPDATE_RESULT     "/userdata/oko-update.state"
#define UPDATE_RELEASE    "/etc/oko-release"
#define UPDATE_TOOL       "/usr/bin/oko-update"
#define UPDATE_TOOL_OEM   "/oem/usr/bin/oko-update"
#define UPDATE_CURL       "/usr/bin/curl"
#define UPDATE_REBOOT     "/sbin/reboot"

#define UPDATE_OUT_MAX    1024
#define UPDATE_URL_MAX    1024

enum worker_kind {
    WORKER_NONE = 0,
    WORKER_STAGE,
    WORKER_FETCH,
    WORKER_APPLY
};

typedef struct {
    char state[16];             /* idle, uploading, fetching, staging, ... */
    char message[192];
    int progress;
    long started;
    long finished;
    long long received;
    long long total;
} update_job;

static pthread_mutex_t update_lock = PTHREAD_MUTEX_INITIALIZER;
static update_job job;
static int busy;
static int thread_valid;
static pthread_t worker_thread;
static int worker_kind = WORKER_NONE;
static pid_t worker_pid;
static char pending_url[UPDATE_URL_MAX];
static char pending_token[32];
static char apply_token[17];

static int upload_fd = -1;
static long long upload_total;
static long long upload_received;
static int upload_cancel;
static char upload_error[160];

/* ------------------------------------------------------------------ helpers */

static void json_escape(char *out, size_t out_size, const char *in)
{
    size_t n = 0;

    if (!out || out_size == 0)
        return;
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

/* Reads a key=value line from a small state file written by oko-update. */
static int read_kv(const char *path, const char *key, char *out, size_t size)
{
    FILE *file;
    char line[256];
    size_t key_len;

    if (!out || size == 0)
        return -1;
    out[0] = '\0';
    file = fopen(path, "r");
    if (!file)
        return -1;
    key_len = strlen(key);
    while (fgets(line, sizeof(line), file))
    {
        if (!strncmp(line, key, key_len) && line[key_len] == '=')
        {
            size_t n = strlen(line + key_len + 1);
            while (n && (line[key_len + n] == '\n' ||
                         line[key_len + n] == '\r'))
                n--;
            if (n >= size)
                n = size - 1;
            memcpy(out, line + key_len + 1, n);
            out[n] = '\0';
            fclose(file);
            return 0;
        }
    }
    fclose(file);
    return -1;
}

static int read_int_kv(const char *path, const char *key)
{
    char text[32];
    if (read_kv(path, key, text, sizeof(text)))
        return 0;
    return atoi(text);
}

static void current_version(char *out, size_t size)
{
    if (read_kv(UPDATE_RELEASE, "OKO_VERSION", out, size) ||
        !out[0])
        snprintf(out, size, "unknown");
}

static int tool_path(char *out, size_t size)
{
    if (!access(UPDATE_TOOL, X_OK))
    {
        snprintf(out, size, "%s", UPDATE_TOOL);
        return 0;
    }
    if (!access(UPDATE_TOOL_OEM, X_OK))
    {
        snprintf(out, size, "%s", UPDATE_TOOL_OEM);
        return 0;
    }
    out[0] = '\0';
    return -1;
}

static void token_generate_locked(void)
{
    static const char hex[] = "0123456789abcdef";
    unsigned char raw[8];
    FILE *file;
    int i;
    int filled = 0;

    file = fopen("/dev/urandom", "rb");
    if (file)
    {
        filled = fread(raw, 1, sizeof(raw), file) == sizeof(raw);
        fclose(file);
    }
    if (!filled)
    {
        unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)getpid();
        srand(seed);
        for (i = 0; i < (int)sizeof(raw); i++)
            raw[i] = (unsigned char)rand();
    }
    for (i = 0; i < (int)sizeof(raw); i++)
    {
        apply_token[i * 2] = hex[raw[i] >> 4];
        apply_token[i * 2 + 1] = hex[raw[i] & 0x0f];
    }
    apply_token[16] = '\0';

    /* The helper compares the token itself, so apply is refused even if
     * divinus is bypassed or its check is wrong. */
    file = fopen(UPDATE_TOKEN_FILE ".tmp", "w");
    if (file)
    {
        fprintf(file, "%s\n", apply_token);
        fclose(file);
        chmod(UPDATE_TOKEN_FILE ".tmp", 0600);
        rename(UPDATE_TOKEN_FILE ".tmp", UPDATE_TOKEN_FILE);
    }
}

static void job_set(const char *state, const char *message, int progress)
{
    pthread_mutex_lock(&update_lock);
    snprintf(job.state, sizeof(job.state), "%s", state);
    if (message)
        snprintf(job.message, sizeof(job.message), "%s", message);
    if (progress >= 0)
        job.progress = progress > 100 ? 100 : progress;
    pthread_mutex_unlock(&update_lock);
}

static void job_finish(const char *state, const char *message)
{
    pthread_mutex_lock(&update_lock);
    snprintf(job.state, sizeof(job.state), "%s", state);
    if (message)
        snprintf(job.message, sizeof(job.message), "%s", message);
    job.finished = (long)time(NULL);
    if (!strcmp(state, "done"))
        job.progress = 100;
    busy = 0;
    pthread_mutex_unlock(&update_lock);
}

/* Only async-signal-safe calls run between fork() and execv(): divinus has
 * media/HTTP/telemetry threads, so popen()/system() can deadlock. */
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

/* Runs a helper, keeps the tail of its output for the WebUI, and tracks the
 * child so POST /api/update?cancel=1 can stop a long download. */
static int run_capture(char *const argv[], char *out, size_t out_size)
{
    int fds[2];
    pid_t pid;
    ssize_t n;
    size_t used = 0;
    char tmp[256];
    int status;

    if (out && out_size)
        out[0] = '\0';
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
        dup2(fds[1], STDERR_FILENO);
        if (fds[1] > STDERR_FILENO)
            close(fds[1]);
        close_extra_fds(-1);
        execv(argv[0], argv);
        _exit(127);
    }
    close(fds[1]);
    pthread_mutex_lock(&update_lock);
    worker_pid = pid;
    pthread_mutex_unlock(&update_lock);

    while ((n = read(fds[0], tmp, sizeof(tmp))) > 0)
    {
        size_t copy = (size_t)n;

        if (copy > sizeof(tmp))
            copy = sizeof(tmp);
        if (!out || out_size < 2)
            continue;
        if (copy >= out_size - 1)
        {
            /* The chunk alone is larger than the buffer; keep its tail. */
            memcpy(out, tmp + copy - (out_size - 1), out_size - 1);
            used = out_size - 1;
            out[used] = '\0';
        }
        else
        {
            if (used + copy > out_size - 1)
            {
                size_t drop = used + copy - (out_size - 1);
                memmove(out, out + drop, used - drop);
                used -= drop;
            }
            memcpy(out + used, tmp, copy);
            used += copy;
            out[used] = '\0';
        }
    }
    close(fds[0]);
    if (wait_child(pid, &status) < 0)
    {
        pthread_mutex_lock(&update_lock);
        worker_pid = 0;
        pthread_mutex_unlock(&update_lock);
        return -1;
    }
    pthread_mutex_lock(&update_lock);
    worker_pid = 0;
    pthread_mutex_unlock(&update_lock);
    if (!WIFEXITED(status))
        return -1;
    return WEXITSTATUS(status) == 0 ? 0 : -1;
}

static void trim_message(char *text)
{
    size_t n;

    if (!text)
        return;
    n = strlen(text);
    while (n && (text[n - 1] == '\n' || text[n - 1] == '\r' ||
                 text[n - 1] == ' '))
        text[--n] = '\0';
    /* The WebUI shows one line; the tail already starts at a line boundary
     * only when the helper printed a lot, which is good enough for an error. */
}

static void stage_result_message(char *out, size_t size)
{
    char staged[8], ok[8], version[64], error[64];

    staged[0] = ok[0] = version[0] = error[0] = '\0';
    read_kv(UPDATE_STAGE, "staged", staged, sizeof(staged));
    read_kv(UPDATE_STAGE, "ok", ok, sizeof(ok));
    read_kv(UPDATE_STAGE, "version", version, sizeof(version));
    read_kv(UPDATE_STAGE, "error", error, sizeof(error));
    if (!strcmp(staged, "1") && !strcmp(ok, "1"))
        snprintf(out, size, "package %s staged", version);
    else if (error[0])
        snprintf(out, size, "staging failed: %s", error);
    else
        snprintf(out, size, "staging failed");
}

/* Runs `oko-update stage` and converts the helper state into a job result. */
static int stage_package(void)
{
    char tool[PATH_MAX];
    char output[UPDATE_OUT_MAX];
    char message[192];
    char staged[8], ok[8];
    int rc;

    if (tool_path(tool, sizeof(tool)))
    {
        job_finish("error", "the oko-update helper is missing");
        return -1;
    }
    {
        char *argv[4];
        argv[0] = tool;
        argv[1] = (char *)"stage";
        argv[2] = (char *)UPDATE_UPLOAD;
        argv[3] = NULL;
        rc = run_capture(argv, output, sizeof(output));
    }
    if (rc != 0)
    {
        unlink(UPDATE_UPLOAD);
        staged[0] = ok[0] = '\0';
        read_kv(UPDATE_STAGE, "staged", staged, sizeof(staged));
        read_kv(UPDATE_STAGE, "ok", ok, sizeof(ok));
        trim_message(output);
        stage_result_message(message, sizeof(message));
        if (strlen(output) > 0 && strstr(message, "failed") != NULL)
        {
            size_t used = strlen(message);
            snprintf(message + used, sizeof(message) - used, ": %.100s",
                     output);
        }
        job_finish("error", message);
        return -1;
    }
    stage_result_message(message, sizeof(message));
    job_finish("done", message);
    return 0;
}

static int fetch_package(void)
{
    char output[UPDATE_OUT_MAX];
    char max_size[32];
    char *argv[16];
    int arg = 0;
    unsigned long long max_bytes;

    if (access(UPDATE_CURL, X_OK))
    {
        job_finish("error", "curl is missing from the image");
        return -1;
    }
    max_bytes = (unsigned long long)app_config.update_max_package_mb *
        1024ull * 1024ull;
    snprintf(max_size, sizeof(max_size), "%llu", max_bytes);

    argv[arg++] = (char *)UPDATE_CURL;
    argv[arg++] = (char *)"-fSL";
    argv[arg++] = (char *)"--proto";
    argv[arg++] = (char *)"=http,https";
    argv[arg++] = (char *)"--proto-redir";
    argv[arg++] = (char *)"=http,https";
    argv[arg++] = (char *)"--max-time";
    argv[arg++] = (char *)"1800";
    argv[arg++] = (char *)"--max-filesize";
    argv[arg++] = max_size;
    argv[arg++] = (char *)"-o";
    argv[arg++] = (char *)UPDATE_UPLOAD;
    argv[arg++] = pending_url;
    argv[arg] = NULL;

    job_set("fetching", "downloading the package", 10);
    if (run_capture(argv, output, sizeof(output)) != 0)
    {
        trim_message(output);
        if (output[0])
            job_finish("error", output);
        else
            job_finish("error", "the package download failed");
        unlink(UPDATE_UPLOAD);
        return -1;
    }
    job_set("staging", "verifying and unpacking the package", 60);
    return stage_package();
}

static void *update_worker(void *arg)
{
    int kind;

    (void)arg;
    pthread_mutex_lock(&update_lock);
    kind = worker_kind;
    pthread_mutex_unlock(&update_lock);

    if (kind == WORKER_FETCH)
        fetch_package();
    else if (kind == WORKER_STAGE)
    {
        job_set("staging", "verifying and unpacking the package", 60);
        stage_package();
    }
    else if (kind == WORKER_APPLY)
    {
        char tool[PATH_MAX];
        char output[UPDATE_OUT_MAX];
        int rc;

        if (tool_path(tool, sizeof(tool)))
        {
            job_finish("error", "the oko-update helper is missing");
        }
        else
        {
            char *argv[4];
            argv[0] = tool;
            argv[1] = (char *)"apply";
            argv[2] = pending_token;
            argv[3] = NULL;
            job_set("applying", "arming the U-Boot update and rebooting", 50);
            rc = run_capture(argv, output, sizeof(output));
            if (rc != 0)
            {
                trim_message(output);
                if (output[0])
                    job_finish("error", output);
                else
                    job_finish("error", "the update could not be armed");
            }
            else
            {
                job_set("reboot", "the device is rebooting into U-Boot", 100);
                pthread_mutex_lock(&update_lock);
                job.finished = (long)time(NULL);
                pthread_mutex_unlock(&update_lock);
                sync();
                sleep(2);
                {
                    char *args[2];
                    args[0] = (char *)UPDATE_REBOOT;
                    args[1] = NULL;
                    run_quiet(args);
                }
                job_finish("done", "reflashing from the microSD card");
            }
        }
    }

    pthread_mutex_lock(&update_lock);
    busy = 0;
    pthread_mutex_unlock(&update_lock);
    return NULL;
}

static int spawn_worker(int kind)
{
    if (thread_valid)
    {
        pthread_join(worker_thread, NULL);
        thread_valid = 0;
    }
    pthread_mutex_lock(&update_lock);
    worker_kind = kind;
    pthread_mutex_unlock(&update_lock);
    if (pthread_create(&worker_thread, NULL, update_worker, NULL) != 0)
    {
        pthread_mutex_lock(&update_lock);
        worker_kind = WORKER_NONE;
        busy = 0;
        pthread_mutex_unlock(&update_lock);
        job_finish("error", "cannot start the update worker");
        return -1;
    }
    thread_valid = 1;
    return 0;
}

/* ------------------------------------------------------------------- public */

int update_init(void)
{
    pthread_mutex_lock(&update_lock);
    memset(&job, 0, sizeof(job));
    snprintf(job.state, sizeof(job.state), "idle");
    busy = 0;
    worker_pid = 0;
    upload_fd = -1;
    upload_total = upload_received = 0;
    upload_cancel = 0;
    upload_error[0] = '\0';
    token_generate_locked();
    pthread_mutex_unlock(&update_lock);
    return 0;
}

void update_shutdown(void)
{
    pid_t pid;

    pthread_mutex_lock(&update_lock);
    upload_cancel = 1;
    pid = worker_pid;
    pthread_mutex_unlock(&update_lock);
    if (pid > 0)
        kill(pid, SIGTERM);
    if (thread_valid)
    {
        pthread_join(worker_thread, NULL);
        thread_valid = 0;
    }
    if (upload_fd >= 0)
    {
        close(upload_fd);
        upload_fd = -1;
    }
}

int update_busy(void)
{
    int value;
    pthread_mutex_lock(&update_lock);
    value = busy;
    pthread_mutex_unlock(&update_lock);
    return value;
}

int update_upload_open(long long total)
{
    gating_result gate;
    unsigned long long free_bytes = 0;
    unsigned long long total_bytes = 0;
    unsigned long long max_bytes;
    char tool[PATH_MAX];

    pthread_mutex_lock(&update_lock);
    upload_error[0] = '\0';
    pthread_mutex_unlock(&update_lock);

    gate = gating_check("update");
    if (!gate.ok)
    {
        pthread_mutex_lock(&update_lock);
        snprintf(upload_error, sizeof(upload_error), "%s",
                 gate.reason ? gate.reason : "update is blocked");
        pthread_mutex_unlock(&update_lock);
        return -1;
    }
    if (tool_path(tool, sizeof(tool)))
    {
        pthread_mutex_lock(&update_lock);
        snprintf(upload_error, sizeof(upload_error),
                 "the oko-update helper is missing from the image");
        pthread_mutex_unlock(&update_lock);
        return -1;
    }
    if (update_busy())
    {
        pthread_mutex_lock(&update_lock);
        snprintf(upload_error, sizeof(upload_error),
                 "another update job is running");
        pthread_mutex_unlock(&update_lock);
        return -1;
    }
    if (!access(UPDATE_PENDING, F_OK))
    {
        pthread_mutex_lock(&update_lock);
        snprintf(upload_error, sizeof(upload_error),
                 "an update is already pending; reboot the device");
        pthread_mutex_unlock(&update_lock);
        return -1;
    }

    max_bytes = (unsigned long long)app_config.update_max_package_mb *
        1024ull * 1024ull;
    if (total <= 0 || (unsigned long long)total > max_bytes)
    {
        pthread_mutex_lock(&update_lock);
        snprintf(upload_error, sizeof(upload_error),
                 "the package must be between 1 byte and %u MiB",
                 app_config.update_max_package_mb);
        pthread_mutex_unlock(&update_lock);
        return -1;
    }
    if (!gating_sd_space(&total_bytes, &free_bytes) ||
        free_bytes < (unsigned long long)total * 2 +
            16ull * 1024ull * 1024ull)
    {
        pthread_mutex_lock(&update_lock);
        snprintf(upload_error, sizeof(upload_error),
                 "not enough free space: staging needs twice the package size");
        pthread_mutex_unlock(&update_lock);
        return -1;
    }
    if (mkdir(UPDATE_DIR, 0755) && errno != EEXIST)
    {
        pthread_mutex_lock(&update_lock);
        snprintf(upload_error, sizeof(upload_error),
                 "cannot create the staging directory on the card");
        pthread_mutex_unlock(&update_lock);
        return -1;
    }

    unlink(UPDATE_UPLOAD);
    upload_fd = open(UPDATE_UPLOAD, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (upload_fd < 0)
    {
        pthread_mutex_lock(&update_lock);
        snprintf(upload_error, sizeof(upload_error),
                 "cannot write to the microSD card");
        pthread_mutex_unlock(&update_lock);
        return -1;
    }

    pthread_mutex_lock(&update_lock);
    upload_total = total;
    upload_received = 0;
    upload_cancel = 0;
    busy = 1;
    memset(&job, 0, sizeof(job));
    snprintf(job.state, sizeof(job.state), "uploading");
    snprintf(job.message, sizeof(job.message), "receiving the package");
    job.started = (long)time(NULL);
    job.total = total;
    pthread_mutex_unlock(&update_lock);
    return 0;
}

const char *update_upload_error(void)
{
    return upload_error;
}

long long update_upload_write(const char *data, long long len)
{
    long long done = 0;

    if (upload_fd < 0 || !data || len <= 0)
        return -1;
    while (done < len)
    {
        ssize_t n = write(upload_fd, data + done, (size_t)(len - done));
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        done += n;
    }
    pthread_mutex_lock(&update_lock);
    upload_received += len;
    job.received = upload_received;
    if (upload_total > 0)
    {
        int pct = (int)((upload_received * 90) / upload_total);
        if (pct > job.progress)
            job.progress = pct;
    }
    pthread_mutex_unlock(&update_lock);
    return done;
}

int update_upload_finish(void)
{
    if (upload_fd < 0)
        return -1;
    fsync(upload_fd);
    close(upload_fd);
    upload_fd = -1;
    job_set("staging", "verifying and unpacking the package", 90);
    if (spawn_worker(WORKER_STAGE) != 0)
    {
        unlink(UPDATE_UPLOAD);
        return -1;
    }
    return 0;
}

void update_upload_abort(const char *reason)
{
    if (upload_fd >= 0)
    {
        close(upload_fd);
        upload_fd = -1;
    }
    unlink(UPDATE_UPLOAD);
    pthread_mutex_lock(&update_lock);
    upload_received = 0;
    pthread_mutex_unlock(&update_lock);
    job_finish("error", reason ? reason : "the upload was cancelled");
}

int update_upload_cancelled(void)
{
    int value;
    pthread_mutex_lock(&update_lock);
    value = upload_cancel;
    pthread_mutex_unlock(&update_lock);
    return value;
}

static int url_allowed(const char *url, char *err, unsigned int err_size)
{
    const char *p;
    int https;

    if (!url || !*url)
    {
        snprintf(err, err_size, "the URL is empty");
        return -1;
    }
    if (strlen(url) >= UPDATE_URL_MAX)
    {
        snprintf(err, err_size, "the URL is too long");
        return -1;
    }
    https = !strncmp(url, "https://", 8);
    if (!https && strncmp(url, "http://", 7))
    {
        snprintf(err, err_size, "only http:// and https:// URLs are accepted");
        return -1;
    }
    if (!https && !app_config.update_allow_insecure_url)
    {
        snprintf(err, err_size, "plain http:// is disabled in the configuration");
        return -1;
    }
    {
        const char *host = url + (https ? 8 : 7);
        if (!strncmp(host, "localhost", 9) || !strncmp(host, "127.", 4) ||
            !strncmp(host, "[::1]", 5) || !strncmp(host, "0.0.0.0", 7))
        {
            snprintf(err, err_size,
                     "the update URL must not point at the device itself");
            return -1;
        }
    }
    /* Reject whitespace, control bytes and the shell/curl URL forms that
     * could point at a local file or another protocol. */
    for (p = url; *p; p++)
    {
        if ((unsigned char)*p <= 0x20 || *p == 0x7f)
        {
            snprintf(err, err_size, "the URL contains invalid characters");
            return -1;
        }
    }
    return 0;
}

int update_fetch_start(const char *url, char *err, unsigned int err_size)
{
    gating_result gate = gating_check("update");
    unsigned long long total_bytes = 0, free_bytes = 0;
    unsigned long long max_bytes;

    if (!gate.ok)
    {
        snprintf(err, err_size, "%s", gate.reason ? gate.reason : "blocked");
        return -1;
    }
    if (update_busy())
    {
        snprintf(err, err_size, "another update job is running");
        return -1;
    }
    if (!access(UPDATE_PENDING, F_OK))
    {
        snprintf(err, err_size, "an update is already pending");
        return -1;
    }
    if (url_allowed(url, err, err_size))
        return -1;
    max_bytes = (unsigned long long)app_config.update_max_package_mb *
        1024ull * 1024ull;
    if (!gating_sd_space(&total_bytes, &free_bytes) ||
        free_bytes < max_bytes + 16ull * 1024ull * 1024ull)
    {
        snprintf(err, err_size,
                 "not enough free space for the download limit");
        return -1;
    }
    if (mkdir(UPDATE_DIR, 0755) && errno != EEXIST)
    {
        snprintf(err, err_size, "cannot create the staging directory");
        return -1;
    }

    pthread_mutex_lock(&update_lock);
    snprintf(pending_url, sizeof(pending_url), "%s", url);
    memset(&job, 0, sizeof(job));
    snprintf(job.state, sizeof(job.state), "fetching");
    snprintf(job.message, sizeof(job.message), "downloading the package");
    job.started = (long)time(NULL);
    busy = 1;
    pthread_mutex_unlock(&update_lock);

    if (spawn_worker(WORKER_FETCH) != 0)
    {
        snprintf(err, err_size, "cannot start the download worker");
        return -1;
    }
    return 0;
}

int update_apply_start(const char *confirm, const char *token, char *err,
                       unsigned int err_size)
{
    gating_result gate = gating_check("update");
    char staged[8], ok[8], current[32];

    if (!gate.ok)
    {
        snprintf(err, err_size, "%s", gate.reason ? gate.reason : "blocked");
        return -1;
    }
    if (!confirm || strcmp(confirm, "APPLY"))
    {
        snprintf(err, err_size, "type APPLY to confirm the update");
        return -1;
    }
    if (!token || !apply_token[0] || strcmp(token, apply_token))
    {
        snprintf(err, err_size,
                 "the confirmation token is invalid; reload the page");
        return -1;
    }
    if (update_busy())
    {
        snprintf(err, err_size, "another update job is running");
        return -1;
    }
    if (!access(UPDATE_PENDING, F_OK))
    {
        snprintf(err, err_size, "an update is already pending");
        return -1;
    }
    staged[0] = ok[0] = '\0';
    read_kv(UPDATE_STAGE, "staged", staged, sizeof(staged));
    read_kv(UPDATE_STAGE, "ok", ok, sizeof(ok));
    if (strcmp(staged, "1") || strcmp(ok, "1"))
    {
        snprintf(err, err_size,
                 "no verified package is staged; upload or download one first");
        return -1;
    }
    current[0] = '\0';
    read_kv(UPDATE_STAGE, "version", current, sizeof(current));

    pthread_mutex_lock(&update_lock);
    memset(&job, 0, sizeof(job));
    snprintf(job.state, sizeof(job.state), "applying");
    snprintf(job.message, sizeof(job.message), "arming the update");
    job.started = (long)time(NULL);
    job.progress = 20;
    busy = 1;
    /* A token works once: the next GET hands out a fresh one.  The helper
     * compares against the token file, so the worker must receive the new
     * token, not the one the client just used. */
    token_generate_locked();
    snprintf(pending_token, sizeof(pending_token), "%s", apply_token);
    pthread_mutex_unlock(&update_lock);

    if (spawn_worker(WORKER_APPLY) != 0)
    {
        snprintf(err, err_size, "cannot start the update worker");
        return -1;
    }
    return 0;
}

int update_clear(char *err, unsigned int err_size)
{
    char tool[PATH_MAX];
    char output[UPDATE_OUT_MAX];

    if (update_busy())
    {
        snprintf(err, err_size, "stop the running update job first");
        return -1;
    }
    if (!access(UPDATE_PENDING, F_OK))
    {
        snprintf(err, err_size, "the update is pending; reboot the device");
        return -1;
    }
    if (tool_path(tool, sizeof(tool)))
    {
        snprintf(err, err_size, "the oko-update helper is missing");
        return -1;
    }
    if (run_capture((char *const[]){ tool, (char *)"clear", NULL }, output,
                    sizeof(output)) != 0)
    {
        trim_message(output);
        snprintf(err, err_size, "%s",
                 output[0] ? output : "the staged files could not be removed");
        return -1;
    }
    job_set("idle", "staged package removed", 0);
    return 0;
}

int update_cancel(void)
{
    int kind;
    pid_t pid;

    pthread_mutex_lock(&update_lock);
    kind = worker_kind;
    pid = worker_pid;
    if (kind == WORKER_APPLY)
    {
        /* The flag and the pending file are already being written; killing
         * the helper between them would arm a flash nobody waits for. */
        pthread_mutex_unlock(&update_lock);
        return -1;
    }
    upload_cancel = 1;
    pthread_mutex_unlock(&update_lock);

    if (pid > 0)
        kill(pid, SIGTERM);
    if (kind == WORKER_NONE)
    {
        /* An upload is owned by the HTTP loop; it notices upload_cancel. */
        if (!update_busy())
            job_finish("idle", "cancelled");
    }
    return 0;
}

char *update_json(void)
{
    char *buffer;
    char state[16], message[256], staged_version[64], staged_parts[32];
    char token_out[32];
    char staged_error[64], last_version[64], last_expected[64];
    char last_error[64], current[64], esc_message[512], esc_current[128];
    char esc_sv[128], esc_sp[64], esc_se[128], esc_lv[128], esc_le[128];
    char esc_lerr[128];
    int progress, staged, staged_ok, last_ok, last_ts, pending, tool;
    long started, finished;
    long long received, total;
    unsigned long long total_bytes = 0, free_bytes = 0;
    int space;
    gating_result gate;
    size_t used;

    buffer = malloc(4096);
    if (!buffer)
        return NULL;
    memset(buffer, 0, 4096);

    pthread_mutex_lock(&update_lock);
    snprintf(state, sizeof(state), "%s", job.state);
    snprintf(message, sizeof(message), "%s", job.message);
    progress = job.progress;
    started = job.started;
    finished = job.finished;
    received = job.received;
    total = job.total;
    snprintf(token_out, sizeof(token_out), "%s", apply_token);
    pthread_mutex_unlock(&update_lock);

    current_version(current, sizeof(current));
    staged = read_int_kv(UPDATE_STAGE, "staged");
    staged_ok = read_int_kv(UPDATE_STAGE, "ok");
    staged_version[0] = staged_parts[0] = staged_error[0] = '\0';
    read_kv(UPDATE_STAGE, "version", staged_version, sizeof(staged_version));
    read_kv(UPDATE_STAGE, "parts", staged_parts, sizeof(staged_parts));
    read_kv(UPDATE_STAGE, "error", staged_error, sizeof(staged_error));
    last_ok = read_int_kv(UPDATE_RESULT, "ok");
    last_ts = read_int_kv(UPDATE_RESULT, "timestamp");
    last_version[0] = last_expected[0] = last_error[0] = '\0';
    read_kv(UPDATE_RESULT, "version", last_version, sizeof(last_version));
    read_kv(UPDATE_RESULT, "expected", last_expected, sizeof(last_expected));
    read_kv(UPDATE_RESULT, "error", last_error, sizeof(last_error));
    pending = access(UPDATE_PENDING, F_OK) == 0;
    {
        char toolbuf[PATH_MAX];
        tool = tool_path(toolbuf, sizeof(toolbuf)) == 0;
    }
    gate = gating_check("update");
    space = gating_sd_space(&total_bytes, &free_bytes);

    json_escape(esc_message, sizeof(esc_message), message);
    json_escape(esc_current, sizeof(esc_current), current);
    json_escape(esc_sv, sizeof(esc_sv), staged_version);
    json_escape(esc_sp, sizeof(esc_sp), staged_parts);
    json_escape(esc_se, sizeof(esc_se), staged_error);
    json_escape(esc_lv, sizeof(esc_lv), last_version);
    json_escape(esc_le, sizeof(esc_le), last_expected);
    json_escape(esc_lerr, sizeof(esc_lerr), last_error);

    used = (size_t)snprintf(buffer, 4096,
        "{\"enabled\":%s,\"sd_mounted\":%s,\"free_mb\":%llu,"
        "\"max_package_mb\":%u,\"current_version\":\"%s\",\"tool\":%s,"
        "\"pending\":%s,\"blocked\":%s,\"reason\":\"%s\",\"hint\":\"%s\","
        "\"staged\":{\"staged\":%s,\"version\":\"%s\",\"parts\":\"%s\","
        "\"ok\":%s,\"error\":\"%s\"},"
        "\"last\":{\"timestamp\":%d,\"ok\":%s,\"version\":\"%s\","
        "\"expected\":\"%s\",\"error\":\"%s\"},"
        "\"job\":{\"state\":\"%s\",\"progress\":%d,\"message\":\"%s\","
        "\"received\":%lld,\"total\":%lld,\"started\":%ld,\"finished\":%ld},"
        "\"apply_token\":\"%s\"}",
        app_config.update_enable ? "true" : "false",
        gating_sd_mounted() ? "true" : "false",
        space ? free_bytes / (1024 * 1024) : 0,
        app_config.update_max_package_mb, esc_current,
        tool ? "true" : "false", pending ? "true" : "false",
        gate.ok ? "false" : "true",
        gate.ok || !gate.reason ? "" : gate.reason,
        gate.ok || !gate.hint ? "" : gate.hint,
        staged ? "true" : "false", esc_sv, esc_sp,
        staged_ok ? "true" : "false", esc_se,
        last_ts, last_ok ? "true" : "false", esc_lv, esc_le, esc_lerr,
        state, progress, esc_message, received, total, started, finished,
        token_out);

    if (used >= 4096 - 64)
    {
        free(buffer);
        return NULL;
    }
    return buffer;
}