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

void i2c_host_set_quiet(int quiet)
{
    verbose = quiet ? 0 : 1;
}

int i2c_host_open(const char *device, uint8_t address)
{
    int fd = open(device, O_RDWR);

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
    return fd;
}

void i2c_host_close(int fd)
{
    if (fd >= 0)
        close(fd);
}

int i2c_host_write(int fd, const uint8_t *buf, size_t len)
{
    ssize_t n = write(fd, buf, len);

    if (n != (ssize_t)len)
    {
        if (verbose)
            fprintf(stderr, "i2c write failed (%zd/%zu): %s\n", n, len,
                    strerror(errno));
        return -1;
    }
    return 0;
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