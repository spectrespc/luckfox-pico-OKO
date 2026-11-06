#include "firmware_status.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "app_config.h"
#include "focus_motor.h"

#define EC_JSON "/tmp/oko-ec.json"
#define EC_JSON_MAX 65536

typedef struct {
    int present;
    char state[8];                 /* app, boot, absent */
    char version[16];
    int proto;
    int device_type;
    long age;
} fw_stm32;

typedef struct {
    int present;
    char state[8];
    char version[16];
    char boot_version[16];
    int address;
    long age;
} fw_stm8;

static pthread_mutex_t fw_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t poll_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t fw_thread;
static int fw_running;
static fw_stm32 stm32;
static fw_stm8 stm8;
static time_t stm32_seen, stm8_seen;

static int json_uint(const char *text, const char *key, unsigned long *value)
{
    char pattern[48];
    const char *p;
    char *end = NULL;
    unsigned long v;

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    p = strstr(text, pattern);
    if (!p)
        return -1;
    p = strchr(p + strlen(pattern), ':');
    if (!p)
        return -1;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    v = strtoul(p, &end, 0);
    if (end == p)
        return -1;
    *value = v;
    return 0;
}

static void poll_stm32(void)
{
    fw_stm32 next;
    char *buffer;
    FILE *file;
    size_t n;
    unsigned long version = 0, device_type = 0, proto = 0, info_ok = 0;
    unsigned long ts = 0;

    memset(&next, 0, sizeof(next));
    strcpy(next.state, "absent");

    file = fopen(EC_JSON, "rb");
    if (!file)
        goto done;
    buffer = malloc(EC_JSON_MAX);
    if (!buffer)
    {
        fclose(file);
        goto done;
    }
    n = fread(buffer, 1, EC_JSON_MAX - 1, file);
    fclose(file);
    buffer[n] = '\0';

    if (json_uint(buffer, "version", &version) == 0 &&
        json_uint(buffer, "device_type", &device_type) == 0)
    {
        next.present = 1;
        next.version[0] = '\0';
        snprintf(next.version, sizeof(next.version), "%lu.%lu.%lu",
                 (version >> 16) & 0xFF, (version >> 8) & 0xFF, version & 0xFF);
        next.device_type = (int)device_type;
        if (json_uint(buffer, "proto", &proto) == 0)
            next.proto = (int)proto;
        if (json_uint(buffer, "info_ok", &info_ok) == 0 && !info_ok)
            next.present = 0;
        strcpy(next.state, device_type == 1 ? "boot" :
            (device_type == 2 ? "app" : "absent"));
        if (!next.present)
            strcpy(next.state, "absent");
    }
    /* The daemon writes the snapshot with its own timestamp.  A stale file
     * means the daemon is stopped or the controller is gone, so the
     * presence flag must not survive forever. */
    if (next.present && json_uint(buffer, "ts", &ts) == 0 && ts > 0)
    {
        time_t now = time(NULL);
        if (now > (time_t)ts && (unsigned long)(now - (time_t)ts) > 15)
        {
            next.present = 0;
            strcpy(next.state, "absent");
        }
    }
    free(buffer);

done:
    pthread_mutex_lock(&fw_lock);
    if (next.present)
        stm32_seen = ts > 0 ? (time_t)ts : time(NULL);
    next.age = stm32_seen ? (long)(time(NULL) - stm32_seen) : -1;
    if (next.age < 0)
        next.age = 0;
    stm32 = next;
    pthread_mutex_unlock(&fw_lock);
}

static void poll_stm8(void)
{
    fw_stm8 next;
    uint8_t info[8];
    uint8_t version[3];

    memset(&next, 0, sizeof(next));
    strcpy(next.state, "absent");
    next.address = (int)app_config.focus_address;

    if (focus_motor_suspended() || focus_motor_busy())
    {
        pthread_mutex_lock(&fw_lock);
        stm8.age = stm8_seen ? (long)(time(NULL) - stm8_seen) : -1;
        pthread_mutex_unlock(&fw_lock);
        return;
    }

    {
        int rc = focus_motor_read_app_version(version);

        if (rc == FM_ERR_BUSY || rc == FM_ERR_SUSPENDED)
            return;                 /* keep the previous snapshot */
        if (rc == FM_OK)
        {
            next.present = 1;
            strcpy(next.state, "app");
            snprintf(next.version, sizeof(next.version), "%u.%u.%u",
                     version[0], version[1], version[2]);
        }
        else
        {
            /* Only a device that does not answer the application protocol
             * needs the bootloader probe; 0x10 must not spam an app. */
            rc = focus_motor_read_boot_info(info);
            if (rc == FM_ERR_BUSY || rc == FM_ERR_SUSPENDED)
                return;
            if (rc == FM_OK)
            {
                next.present = 1;
                strcpy(next.state, "boot");
                snprintf(next.boot_version, sizeof(next.boot_version), "%u.%u.%u",
                         info[0], info[1], info[2]);
                snprintf(next.version, sizeof(next.version), "%u.%u.%u",
                         info[3], info[4], info[5]);
            }
        }
    }

    pthread_mutex_lock(&fw_lock);
    if (next.present)
        stm8_seen = time(NULL);
    next.age = stm8_seen ? (long)(time(NULL) - stm8_seen) : -1;
    stm8 = next;
    pthread_mutex_unlock(&fw_lock);
}

static void poll_all(void)
{
    pthread_mutex_lock(&poll_lock);
    poll_stm32();
    poll_stm8();
    pthread_mutex_unlock(&poll_lock);
}

static void *fw_thread_fn(void *arg)
{
    (void)arg;
    while (1)
    {
        int i;

        for (i = 0; i < 25 && fw_running; i++)
            usleep(200000);
        if (!fw_running)
            break;
        poll_all();
    }
    return NULL;
}

int firmware_status_start(void)
{
    if (fw_running)
        return 0;
    fw_running = 1;
    if (pthread_create(&fw_thread, NULL, fw_thread_fn, NULL))
    {
        fw_running = 0;
        return -1;
    }
    poll_all();
    return 0;
}

void firmware_status_stop(void)
{
    if (!fw_running)
        return;
    fw_running = 0;
    pthread_join(fw_thread, NULL);
}

void firmware_status_refresh(void)
{
    poll_all();
}

char *firmware_status_json(void)
{
    char *buffer = malloc(2048);
    fw_stm32 ec;
    fw_stm8 mo;

    if (!buffer)
        return NULL;

    pthread_mutex_lock(&fw_lock);
    ec = stm32;
    mo = stm8;
    pthread_mutex_unlock(&fw_lock);

    {
        char v32[20], v8[20], b8[20];

        if (ec.version[0])
            snprintf(v32, sizeof(v32), "\"%s\"", ec.version);
        else
            snprintf(v32, sizeof(v32), "null");
        if (mo.version[0])
            snprintf(v8, sizeof(v8), "\"%s\"", mo.version);
        else
            snprintf(v8, sizeof(v8), "null");
        if (mo.boot_version[0])
            snprintf(b8, sizeof(b8), "\"%s\"", mo.boot_version);
        else
            snprintf(b8, sizeof(b8), "null");

        snprintf(buffer, 2048,
            "{\"stm32\":{\"present\":%s,\"state\":\"%s\","
            "\"version\":%s,\"proto\":%d,\"device_type\":%d,\"age\":%ld},"
            "\"stm8\":{\"present\":%s,\"state\":\"%s\","
            "\"version\":%s,\"boot_version\":%s,"
            "\"address\":%d,\"age\":%ld,\"stale\":%s}}",
            ec.present ? "true" : "false", ec.state, v32, ec.proto,
            ec.device_type, ec.age,
            mo.present ? "true" : "false", mo.state, v8, b8,
            mo.address, mo.age, focus_motor_suspended() ? "true" : "false");
    }
    return buffer;
}
void firmware_status_stm32(int *present, char *state, size_t state_size,
                           char *version, size_t version_size, long *age)
{
    fw_stm32 copy;

    pthread_mutex_lock(&fw_lock);
    copy = stm32;
    pthread_mutex_unlock(&fw_lock);
    if (present) *present = copy.present;
    if (state && state_size) snprintf(state, state_size, "%s", copy.state);
    if (version && version_size) snprintf(version, version_size, "%s",
                                          copy.version);
    if (age) *age = copy.age;
}

void firmware_status_stm8(int *present, char *state, size_t state_size,
                          char *version, size_t version_size,
                          char *boot_version, size_t boot_size, long *age)
{
    fw_stm8 copy;

    pthread_mutex_lock(&fw_lock);
    copy = stm8;
    pthread_mutex_unlock(&fw_lock);
    if (present) *present = copy.present;
    if (state && state_size) snprintf(state, state_size, "%s", copy.state);
    if (version && version_size) snprintf(version, version_size, "%s",
                                          copy.version);
    if (boot_version && boot_size) snprintf(boot_version, boot_size, "%s",
                                            copy.boot_version);
    if (age) *age = copy.age;
}
