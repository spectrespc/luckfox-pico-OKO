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

    if (!app_config.sleep_enable)
    {
        HAL_INFO("sleep", "disabled by [sleep] enable\n");
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

    HAL_INFO("sleep", "suspending: PMU timeout %u ticks (%u s)\n", count,
             app_config.sleep_wake_interval_s);
    int rc = write_power_state();
    if (rc < 0)
    {
        /* EC already knows about SUSPENDING; a failed mem write must not
         * leave the host marked asleep while it keeps running. */
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
    snprintf(out, size,
             "\"sleep\":{\"enabled\":%s,\"state\":\"%s\",\"idle_s\":%ld,"
             "\"last_wake_src\":%d}",
             enabled ? "true" : "false", state_name(state),
             idle > 0 ? idle : 0, last_wake_src);
    pthread_mutex_unlock(&lock);
}