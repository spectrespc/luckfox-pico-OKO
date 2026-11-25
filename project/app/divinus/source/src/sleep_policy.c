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
#include "focus_motor.h"
#include "hal/macros.h"
#include "heartbeat_task.h"
#include "sleep_logic.h"

#define PMU_BASE 0xff300000UL
#define PMU_TIMEOUT_OFF 0x048UL
#define PMU_MAP_SIZE 0x1000UL

#define AOV_PROXY_PATH "/sys/devices/platform/rockchip-suspend/aov_wakeproxy"
#define AOV_STATE_PATH "/sys/devices/platform/rockchip-suspend/aov_state"
#define AOV_HEARTBEAT_WAKES_PATH \
    "/sys/devices/platform/rockchip-suspend/aov_heartbeat_wakes"
#define AOV_TIMEOUT_STREAK_PATH \
    "/sys/devices/platform/rockchip-suspend/aov_timeout_streak"
/* A tier-2 resume restarts the process; the new instance sends the heartbeat
 * even though CLOCK_MONOTONIC did not advance across the suspended interval. */
#define HEARTBEAT_MARKER_PATH "/tmp/oko-sleep-heartbeat"
/* Survives the exec restart so /api/sleep can explain why AOV was disarmed. */
#define AOV_HEAL_MARKER_PATH "/tmp/oko-sleep-mcu-unresponsive"
/* The HCD loses power in deep suspend; with [sleep] usb_on_motion the host is
 * unbound before mem and bound again only when a wake needs WiFi/streaming. */
#define XHCI_DRIVER_DIR "/sys/bus/platform/drivers/xhci-hcd"
#define XHCI_DEVICE "xhci-hcd.0.auto"
#define META_IMAGE_PATH "/oem/usr/share/oko/meta.img"
#define META_ADDR 0x1000000UL
#define META_MAP_SIZE 0x60000UL
#define META_TAG 0x544d4b52U /* "RKMT" */

#define EC_BIN "/oem/usr/bin/oko-ec"
#define MEM_SLEEP_PATH "/sys/power/mem_sleep"
#define POWER_STATE_PATH "/sys/power/state"
#define WAKE_INFO_PATH "/tmp/oko-sleep-wake"

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
/* Origin of the pending/accepted suspend request: 1 when the quiet SLEEP_PROBE
 * window expired, 0 for the normal idle timeout. */
static int quiet_request;
static int quiet_suspend;
/* Set by a tier-2 kernel wake, consumed by main after the probe window. */
static int heartbeat_wake;
/* motion_seq at the last resume: an in-process wake window must not override a
 * motion report that arrived after it. */
static unsigned long wake_seq;
static struct timespec last_motion;
static struct timespec probe_deadline;

static volatile unsigned int *pmu_timeout;

/* AOV wake proxy (kernel CONFIG_RV1106_HPMCU_FAST_WAKEUP). */
static int aov_supported;
static int aov_active;
static int aov_healed;
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

static int sysfs_write_uint(const char *path, unsigned int value)
{
    char buf[16];

    snprintf(buf, sizeof(buf), "%u\n", value);
    return sysfs_write_str(path, buf);
}

static unsigned int sysfs_read_uint(const char *path)
{
    char buf[16];
    unsigned long value;

    if (sysfs_read(path, buf, sizeof(buf)) < 0)
    {
        return 0;
    }
    value = strtoul(buf, NULL, 10);
    return value > 0xffffffffUL ? 0xffffffffU : (unsigned int)value;
}

static int usb_host_bound(void)
{
    return access(XHCI_DRIVER_DIR "/" XHCI_DEVICE, F_OK) == 0;
}

static int usb_host_set(int bind)
{
    char path[80];

    if (usb_host_bound() == bind)
    {
        return 0;
    }
    snprintf(path, sizeof(path), "%s/%s", XHCI_DRIVER_DIR,
             bind ? "bind" : "unbind");
    if (sysfs_write_str(path, XHCI_DEVICE "\n") < 0)
    {
        HAL_WARNING("sleep", "cannot %s the USB host: %s\n",
                    bind ? "bind" : "unbind", strerror(errno));
        return -1;
    }
    HAL_INFO("sleep", "USB host %s\n", bind ? "bound" : "unbound");
    if (bind && access("/usr/bin/oko-wifi", X_OK) == 0)
    {
        /* wlan0 disappears with the HCD; re-apply the WiFi configuration so
         * the AP/STA comes back without waiting for the next boot. */
        (void)system("/usr/bin/oko-wifi apply >/dev/null 2>&1");
    }
    return 0;
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
        if (aov_healed)
        {
            snprintf(aov_reason, sizeof(aov_reason), "mcu_unresponsive");
        }
        /* Clear a proxy left armed by a previous configuration. */
        if (aov_supported)
        {
            (void)sysfs_write_str(AOV_PROXY_PATH, "0\n");
            (void)sysfs_write_uint(AOV_HEARTBEAT_WAKES_PATH, 0);
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
    (void)sysfs_write_uint(AOV_HEARTBEAT_WAKES_PATH, 0);
    unlink(AOV_HEAL_MARKER_PATH);
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
                    quiet_request = 1;
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
                    quiet_request = 0;
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
    heartbeat_wake = access(HEARTBEAT_MARKER_PATH, F_OK) == 0;
    if (heartbeat_wake)
    {
        unlink(HEARTBEAT_MARKER_PATH);
    }
    aov_healed = access(AOV_HEAL_MARKER_PATH, F_OK) == 0;
    /* Recover a host left unbound by a previous configuration or a failed
     * suspend; with the feature off USB must be available from boot. */
    if (!app_config.sleep_usb_on_motion && !usb_host_bound())
    {
        (void)usb_host_set(1);
    }
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
    int marker = access(SLEEP_PROBE_MARKER_PATH, F_OK) == 0;
    HAL_INFO("sleep", "init: wake env %s, probe marker %s, idle=%u s\n",
             (wake && *wake) ? "yes" : "no", marker ? "yes" : "no",
             app_config.sleep_idle_timeout_s);
    if (marker)
    {
        unlink(SLEEP_PROBE_MARKER_PATH);
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

void sleep_policy_begin_wake_window(void)
{
    if (!enabled)
    {
        return;
    }

    pthread_mutex_lock(&lock);
    probe_mode = 1;
    last_motion = mono_now();
    if (motion_seq != wake_seq)
    {
        /* IVS/event path already reported motion after resume. */
        state = SLEEP_AWAKE;
    }
    else
    {
        state = SLEEP_PROBE;
        probe_deadline = last_motion;
        probe_deadline.tv_sec += (time_t)app_config.sleep_wake_check_s;
    }
    pthread_mutex_unlock(&lock);
}

void sleep_policy_clear_heartbeat_wake(void)
{
    heartbeat_wake = 0;
    unlink(HEARTBEAT_MARKER_PATH);
}

void sleep_policy_mark_wake(void)
{
    FILE *file = fopen(SLEEP_PROBE_MARKER_PATH, "w");
    if (file)
    {
        fclose(file);
    }
    else
    {
        HAL_WARNING("sleep", "cannot create %s: %s\n", SLEEP_PROBE_MARKER_PATH,
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

    /* A motion wake without AOV (or a motion detected in the probe window)
     * needs the network/streaming path, so bring the host back. */
    if (app_config.sleep_usb_on_motion && !usb_host_bound())
    {
        (void)usb_host_set(1);
    }

    /* Confirmed motion is delivered by the event path; rebase the heartbeat
     * interval so the next quiet cycle does not add a duplicate frame. */
    heartbeat_set_due();
    heartbeat_wake = 0;
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
            quiet_request = 0;
            state = SLEEP_AWAKE;
            HAL_INFO("sleep", "motion during suspend entry; cancelled\n");
        }
        else
        {
            request = 0;
            quiet_suspend = quiet_request;
            quiet_request = 0;
            suspend_seq = motion_seq;
            result = 1;
        }
    }
    pthread_mutex_unlock(&lock);

    return result;
}

int sleep_policy_cycle_is_quiet_resume(void)
{
    return quiet_suspend;
}

int sleep_policy_cycle_is_heartbeat(void)
{
    return heartbeat_wake;
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

    /* The PMU wake timeout counts ticks of the 32 kHz sleep clock, so a
     * 60 s interval is 1 966 080 ticks (the counter is 32-bit).  A stale
     * "32 ticks/s" config used to turn every interval into a 60 ms sleep;
     * app_config_parse() migrates that legacy value. */
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

    /* Tier 2: let the kernel return to Linux once every N quiet cycles.  The
     * kernel counts those cycles itself (CLOCK_MONOTONIC does not advance
     * while the AP is suspended), so N is the whole policy handover. */
    if (aov_armed)
    {
        unsigned int wakes = app_config.sleep_heartbeat
            ? sleep_heartbeat_wakes(app_config.sleep_heartbeat_interval_s,
                                    app_config.sleep_wake_interval_s)
            : 0;

        if (sysfs_write_uint(AOV_HEARTBEAT_WAKES_PATH, wakes) < 0)
            HAL_WARNING("sleep", "cannot program aov_heartbeat_wakes; "
                        "userspace stays asleep across quiet cycles\n");
        else
            HAL_INFO("sleep", "tier 2: %u quiet cycle(s) per userspace wake\n",
                     wakes);
    }

    /* Put the STM8 focus/zoom controller into its I2C-idle sleep chain while
       the AP is suspended.  Best effort: a missing motor controller must not
       block the camera sleep path. */
    if (app_config.focus_motor && app_config.motor_sleep_enable)
    {
        int src = focus_motor_set_sleep(1, app_config.motor_sleep_idle_s);

        if (src != FM_OK)
            HAL_WARNING("sleep", "cannot enable STM8 I2C-idle sleep: %s\n",
                        focus_motor_error(src));
        else
            HAL_INFO("sleep", "STM8 I2C-idle sleep enabled (idle %u s)\n",
                     app_config.motor_sleep_idle_s);
    }

    HAL_INFO("sleep", "suspending: PMU timeout %u ticks (%u s), aov %s\n",
             count, app_config.sleep_wake_interval_s,
             aov_armed ? "armed" : "off");

    /* The USB host loses power in deep suspend anyway; unbinding it here
     * keeps the hub and the WiFi dongle out of the resume path.  A failed
     * unbind only costs the old resume time, so it is never fatal. */
    if (app_config.sleep_usb_on_motion)
    {
        (void)usb_host_set(0);
    }

    int rc = write_power_state();
    if (rc < 0)
    {
        /* EC already knows about SUSPENDING; a failed mem write must not
         * leave the host marked asleep while it keeps running. */
        if (aov_armed)
        {
            (void)sysfs_write_str(AOV_PROXY_PATH, "0\n");
        }
        if (app_config.sleep_usb_on_motion)
        {
            (void)usb_host_set(1);
        }
        ec_set_state("awake");
        pthread_mutex_lock(&lock);
        state = SLEEP_AWAKE;
        last_motion = mono_now();
        pthread_mutex_unlock(&lock);
        return -1;
    }

    /* Resume: turn the STM8 sleep off and prove that the controller answers
       again before anything else talks to it. */
    if (app_config.focus_motor)
    {
        uint8_t version[3];
        int src = focus_motor_set_sleep(0, 0);

        if (src != FM_OK)
            HAL_WARNING("sleep", "cannot disable STM8 sleep after resume: %s\n",
                        focus_motor_error(src));
        if (focus_motor_read_app_version(version) != FM_OK)
            HAL_WARNING("sleep", "STM8 did not answer after resume\n");
    }

    ec_set_state("awake");
    ec_read_power();
    save_wake_info();

    /*
     * Publish the MCU decision while the kernel still has it: the vendor
     * path keeps aov_state until the proxy is disarmed, so this is the only
     * place that can tell a healthy handshake (`quiet`/`motion`) from the
     * 2 s mailbox timeout without a live UART - the -h-wait-/-h-mbox-/
     * -h-timeout- markers are printed by rkpm_printstr and bypass printk.
     */
    if (aov_armed)
    {
        char aov_state[16] = "?";

        if (sysfs_read(AOV_STATE_PATH, aov_state, sizeof(aov_state)) < 0)
        {
            snprintf(aov_state, sizeof(aov_state), "?");
        }
        else
        {
            aov_state[strcspn(aov_state, "\n")] = '\0';
        }

        if (sleep_aov_state_is_heartbeat(aov_state))
        {
            /* Tier 2: the kernel woke Linux for telemetry, not for motion.
             * Persist it for the exec-restarted instance; CLOCK_MONOTONIC
             * skipped the suspended interval, so the normal due-gate would
             * suppress the very frame this wake exists for. */
            heartbeat_wake = 1;
            FILE *marker = fopen(HEARTBEAT_MARKER_PATH, "w");
            if (marker)
            {
                fclose(marker);
            }
            HAL_INFO("sleep", "tier 2 heartbeat wake\n");
        }

        if (sleep_aov_state_needs_heal(aov_state))
        {
            /* The kernel only resumes on a mailbox timeout after three in a
             * row, so this is the terminal state of a dead MCU. */
            HAL_WARNING("sleep", "MCU did not answer %u mailbox timeouts in "
                        "a row; disarming AOV until the next boot\n",
                        sysfs_read_uint(AOV_TIMEOUT_STREAK_PATH));
            aov_healed = 1;
            snprintf(aov_reason, sizeof(aov_reason), "mcu_unresponsive");
            aov_active = 0;
            app_config.sleep_aov_mcu = false;
            app_config.sleep_wake_interval_s = 60;
            (void)sysfs_write_uint(AOV_HEARTBEAT_WAKES_PATH, 0);
            (void)sysfs_write_str(AOV_PROXY_PATH, "0\n");
            FILE *marker = fopen(AOV_HEAL_MARKER_PATH, "w");
            if (marker)
            {
                fclose(marker);
            }
            app_config_save();
        }

        if (sleep_usb_bind_on_wake(app_config.sleep_usb_on_motion,
                                   aov_state))
        {
            (void)usb_host_set(1);
        }

        HAL_INFO("sleep", "resumed (EC wake src %d, last event %d, aov %s)\n",
                 last_wake_src, last_event, aov_state);
    }
    else
    {
        HAL_INFO("sleep", "resumed (EC wake src %d, last event %d, aov off)\n",
                 last_wake_src, last_event);
    }

    pthread_mutex_lock(&lock);
    state = SLEEP_AWAKE;
    last_motion = mono_now();
    motion_seq++;
    wake_seq = motion_seq;
    pthread_mutex_unlock(&lock);

    return 0;
}

int sleep_policy_aov_available(void)
{
    return aov_supported;
}

int sleep_policy_aov_keep_pipeline(void)
{
    /*
     * AOV: the MCU takes the frames from the CSI/ISP while the AP sleeps, so
     * the pipeline must stay up.  Only valid together with the AOV proxy:
     * without it the MCU is never released and the streaming pipe would just
     * burn power across the suspend.
     */
    return enabled && aov_active && app_config.sleep_aov_keep_isp;
}

void sleep_policy_api_json(char *out, size_t size)
{
    long idle = 0;
    char heartbeat[256];

    if (!out || !size)
    {
        return;
    }

    heartbeat_status_json(heartbeat, sizeof(heartbeat));

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
             "\"aov_keep_isp\":%s,\"usb_on_motion\":%s,"
             "\"fast_resume\":\"%s\","
             "\"usb_bound\":%s,"
             "\"aov_available\":%s,\"aov_state\":\"%s\","
             "\"idle_timeout_s\":%u,\"wake_interval_s\":%u,"
             "\"wake_check_s\":%u,\"pmu_timeout_hz\":%u,"
             "\"heartbeat_wakes\":%u,\"timeout_streak\":%u,"
             "\"enabled\":%s,\"state\":\"%s\",\"idle_s\":%ld,"
             "\"last_wake_src\":%d,\"reason\":\"%s\",\"heartbeat\":%s}",
             app_config.sleep_enable ? "true" : "false",
             app_config.sleep_pmu_timer_wake ? "true" : "false",
             app_config.sleep_aov_mcu ? "true" : "false",
             app_config.sleep_aov_keep_isp ? "true" : "false",
             app_config.sleep_usb_on_motion ? "true" : "false",
             sleep_fast_resume_name(app_config.sleep_fast_resume),
             usb_host_bound() ? "true" : "false",
             aov_supported ? "true" : "false", aov_state_name(),
             app_config.sleep_idle_timeout_s,
             app_config.sleep_wake_interval_s,
             app_config.sleep_wake_check_s,
             app_config.sleep_pmu_timeout_hz,
             sysfs_read_uint(AOV_HEARTBEAT_WAKES_PATH),
             sysfs_read_uint(AOV_TIMEOUT_STREAK_PATH),
             enabled ? "true" : "false", state_name(state),
             idle > 0 ? idle : 0, last_wake_src, reason, heartbeat);
    pthread_mutex_unlock(&lock);
}

void sleep_policy_status_json(char *out, size_t size)
{
    long idle = 0;
    char heartbeat[96];

    if (!out || !size)
    {
        return;
    }

    heartbeat_status_fields(heartbeat, sizeof(heartbeat));

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
             "\"aov_state\":\"%s\",\"usb_on_motion\":%s,"
             "\"usb_bound\":%s,\"heartbeat_wakes\":%u,"
             "\"timeout_streak\":%u,\"idle_s\":%ld,"
             "\"last_wake_src\":%d,\"reason\":\"%s\",%s}",
             enabled ? "true" : "false", state_name(state),
             app_config.sleep_pmu_timer_wake ? "true" : "false",
             aov_active ? "true" : "false",
             aov_supported ? "true" : "false", aov_state_name(),
             app_config.sleep_usb_on_motion ? "true" : "false",
             usb_host_bound() ? "true" : "false",
             sysfs_read_uint(AOV_HEARTBEAT_WAKES_PATH),
             sysfs_read_uint(AOV_TIMEOUT_STREAK_PATH),
             idle > 0 ? idle : 0, last_wake_src, reason, heartbeat);
    pthread_mutex_unlock(&lock);
}