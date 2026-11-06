#include "focus_motor.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <pthread.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "focus_proto.h"

#define FM_APP_DELAY_US   2000u
#define FM_BOOT_DELAY_US  5000u
#define FM_RETRIES        3
#define FM_LOCK_PATH      "/run/oko-motor.lock"
#define FM_LOCK_FALLBACK  "/tmp/oko-motor.lock"

/* Protocol values mirrored from stm8-motor/src/common/oko_boot.h. */
#define FM_CMD_GET_STATE   0x08
#define FM_ROLE_APP        0x00
#define FM_BOOT_CMD_INFO   0x10
#define FM_BOOT_WRITE_MAX  32
#define FM_BOOT_STATE_BOOT 1

static pthread_mutex_t fm_mutex = PTHREAD_MUTEX_INITIALIZER;
static char fm_device[64] = "/dev/i2c-3";
static unsigned int fm_address = 0x60;
static int fm_enabled;
static volatile int fm_suspended;
static uint16_t fm_speed;
static fm_snapshot fm_cache;

static const char *fm_lock_path(void)
{
    if (access("/run", W_OK) == 0)
        return FM_LOCK_PATH;
    return FM_LOCK_FALLBACK;
}

static int fm_lock_take(int exclusive)
{
    const char *path = fm_lock_path();
    int fd = open(path, O_RDWR | O_CREAT, 0666);

    if (fd < 0)
        return -2;                /* locking is advisory; the bus still works */
    if (flock(fd, (exclusive ? LOCK_EX : LOCK_SH) | LOCK_NB) < 0)
    {
        close(fd);
        return -1;
    }
    return fd;
}

static void fm_lock_release(int fd)
{
    if (fd >= 0)
    {
        flock(fd, LOCK_UN);
        close(fd);
    }
}

static int fm_open(void)
{
    int fd = open(fm_device, O_RDWR);

    if (fd < 0)
        return -1;
    if (ioctl(fd, I2C_SLAVE, fm_address) < 0)
    {
        close(fd);
        return -1;
    }
    return fd;
}

static int fm_transfer(int fd, const uint8_t *wbuf, size_t wlen,
                       uint8_t *rbuf, size_t rlen,
                       unsigned delay_us, int retries)
{
    ssize_t n;

    if (!wbuf || write(fd, wbuf, wlen) != (ssize_t)wlen)
        return -1;

    for (int attempt = 0; attempt < retries; attempt++)
    {
        usleep(delay_us);
        n = read(fd, rbuf, rlen);
        if (n == (ssize_t)rlen)
            return 0;
    }
    return -1;
}

int focus_motor_init(const char *device, unsigned int address)
{
    pthread_mutex_lock(&fm_mutex);
    if (device && *device)
    {
        strncpy(fm_device, device, sizeof(fm_device) - 1);
        fm_device[sizeof(fm_device) - 1] = '\0';
    }
    if (address >= 1 && address <= 127)
        fm_address = address;
    memset(&fm_cache, 0, sizeof(fm_cache));
    fm_cache.status = -1;
    fm_cache.enabled = fm_enabled;
    pthread_mutex_unlock(&fm_mutex);
    focus_motor_probe();
    return FM_OK;
}

void focus_motor_shutdown(void)
{
    pthread_mutex_lock(&fm_mutex);
    fm_cache.present = 0;
    pthread_mutex_unlock(&fm_mutex);
}

void focus_motor_set_enabled(int enabled)
{
    pthread_mutex_lock(&fm_mutex);
    fm_enabled = enabled ? 1 : 0;
    fm_cache.enabled = fm_enabled;
    pthread_mutex_unlock(&fm_mutex);
}

int focus_motor_enabled(void)
{
    return fm_enabled;
}

void focus_motor_suspend(int on)
{
    fm_suspended = on ? 1 : 0;
}

int focus_motor_suspended(void)
{
    return fm_suspended;
}

int focus_motor_busy(void)
{
    int fd = fm_lock_take(1);

    if (fd == -1)
        return 1;
    if (fd >= 0)
        fm_lock_release(fd);
    return 0;
}

static int fm_begin(int exclusive)
{
    int fd;

    if (fm_suspended)
        return FM_ERR_SUSPENDED;
    fd = fm_lock_take(exclusive);
    if (fd == -1)
        return FM_ERR_BUSY;
    return fd;
}

static int fm_end(int lock_fd)
{
    fm_lock_release(lock_fd);
    return 0;
}

static int fm_app_read(const uint8_t *req, size_t req_len, uint8_t *resp,
                       size_t resp_len)
{
    int lock = fm_begin(0);
    int fd;
    int rc;

    if (lock == FM_ERR_BUSY || lock == FM_ERR_SUSPENDED)
        return lock;
    fd = fm_open();
    if (fd < 0)
    {
        fm_end(lock);
        return FM_ERR_IO;
    }
    rc = fm_transfer(fd, req, req_len, resp, resp_len, FM_APP_DELAY_US,
                     FM_RETRIES);
    close(fd);
    fm_end(lock);
    return rc == 0 ? FM_OK : FM_ERR_IO;
}

static int fm_app_write(const uint8_t *req, size_t req_len)
{
    int lock = fm_begin(1);
    int fd;
    int rc;

    if (lock == FM_ERR_BUSY || lock == FM_ERR_SUSPENDED)
        return lock;
    fd = fm_open();
    if (fd < 0)
    {
        fm_end(lock);
        return FM_ERR_IO;
    }
    rc = write(fd, req, req_len) == (ssize_t)req_len ? 0 : -1;
    close(fd);
    fm_end(lock);
    return rc == 0 ? FM_OK : FM_ERR_IO;
}

int focus_motor_read_app_version(uint8_t version[3])
{
    uint8_t req[1], resp[4];
    int rc;

    /* 0x08 reports the role, so the bootloader-only 0x10 probe never reaches
     * a running application. */
    req[0] = FM_CMD_GET_STATE;
    rc = fm_app_read(req, sizeof(req), resp, sizeof(resp));
    if (rc != FM_OK)
        return rc;
    if (resp[0] != FM_ROLE_APP)
        return FM_ERR_NO_DEVICE;
    memcpy(version, &resp[1], 3);
    return FM_OK;
}

int focus_motor_read_boot_info(uint8_t info[8])
{
    int lock = fm_begin(0);
    int fd;
    uint8_t cmd = FM_BOOT_CMD_INFO;
    int rc;

    if (lock == FM_ERR_BUSY || lock == FM_ERR_SUSPENDED)
        return lock;
    fd = fm_open();
    if (fd < 0)
    {
        fm_end(lock);
        return FM_ERR_IO;
    }
    rc = fm_transfer(fd, &cmd, 1, info, 8, FM_BOOT_DELAY_US, FM_RETRIES);
    close(fd);
    fm_end(lock);
    if (rc != 0)
        return FM_ERR_IO;
    /* BOOT_INFO reports max block 32 in byte 6 and state 1 in byte 7. */
    if (info[6] != FM_BOOT_WRITE_MAX || info[7] != FM_BOOT_STATE_BOOT)
        return FM_ERR_NO_DEVICE;
    return FM_OK;
}

int focus_motor_probe(void)
{
    fm_snapshot snap;
    uint8_t info[8];
    uint8_t version[3];
    int rc;

    memset(&snap, 0, sizeof(snap));
    snap.enabled = fm_enabled;
    snap.status = -1;

    if (focus_motor_suspended())
        return FM_ERR_SUSPENDED;  /* keep the last known snapshot */

    /* Application first: 0x10 is a bootloader-only command, so polling it
     * against a running application every few seconds adds bus noise. */
    rc = focus_motor_read_app_version(version);
    if (rc == FM_ERR_BUSY || rc == FM_ERR_SUSPENDED)
        return rc;                /* keep the last known snapshot */
    if (rc == FM_OK)
    {
        snap.present = 1;
        snap.has_app_version = 1;
        memcpy(snap.app_version, version, 3);
    }
    else
    {
        rc = focus_motor_read_boot_info(info);
        if (rc == FM_ERR_BUSY || rc == FM_ERR_SUSPENDED)
            return rc;            /* keep the last known snapshot */
        if (rc == FM_OK)
        {
            snap.present = 1;
            snap.bootloader = 1;
            snap.has_boot_version = 1;
            snap.boot_version[0] = info[0];
            snap.boot_version[1] = info[1];
            snap.boot_version[2] = info[2];
            snap.has_app_version = 1;
            snap.app_version[0] = info[3];
            snap.app_version[1] = info[4];
            snap.app_version[2] = info[5];
        }
    }

    if (snap.present && !snap.bootloader)
    {
        int32_t position;
        uint8_t status;

        if (focus_motor_get_position(0, &position) == FM_OK)
        {
            snap.has_position = 1;
            snap.position = position;
        }
        if (focus_motor_get_status(0, &status) == FM_OK)
            snap.status = status;
    }

    pthread_mutex_lock(&fm_mutex);
    fm_cache = snap;
    pthread_mutex_unlock(&fm_mutex);
    return snap.present ? FM_OK : FM_ERR_NO_DEVICE;
}

void focus_motor_snapshot(fm_snapshot *out)
{
    if (!out) return;
    pthread_mutex_lock(&fm_mutex);
    *out = fm_cache;
    pthread_mutex_unlock(&fm_mutex);
}

int focus_motor_move(uint8_t motor, int16_t steps)
{
    uint8_t req[4];
    size_t len = focus_proto_move(req, motor, steps);
    int rc;

    if (len == 0 || fm_speed == 0) return FM_ERR_PARAM;
    /* The controller loses its speed on reset; re-send the cached value. */
    if (focus_motor_set_speed(motor, fm_speed) != FM_OK)
        return FM_ERR_IO;
    rc = fm_app_write(req, len);
    if (rc == FM_OK) focus_motor_probe();
    return rc;
}

int focus_motor_stop(uint8_t motor)
{
    uint8_t req[2];
    size_t len = focus_proto_stop(req, motor);
    int rc;

    if (len == 0) return FM_ERR_PARAM;
    rc = fm_app_write(req, len);
    if (rc == FM_OK) focus_motor_probe();
    return rc;
}

int focus_motor_stop_all(void)
{
    int rc = focus_motor_stop(FOCUS_MOTOR_FOCUS);
    if (focus_motor_stop(FOCUS_MOTOR_ZOOM) != FM_OK)
        rc = FM_ERR_IO;
    return rc;
}

int focus_motor_set_speed(uint8_t motor, uint16_t speed)
{
    uint8_t req[4];
    size_t len = focus_proto_speed(req, motor, speed);
    int rc;

    if (len == 0 || speed < 1 || speed > 5000) return FM_ERR_PARAM;
    rc = fm_app_write(req, len);
    if (rc == FM_OK)
        fm_speed = speed;
    return rc;
}

int focus_motor_set_all_speeds(uint16_t speed)
{
    int rc = focus_motor_set_speed(FOCUS_MOTOR_FOCUS, speed);
    if (focus_motor_set_speed(FOCUS_MOTOR_ZOOM, speed) != FM_OK)
        rc = FM_ERR_IO;
    return rc;
}

int focus_motor_get_position(uint8_t motor, int32_t *position)
{
    uint8_t req[2], resp[4];
    size_t len = focus_proto_get_pos(req, motor);

    if (len == 0 || !position) return FM_ERR_PARAM;
    if (fm_app_read(req, len, resp, sizeof(resp)) != FM_OK)
        return FM_ERR_IO;
    return focus_proto_parse_pos(resp, sizeof(resp), position) == 0 ?
        FM_OK : FM_ERR_IO;
}

int focus_motor_get_status(uint8_t motor, uint8_t *status)
{
    uint8_t req[2], resp[1];
    size_t len = focus_proto_get_status(req, motor);

    if (len == 0 || !status) return FM_ERR_PARAM;
    if (fm_app_read(req, len, resp, sizeof(resp)) != FM_OK)
        return FM_ERR_IO;
    return focus_proto_parse_status(resp, sizeof(resp), status) == 0 ?
        FM_OK : FM_ERR_IO;
}

int focus_motor_reset(void)
{
    uint8_t req[1];
    size_t len = focus_proto_reset(req);

    if (len == 0) return FM_ERR_PARAM;
    return fm_app_write(req, len);
}

const char *focus_motor_error(int code)
{
    switch (code)
    {
        case FM_OK:             return "ok";
        case FM_ERR_IO:         return "I2C transfer failed";
        case FM_ERR_NO_DEVICE:  return "no response at the configured address";
        case FM_ERR_SUSPENDED:  return "the motor bus is suspended for flashing";
        case FM_ERR_BUSY:       return "the motor bus is locked by another process";
        case FM_ERR_PARAM:      return "invalid parameter";
        default:                return "unknown error";
    }
}