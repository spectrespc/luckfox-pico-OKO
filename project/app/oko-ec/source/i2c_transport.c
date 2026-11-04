/* tools/bootloader_tool/i2c_transport.c */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#include "i2c_transport.h"

static int transport_verbose = 1;

void i2c_transport_set_verbose(int enable)
{
    transport_verbose = enable ? 1 : 0;
}

int i2c_transport_verbose(void)
{
    return transport_verbose;
}

int i2c_transport_open(const char *device, uint8_t addr)
{
    int fd = open(device, O_RDWR);
    if (fd < 0)
    {
        if (transport_verbose)
        {
            fprintf(stderr, "Cannot open %s: %s\n", device, strerror(errno));
        }
        return -1;
    }

    if (ioctl(fd, I2C_SLAVE, addr) < 0)
    {
        if (transport_verbose)
        {
            fprintf(stderr, "Cannot set I2C address 0x%02X: %s\n", addr, strerror(errno));
        }
        close(fd);
        return -1;
    }

    return fd;
}

int i2c_transport_write_read_timed(int fd, const uint8_t *wbuf, size_t wlen,
                                   uint8_t *rbuf, size_t rlen,
                                   unsigned delay_us, unsigned retries)
{
    ssize_t written = write(fd, wbuf, wlen);
    if (written != (ssize_t)wlen)
    {
        if (transport_verbose)
        {
            fprintf(stderr, "I2C write failed (%zd/%zu): %s\n", written, wlen, strerror(errno));
        }
        return -1;
    }

    if (retries == 0)
    {
        retries = 1;
    }

    for (unsigned attempt = 0; attempt < retries; attempt++)
    {
        usleep(delay_us);

        ssize_t readed = read(fd, rbuf, rlen);
        if (readed >= 3)
        {
            return (int)readed;
        }

        if (attempt + 1 < retries)
        {
            usleep(delay_us);
        }
    }

    if (transport_verbose)
    {
        fprintf(stderr, "I2C read failed after %u retries\n", retries);
    }
    return -1;
}

int i2c_transport_write_read(int fd, const uint8_t *wbuf, size_t wlen, uint8_t *rbuf, size_t rlen)
{
    return i2c_transport_write_read_timed(fd, wbuf, wlen, rbuf, rlen,
                                          I2C_WRITE_READ_DELAY_US, I2C_READ_RETRIES) < 0
               ? -1
               : 0;
}

int i2c_transport_probe(const char *device, uint8_t addr)
{
    int fd = open(device, O_RDWR);
    if (fd < 0)
    {
        if (transport_verbose)
        {
            fprintf(stderr, "Cannot open %s: %s\n", device, strerror(errno));
        }
        return -1;
    }

    if (ioctl(fd, I2C_SLAVE, addr) < 0)
    {
        close(fd);
        return -1;
    }

    uint8_t byte = 0;
    ssize_t n = read(fd, &byte, 1);
    int result;
    if (n == 1)
    {
        result = 1;
    }
    else if (n == 0 || errno == ENXIO || errno == ENODEV || errno == EREMOTEIO || errno == EIO)
    {
        result = 0;
    }
    else
    {
        result = -1;
    }

    close(fd);
    return result;
}

int i2c_transport_write_only(int fd, const uint8_t *buf, size_t len)
{
    ssize_t written = write(fd, buf, len);
    if (written != (ssize_t)len)
    {
        if (transport_verbose)
        {
            fprintf(stderr, "I2C write failed (%zd/%zu): %s\n", written, len, strerror(errno));
        }
        return -1;
    }

    return 0;
}