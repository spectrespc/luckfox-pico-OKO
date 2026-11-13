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

/* Проверить адрес на уже открытом fd: 1 — ACK, 0 — NACK, -1 — ошибка. */
static int transport_probe_fd(int fd)
{
    uint8_t byte = 0;
    ssize_t n = read(fd, &byte, 1);

    if (n == 1)
    {
        return 1;
    }

    if (n == 0 || errno == ENXIO || errno == ENODEV || errno == EREMOTEIO || errno == EIO)
    {
        return 0;
    }

    return -1;
}

/* Запись кадра с повтором: после неудачи ждём и проверяем адрес.
 * Повтор не делается, если запись уже прошла (иначе команда с
 * побочным эффектом выполнилась бы дважды). */
static int transport_write_retry(int fd, const uint8_t *wbuf, size_t wlen, unsigned delay_us)
{
    for (unsigned attempt = 0; attempt < I2C_WRITE_RETRIES; attempt++)
    {
        ssize_t written = write(fd, wbuf, wlen);

        if (written == (ssize_t)wlen)
        {
            return 0;
        }

        if (transport_verbose)
        {
            fprintf(stderr, "I2C write failed (%zd/%zu): %s\n", written, wlen, strerror(errno));
        }

        if (attempt + 1 < I2C_WRITE_RETRIES)
        {
            usleep(delay_us);

            int ack = transport_probe_fd(fd);

            if (ack != 1 && transport_verbose)
            {
                fprintf(stderr, "I2C write retry: address probe %d\n", ack);
            }
        }
    }

    return -1;
}

int i2c_transport_write_read_timed(int fd, const uint8_t *wbuf, size_t wlen,
                                   uint8_t *rbuf, size_t rlen,
                                   unsigned delay_us, unsigned retries)
{
    if (transport_write_retry(fd, wbuf, wlen, delay_us) < 0)
    {
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

    int result = transport_probe_fd(fd);

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