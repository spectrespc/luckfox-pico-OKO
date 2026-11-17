/* sleep_policy.c — сон камеры при отсутствии движения (OKO, [sleep]).
 *
 * Механика проверена на стенде 2026-11-16:
 *   - deep suspend доступен только когда ядро выбрало ROCKCHIP_DT (в root
 *     compatible продукта добавлен "rockchip,rv1106");
 *   - PMU timeout 0xff300048 считает 32 тика в секунду (вендорский
 *     rkadk_aov.c умножает секунды на 32), поэтому 10 с = 320;
 *   - suspend с живым CSI/ISP-пайплайном даёт шторм mipi-csi2-hw ERR1:0x10,
 *     поэтому пайплайн разбирается до suspend и поднимается после wake.
 *
 * После resume процесс перезапускается через execvp (OKO_SLEEP_WAKE=1) —
 * это тот же путь, что SIGHUP-рестарт, уже проверенный на KI-069.  Новый
 * процесс в течение wake_check_s ждёт движение и либо продолжает работу,
 * либо засыпает снова.
 */

#include "sleep_policy.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "app_config.h"
#include "hal/macros.h"

#define PMU_BASE 0xff300000UL
#define PMU_TIMEOUT_OFF 0x048UL
#define PMU_MAP_SIZE 0x1000UL

#define AOV_PROXY_PATH "/sys/devices/platform/rockchip-suspend/aov_wakeproxy"
#define AOV_STATE_PATH "/sys/devices/platform/rockchip-suspend/aov_state"
#define META_IMAGE_PATH "/oem/usr/share/oko/meta.img"
#define META_ADDR 0x1000000UL
#define META_MAP_SIZE 0x60000UL
#define META_TAG 0x544d4b52U /* "RKMT" */

#define EC_BIN "/oem/usr/bin/oko-ec"
#define MEM_SLEEP_PATH "/sys/power/mem_sleep"
#define POWER_STATE_PATH "/sys/power/state"
#define WAKE_INFO_PATH "/tmp/oko-sleep-wake"
#define PROBE_MARKER_PATH "/tmp/oko-sleep-probe"

enum sleep_state
{
    SLEEP_DISABLED = 0,
    SLEEP_AWAKE,
    SLEEP_PROBE,
    SLEEP_SUSPENDING,
    SLEEP_SUSPENDED
};

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t thread;
static volatile int running;
static int thread_started;

static int enabled;
static int platform_ready;
static int probe_mode;
static int request;
static int state = SLEEP_DISABLED;
static int last_wake_src = -1;
static int last_event = -1;
static unsigned long motion_seq;
static unsigned long request_seq;
static unsigned long suspend_seq;
static struct timespec last_motion;
static struct timespec probe_deadline;

static volatile unsigned int *pmu_timeout;

/* AOV wake proxy (kernel CONFIG_RV1106_HPMCU_FAST_WAKEUP). */
static int aov_supported;
static int aov_active;
static char aov_reason[64];
static const char *sleep_reason = "";

static const char *state_name(int value)
{
    switch (value)
    {
    case SLEEP_AWAKE:
        return "awake";
    case SLEEP_PROBE:
        return "probe";
    case SLEEP_SUSPENDING:
        return "suspending";
    case SLEEP_SUSPENDED:
        return "suspended";
    default:
        return "disabled";
    }
}

static struct timespec mono_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts;
}

static long elapsed_s(const struct timespec *since, const struct timespec *now)
{
    return (long)(now->tv_sec - since->tv_sec);
}

static int sysfs_read(const char *path, char *buf, size_t size)
{
    int fd = open(path, O_RDONLY);
    ssize_t n;

    if (fd < 0)
    {
        return -1;
    }
    n = read(fd, buf, size - 1);
    close(fd);
    if (n <= 0)
    {
        return -1;
    }
    buf[n] = '\0';
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
    {
        buf[--n] = '\0';
    }
    return 0;
}

static int sysfs_write_str(const char *path, const char *value)
{
    int fd = open(path, O_WRONLY);
    ssize_t n;

    if (fd < 0)
    {
        return -1;
    }
    n = write(fd, value, strlen(value));
    close(fd);
    return n == (ssize_t)strlen(value) ? 0 : -1;
}

static const char *aov_state_name(void)
{
    static char value[16];

    if (!app_config.sleep_aov_mcu)
    {
        return "off";
    }
    if (!aov_supported)
    {
        return "unsupported";
    }
    if (sysfs_read(AOV_STATE_PATH, value, sizeof(value)) < 0)
    {
        return "off";
    }
    return value;
}

/* The MCU expects the meta image in its loaded layout at 0x1000000: the
 * partition header and items are uncompressed (build_meta --no-compress-iq),
 * so the OEM file can be byte-copied into the reserved region.  This is the
 * AP-side loader chosen after S0.2: the product SPL has no meta support and
 * the 256MB NAND has no room for a separate meta partition. */
static int load_meta_image(void)
{
    struct stat info;
    int fd, mem;
    void *map;
    ssize_t done = 0, n;
    const unsigned int *head;
    int rc = -1;

    fd = open(META_IMAGE_PATH, O_RDONLY);
    if (fd < 0)
    {
        HAL_WARNING("sleep", "AOV meta image %s is missing: %s\n",
                    META_IMAGE_PATH, strerror(errno));
        return -1;
    }
    if (fstat(fd, &info) < 0 || info.st_size <= 0 ||
        (unsigned long)info.st_size > META_MAP_SIZE)
    {
        HAL_WARNING("sleep", "AOV meta image has a bad size\n");
        close(fd);
        return -1;
    }

    map = MAP_FAILED;
    mem = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem >= 0)
    {
        map = mmap(NULL, META_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
                   mem, META_ADDR);
        close(mem);
    }
    if (map == MAP_FAILED)
    {
        HAL_WARNING("sleep", "cannot map AOV meta at 0x%lx: %s\n",
                    META_ADDR, strerror(errno));
        close(fd);
        return -1;
    }

    while (done < info.st_size)
    {
        n = read(fd, (char *)map + done, (size_t)(info.st_size - done));
        if (n <= 0)
        {
            break;
        }
        done += n;
    }
    close(fd);

    head = (const unsigned int *)map;
    if (done != info.st_size || head[0] != META_TAG ||
        head[1] != (unsigned int)META_ADDR || head[3] != 0)
    {
        HAL_WARNING("sleep", "AOV meta layout is invalid (tag/load/compress)\n");
        goto out;
    }

    msync(map, META_MAP_SIZE, MS_SYNC);
    HAL_INFO("sleep", "AOV meta loaded to 0x%lx (%ld bytes)\n",
             META_ADDR, (long)done);
    rc = 0;
out:
    munmap(map, META_MAP_SIZE);
    return rc;
}

/* Probe the kernel/DTS interface even when [sleep] is disabled, so the
 * WebUI can show or hide the AOV switch before the user enables sleep. */
static void aov_probe(void)
{
    char state[16];

    aov_supported = 0;
    if (access(AOV_PROXY_PATH, F_OK) != 0)
    {
        return;
    }
    if (sysfs_read(AOV_STATE_PATH, state, sizeof(state)) == 0 &&
        strcmp(state, "unsupported") == 0)
    {
        return;
    }
    aov_supported = 1;
}

static void aov_prepare(void)
{
    aov_active = 0;
    aov_reason[0] = '\0';

    if (!app_config.sleep_aov_mcu)
    {
        /* Clear a proxy left armed by a previous configuration. */
        if (aov_supported)
        {
            (void)sysfs_write_str(AOV_PROXY_PATH, "0\n");
        }
        return;
    }

    if (!aov_supported)
    {
        snprintf(aov_reason, sizeof(aov_reason),
                 "AOV is not supported by the kernel or DTS");
        HAL_WARNING("sleep", "AOV requested but the kernel/DTS does not "
                    "provide a wake proxy\n");
        return;
    }

    if (load_meta_image() < 0)
    {
        snprintf(aov_reason, sizeof(aov_reason),
                 "AOV meta image is missing or invalid");
        return;
    }

    aov_active = 1;
    (void)sysfs_write_str(AOV_PROXY_PATH, "0\n");
    HAL_INFO("sleep", "AOV wake proxy is available\n");
}

static int map_pmu(void)
{
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0)
    {
        HAL_WARNING("sleep", "open /dev/mem failed: %s\n", strerror(errno));
        return -1;
    }

    void *map = mmap(NULL, PMU_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                     PMU_BASE);
    close(fd);
    if (map == MAP_FAILED)
    {
        HAL_WARNING("sleep", "mmap PMU failed: %s\n", strerror(errno));
        return -1;
    }

    pmu_timeout = (volatile unsigned int *)((char *)map + PMU_TIMEOUT_OFF);
    return 0;
}

/* deep должен быть доступен и выбран; s2idle не будит по PMU timeout. */
static int select_deep(void)
{
    int fd = open(MEM_SLEEP_PATH, O_RDWR);
    if (fd < 0)
    {
        HAL_WARNING("sleep", "open %s failed: %s\n", MEM_SLEEP_PATH,
                    strerror(errno));
        return -1;
    }

    if (write(fd, "deep\n", 5) < 0 && errno != EBUSY)
    {
        HAL_WARNING("sleep", "cannot select deep: %s\n", strerror(errno));
        close(fd);
        return -1;
    }
    close(fd);

    char buf[64] = {0};
    fd = open(MEM_SLEEP_PATH, O_RDONLY);
    if (fd >= 0)
    {
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n > 0 && !strstr(buf, "[deep]"))
        {
            HAL_WARNING("sleep", "deep suspend is not available (%s)\n", buf);
            return -1;
        }
    }
    return 0;
}

static const char *ec_bin(void)
{
    return access(EC_BIN, X_OK) == 0 ? EC_BIN : "oko-ec";
}

static void ec_set_state(const char *name)
{
    char cmd[160];

    snprintf(cmd, sizeof(cmd), "%s --quiet power --state %s >/dev/null 2>&1",
             ec_bin(), name);
    if (system(cmd) != 0)
    {
        HAL_WARNING("sleep", "oko-ec power --state %s failed\n", name);
    }
}

/* GET_POWER после resume: причина/событие нужны в логе и /api/status.
 * last_wake_src — источник пробуждения самого STM32, не AP; для PMU-wake
 * он обычно 0, но кнопка видна как event_count/last_event. */
static void ec_read_power(void)
{
    char cmd[128];
    char line[512];

    snprintf(cmd, sizeof(cmd), "%s --json power 2>/dev/null", ec_bin());
    FILE *pipe = popen(cmd, "r");
    if (!pipe)
    {
        return;
    }
    if (!fgets(line, sizeof(line), pipe))
    {
        pclose(pipe);
        return;
    }
    pclose(pipe);

    const char *src = strstr(line, "\"last_wake_src\":");
    const char *event = strstr(line, "\"last_event\":");
    if (src)
    {
        (void)sscanf(src + strlen("\"last_wake_src\":"), "%d", &last_wake_src);
    }
    if (event)
    {
        (void)sscanf(event + strlen("\"last_event\":"), "%d", &last_event);
    }
}

/* Переживает exec-рестарт: причина пробуждения видна в /api/status. */
static void save_wake_info(void)
{
    FILE *file = fopen(WAKE_INFO_PATH, "w");
    if (!file)
    {
        return;
    }
    fprintf(file, "%d %d\n", last_wake_src, last_event);
    fclose(file);
}

static void load_wake_info(void)
{
    FILE *file = fopen(WAKE_INFO_PATH, "r");
    if (!file)
    {
        return;
    }
    if (fscanf(file, "%d %d", &last_wake_src, &last_event) != 2)
    {
        last_wake_src = -1;
        last_event = -1;
    }
    fclose(file);
}

static int write_power_state(void)
{
    int fd = open(POWER_STATE_PATH, O_WRONLY);
    if (fd < 0)
    {
        HAL_WARNING("sleep", "open %s failed: %s\n", POWER_STATE_PATH,
                    strerror(errno));
        return -1;
    }

    ssize_t n = write(fd, "mem\n", 4);
    int saved = errno;
    close(fd);
    if (n != 4)
    {
        HAL_WARNING("sleep", "suspend write failed: %s\n", strerror(saved));
        return -1;
    }
    return 0;
}

static void *policy_thread(void *arg)
{
    (void)arg;

    while (running)
    {
        struct timespec now = mono_now();

        pthread_mutex_lock(&lock);
        if (enabled)
        {
            if (state == SLEEP_PROBE)
            {
                if (now.tv_sec >= probe_deadline.tv_sec)
                {
                    request = 1;
                    request_seq = motion_seq;
                    state = SLEEP_SUSPENDING;
                    HAL_INFO("sleep", "wake window is quiet; suspending again\n");
                }
            }
            else if (state == SLEEP_AWAKE)
            {
                long idle = elapsed_s(&last_motion, &now);
                if (idle >= (long)app_config.sleep_idle_timeout_s)
                {
                    request = 1;
                    request_seq = motion_seq;
                    state = SLEEP_SUSPENDING;
                    HAL_INFO("sleep", "idle for %ld s; suspending\n", idle);
                }
            }
        }
        pthread_mutex_unlock(&lock);

        usleep(250000);
    }
    return NULL;
}

void sleep_policy_init(void)
{
    struct timespec now = mono_now();

    last_motion = now;
    state = SLEEP_DISABLED;
    load_wake_info();
    aov_probe();

    if (!app_config.sleep_enable)
    {
        if (aov_supported)
        {
            (void)sysfs_write_str(AOV_PROXY_PATH, "0\n");
        }
        HAL_INFO("sleep", "disabled by [sleep] enable\n");
        return;
    }

    aov_prepare();

    /* Guard: a sleep with no way to wake the AP is forbidden.  The MCU is
     * released only after a PMU timeout wake, so AOV alone (pmu_timer_wake=0)
     * cannot serve as the wake source on this design (S0.4). */
    if (!app_config.sleep_pmu_timer_wake)
    {
        sleep_reason = "no_wake_source";
        HAL_WARNING("sleep", "enable=1 needs pmu_timer_wake or a supported "
                    "AOV wake source; disabled\n");
        return;
    }

    if (app_config.sleep_wake_interval_s == 0 ||
        app_config.sleep_pmu_timeout_hz == 0)
    {
        HAL_WARNING("sleep", "invalid wake_interval_s/pmu_timeout_hz; disabled\n");
        return;
    }

    if (map_pmu() < 0 || select_deep() < 0)
    {
        HAL_WARNING("sleep", "platform is not ready for deep suspend; disabled\n");
        return;
    }

    platform_ready = 1;
    enabled = 1;

    const char *wake = getenv("OKO_SLEEP_WAKE");
    int marker = access(PROBE_MARKER_PATH, F_OK) == 0;
    HAL_INFO("sleep", "init: wake env %s, probe marker %s, idle=%u s\n",
             (wake && *wake) ? "yes" : "no", marker ? "yes" : "no",
             app_config.sleep_idle_timeout_s);
    if (marker)
    {
        unlink(PROBE_MARKER_PATH);
    }
    if ((wake && *wake) || marker)
    {
        unsetenv("OKO_SLEEP_WAKE");
        probe_mode = 1;
        state = SLEEP_PROBE;
        HAL_INFO("sleep", "wake probe: will wait %u s for motion\n",
                 app_config.sleep_wake_check_s);
    }
    else
    {
        state = SLEEP_AWAKE;
        HAL_INFO("sleep", "armed: idle_timeout_s=%u, wake_interval_s=%u, "
                 "wake_check_s=%u, pmu_timeout_hz=%u\n",
                 app_config.sleep_idle_timeout_s,
                 app_config.sleep_wake_interval_s,
                 app_config.sleep_wake_check_s,
                 app_config.sleep_pmu_timeout_hz);
    }
}

void sleep_policy_mark_wake(void)
{
    FILE *file = fopen(PROBE_MARKER_PATH, "w");
    if (file)
    {
        fclose(file);
    }
    else
    {
        HAL_WARNING("sleep", "cannot create %s: %s\n", PROBE_MARKER_PATH,
                    strerror(errno));
    }
}

void sleep_policy_start(void)
{
    if (!enabled || thread_started)
    {
        return;
    }

    /* The pipeline is up now: start the idle/probe clock here, not in init
     * (startup itself takes seconds and must not consume wake_check_s). */
    pthread_mutex_lock(&lock);
    last_motion = mono_now();
    if (probe_mode)
    {
        probe_deadline = last_motion;
        probe_deadline.tv_sec += (time_t)app_config.sleep_wake_check_s;
    }
    pthread_mutex_unlock(&lock);

    running = 1;
    if (pthread_create(&thread, NULL, policy_thread, NULL) != 0)
    {
        running = 0;
        HAL_WARNING("sleep", "cannot start the policy thread\n");
        return;
    }
    thread_started = 1;
}

void sleep_policy_stop(void)
{
    if (!thread_started)
    {
        return;
    }

    running = 0;
    pthread_join(thread, NULL);
    thread_started = 0;
}

void sleep_policy_report_motion(void)
{
    if (!enabled)
    {
        return;
    }

    pthread_mutex_lock(&lock);
    motion_seq++;
    last_motion = mono_now();
    if (state == SLEEP_PROBE)
    {
        state = SLEEP_AWAKE;
        HAL_INFO("sleep", "motion in the wake window; stream stays on\n");
    }
    pthread_mutex_unlock(&lock);
}

int sleep_policy_should_suspend(void)
{
    int result = 0;

    if (!enabled || !platform_ready)
    {
        return 0;
    }

    pthread_mutex_lock(&lock);
    if (request)
    {
        if (motion_seq != request_seq)
        {
            /* Движение пришло уже после решения: отменяем вход в сон. */
            request = 0;
            state = SLEEP_AWAKE;
            HAL_INFO("sleep", "motion during suspend entry; cancelled\n");
        }
        else
        {
            request = 0;
            suspend_seq = motion_seq;
            result = 1;
        }
    }
    pthread_mutex_unlock(&lock);

    return result;
}

int sleep_policy_suspend(void)
{
    unsigned int count;

    if (!enabled || !platform_ready)
    {
        return -1;
    }

    pthread_mutex_lock(&lock);
    if (motion_seq != suspend_seq)
    {
        /* The teardown takes a moment; motion in that window must win. */
        state = SLEEP_AWAKE;
        last_motion = mono_now();
        pthread_mutex_unlock(&lock);
        HAL_INFO("sleep", "motion during teardown; suspend cancelled\n");
        return -1;
    }
    state = SLEEP_SUSPENDED;
    pthread_mutex_unlock(&lock);

    sync();
    ec_set_state("suspending");

    int aov_armed = 0;
    if (aov_active)
    {
        if (sysfs_write_str(AOV_PROXY_PATH, "1\n") == 0)
        {
            aov_armed = 1;
        }
        else
        {
            HAL_WARNING("sleep", "cannot arm aov_wakeproxy; "
                        "continuing with the PMU timer only\n");
            aov_active = 0;
        }
    }

    count = app_config.sleep_wake_interval_s * app_config.sleep_pmu_timeout_hz;
    if (count == 0)
    {
        count = 1;
    }
    if (!pmu_timeout)
    {
        return -1;
    }
    *pmu_timeout = count;
    __sync_synchronize();

    HAL_INFO("sleep", "suspending: PMU timeout %u ticks (%u s), aov %s\n",
             count, app_config.sleep_wake_interval_s,
             aov_armed ? "armed" : "off");
    int rc = write_power_state();
    if (rc < 0)
    {
        /* EC already knows about SUSPENDING; a failed mem write must not
         * leave the host marked asleep while it keeps running. */
        if (aov_armed)
        {
            (void)sysfs_write_str(AOV_PROXY_PATH, "0\n");
        }
        ec_set_state("awake");
        pthread_mutex_lock(&lock);
        state = SLEEP_AWAKE;
        last_motion = mono_now();
        pthread_mutex_unlock(&lock);
        return -1;
    }

    ec_set_state("awake");
    ec_read_power();
    save_wake_info();

    pthread_mutex_lock(&lock);
    state = SLEEP_AWAKE;
    last_motion = mono_now();
    motion_seq++;
    pthread_mutex_unlock(&lock);

    HAL_INFO("sleep", "resumed (EC wake src %d, last event %d)\n",
             last_wake_src, last_event);
    return 0;
}

int sleep_policy_aov_available(void)
{
    return aov_supported;
}

void sleep_policy_api_json(char *out, size_t size)
{
    long idle = 0;

    if (!out || !size)
    {
        return;
    }

    pthread_mutex_lock(&lock);
    if (enabled)
    {
        struct timespec now = mono_now();
        idle = elapsed_s(&last_motion, &now);
    }
    const char *reason = state == SLEEP_DISABLED && *sleep_reason
        ? sleep_reason : aov_reason;

    snprintf(out, size,
             "{\"enable\":%s,\"pmu_timer_wake\":%s,\"aov_mcu\":%s,"
             "\"aov_available\":%s,\"aov_state\":\"%s\","
             "\"idle_timeout_s\":%u,\"wake_interval_s\":%u,"
             "\"wake_check_s\":%u,\"pmu_timeout_hz\":%u,"
             "\"enabled\":%s,\"state\":\"%s\",\"idle_s\":%ld,"
             "\"last_wake_src\":%d,\"reason\":\"%s\"}",
             app_config.sleep_enable ? "true" : "false",
             app_config.sleep_pmu_timer_wake ? "true" : "false",
             app_config.sleep_aov_mcu ? "true" : "false",
             aov_supported ? "true" : "false", aov_state_name(),
             app_config.sleep_idle_timeout_s,
             app_config.sleep_wake_interval_s,
             app_config.sleep_wake_check_s,
             app_config.sleep_pmu_timeout_hz,
             enabled ? "true" : "false", state_name(state),
             idle > 0 ? idle : 0, last_wake_src, reason);
    pthread_mutex_unlock(&lock);
}

void sleep_policy_status_json(char *out, size_t size)
{
    long idle = 0;

    if (!out || !size)
    {
        return;
    }

    pthread_mutex_lock(&lock);
    if (enabled)
    {
        struct timespec now = mono_now();
        idle = elapsed_s(&last_motion, &now);
    }
    const char *reason = state == SLEEP_DISABLED && *sleep_reason
        ? sleep_reason : aov_reason;

    snprintf(out, size,
             "\"sleep\":{\"enabled\":%s,\"state\":\"%s\","
             "\"pmu_timer\":%s,\"aov\":%s,\"aov_available\":%s,"
             "\"aov_state\":\"%s\",\"idle_s\":%ld,"
             "\"last_wake_src\":%d,\"reason\":\"%s\"}",
             enabled ? "true" : "false", state_name(state),
             app_config.sleep_pmu_timer_wake ? "true" : "false",
             aov_active ? "true" : "false",
             aov_supported ? "true" : "false", aov_state_name(),
             idle > 0 ? idle : 0, last_wake_src, reason);
    pthread_mutex_unlock(&lock);
}