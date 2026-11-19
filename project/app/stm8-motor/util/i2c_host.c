#define _GNU_SOURCE

#include "i2c_host.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int verbose = 1;

/* Write-phase retry budget.  The STM8 wakes from Active-halt on the address
 * match, but the HSI start-up latency can make the very first frame after a
 * sleep period fail with EREMOTEIO (no ACK).  Retrying the address phase is
 * the same fix that was applied to the STM32 EC transport.
 * Only address-phase failures (write() < 0) are retried: a partial frame is
 * not replayed, so a move command can never be executed twice. */
#define I2C_WRITE_RETRIES 2
#define I2C_WRITE_RETRY_US 2000

void i2c_host_set_quiet(int quiet)
{
    verbose = quiet ? 0 : 1;
}

int i2c_host_open(const char *device, uint8_t address)
{
    int fd = open(device, O_RDWR);
    int timeout = 100;         /* 100 * 10 ms = 1 s */

    if (fd < 0)
    {
        if (verbose)
            fprintf(stderr, "cannot open %s: %s\n", device, strerror(errno));
        return -1;
    }
    if (ioctl(fd, I2C_SLAVE, address) < 0)
    {
        if (verbose)
            fprintf(stderr, "cannot select 0x%02X on %s: %s\n", address,
                    device, strerror(errno));
        close(fd);
        return -1;
    }
    /* The STM8 stretches SCL while it programs/erases Flash (the CPU stalls
       with interrupts disabled).  The rk3x default timeout is shorter than a
       bootloader erase+write burst, so the adapter aborts mid-transaction and
       the slave is left holding SCL.  One second covers the documented
       tPROG/tERASE worst cases with a margin. */
    if (ioctl(fd, I2C_TIMEOUT, timeout) < 0)
    {
        /* Not fatal: some adapters do not implement it. */
    }
    return fd;
}

void i2c_host_close(int fd)
{
    if (fd >= 0)
        close(fd);
}

int i2c_host_write(int fd, const uint8_t *buf, size_t len)
{
    unsigned attempt;
    ssize_t n = -1;

    for (attempt = 0; attempt < I2C_WRITE_RETRIES; attempt++)
    {
        if (attempt)
            usleep(I2C_WRITE_RETRY_US);

        n = write(fd, buf, len);
        if (n == (ssize_t)len)
            return 0;
        if (n >= 0)
            break;             /* partial transfer: do not replay it */
    }

    if (verbose)
        fprintf(stderr, "i2c write failed (%zd/%zu): %s\n", n, len,
                n < 0 ? strerror(errno) : "short write");
    return -1;
}

int i2c_host_transfer(int fd, const uint8_t *wbuf, size_t wlen,
                      uint8_t *rbuf, size_t rlen,
                      unsigned delay_us, unsigned retries)
{
    unsigned attempt;

    if (i2c_host_write(fd, wbuf, wlen))
        return -1;
    if (retries == 0)
        retries = 1;

    for (attempt = 0; attempt < retries; attempt++)
    {
        ssize_t n;

        usleep(delay_us);
        n = read(fd, rbuf, rlen);
        if (n == (ssize_t)rlen)
            return (int)n;
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EREMOTEIO)
        {
            if (verbose)
                fprintf(stderr, "i2c read failed: %s\n", strerror(errno));
        }
    }

    if (verbose)
        fprintf(stderr, "i2c read timed out after %u attempts\n", retries);
    return -1;
}

int i2c_host_probe(const char *device, uint8_t address)
{
    int fd = open(device, O_RDWR);

    if (fd < 0)
        return -1;
    if (ioctl(fd, I2C_SLAVE, address) < 0)
    {
        close(fd);
        return -1;
    }
    if (ioctl(fd, I2C_PEC, 0) < 0)
    {
        /* Not an error: some adapters do not implement PEC. */
    }
    if (i2c_host_write(fd, (const uint8_t *)"\x00", 1) < 0)
    {
        close(fd);
        return 0;              /* address free / no ACK */
    }
    close(fd);
    return 1;                  /* ACK */
}