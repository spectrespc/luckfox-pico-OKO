/* i2c_host.h - minimal Linux i2c-dev transport for oko-motor. */
#ifndef OKO_I2C_HOST_H
#define OKO_I2C_HOST_H

#include <stddef.h>
#include <stdint.h>

void i2c_host_set_quiet(int quiet);
int i2c_host_open(const char *device, uint8_t address);
void i2c_host_close(int fd);
int i2c_host_write(int fd, const uint8_t *buf, size_t len);
/* write + delay + read; returns the number of bytes read or -1. */
int i2c_host_transfer(int fd, const uint8_t *wbuf, size_t wlen,
                      uint8_t *rbuf, size_t rlen,
                      unsigned delay_us, unsigned retries);
int i2c_host_probe(const char *device, uint8_t address);

#endif /* OKO_I2C_HOST_H */