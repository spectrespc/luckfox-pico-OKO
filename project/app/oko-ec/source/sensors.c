/* sensors.c — INA226/INA3221/SHT21 по i2c-dev без драйверов ядра. */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "i2c_transport.h"
#include "sensors.h"
#include "sensors_math.h"

#define INA_REG_SHUNT 0x01
#define INA_REG_BUS 0x02
#define INA_REG_DIE 0xFF

#define INA226_DIE_ID 0x2260
#define INA3221_DIE_ID 0x3220

#define INA226_DELAY_US 1000u
#define INA226_RETRIES 10u

#define SHT21_CMD_TEMP 0xE3
#define SHT21_CMD_HUM 0xE5
#define SHT21_TEMP_DELAY_US 90000u
#define SHT21_HUM_DELAY_US 40000u
#define SHT21_RETRIES 2u

static unsigned timing_delay_us;
static unsigned timing_retries;

void sensors_set_timing(unsigned delay_us, unsigned retries)
{
    timing_delay_us = delay_us;
    timing_retries = retries;
}

static unsigned ina_delay(void)
{
    return timing_delay_us ? timing_delay_us : INA226_DELAY_US;
}

static unsigned ina_retries(void)
{
    return timing_retries ? timing_retries : INA226_RETRIES;
}

static unsigned sht_retries(void)
{
    return timing_retries ? timing_retries : SHT21_RETRIES;
}

static int ina_read_reg(int fd, uint8_t reg, uint16_t *out)
{
    uint8_t buf[2];
    unsigned delay_us = ina_delay();
    unsigned retries = ina_retries();

    for (unsigned attempt = 0; attempt < retries; attempt++)
    {
        if (write(fd, &reg, 1) != 1)
        {
            usleep(delay_us);
            continue;
        }

        usleep(delay_us);

        ssize_t n = read(fd, buf, sizeof(buf));
        if (n == (ssize_t)sizeof(buf))
        {
            *out = (uint16_t)(((uint16_t)buf[0] << 8) | buf[1]);
            return 0;
        }

        usleep(delay_us);
    }

    return -1;
}

int ina226_read(const char *device, uint8_t addr, uint32_t shunt_mohm,
                uint8_t channel, struct ina226_sample *out)
{
    int fd = i2c_transport_open(device, addr);
    if (fd < 0)
    {
        return -1;
    }

    uint16_t die_id = 0;
    enum ina_chip chip = INA_CHIP_UNKNOWN;
    if (ina_read_reg(fd, INA_REG_DIE, &die_id) == 0)
    {
        if (die_id == INA3221_DIE_ID)
        {
            chip = INA_CHIP_3221;
        }
        else if (die_id == INA226_DIE_ID)
        {
            chip = INA_CHIP_226;
        }
    }

    uint8_t shunt_reg;
    uint8_t bus_reg;
    if (chip == INA_CHIP_3221)
    {
        if (channel < 1 || channel > 3)
        {
            close(fd);
            return -1;
        }
        shunt_reg = (uint8_t)(INA_REG_SHUNT + 2 * (channel - 1));
        bus_reg = (uint8_t)(INA_REG_BUS + 2 * (channel - 1));
    }
    else
    {
        if (channel != 1)
        {
            close(fd);
            return -1;
        }
        shunt_reg = INA_REG_SHUNT;
        bus_reg = INA_REG_BUS;
    }

    uint16_t bus_raw = 0;
    uint16_t shunt_raw = 0;
    int rc = -1;

    if (ina_read_reg(fd, bus_reg, &bus_raw) == 0 &&
        ina_read_reg(fd, shunt_reg, &shunt_raw) == 0)
    {
        if (chip == INA_CHIP_3221)
        {
            out->bus_uv = ina3221_bus_uv(bus_raw);
            out->shunt_nv = ina3221_shunt_nv(shunt_raw);
            out->chip_name = "INA3221";
        }
        else
        {
            out->bus_uv = ina226_bus_uv(bus_raw);
            out->shunt_nv = ina226_shunt_nv(shunt_raw);
            out->chip_name = (chip == INA_CHIP_226) ? "INA226" : "INA";
        }

        out->chip = chip;
        out->channel = channel;
        out->bus_mv = out->bus_uv / 1000u;
        out->current_ma = ina226_current_ma(out->shunt_nv, shunt_mohm);
        out->power_mw = ina226_power_mw(out->bus_uv, out->current_ma);
        rc = 0;
    }

    close(fd);
    return rc;
}

static int sht21_measure(int fd, uint8_t command, unsigned delay_us, uint16_t *raw)
{
    uint8_t frame[3];
    unsigned retries = sht_retries();

    for (unsigned attempt = 0; attempt <= retries; attempt++)
    {
        if (write(fd, &command, 1) != 1)
        {
            usleep(delay_us);
            continue;
        }

        usleep(delay_us);

        ssize_t n = read(fd, frame, sizeof(frame));
        if (n == (ssize_t)sizeof(frame) && sht21_check(frame) == 0)
        {
            *raw = (uint16_t)(((uint16_t)frame[0] << 8) | frame[1]);
            return 0;
        }
    }

    return -1;
}

int sht21_read(const char *device, uint8_t addr, struct sht21_sample *out)
{
    int fd = i2c_transport_open(device, addr);
    if (fd < 0)
    {
        return -1;
    }

    unsigned temp_delay = timing_delay_us ? timing_delay_us : SHT21_TEMP_DELAY_US;
    unsigned hum_delay = timing_delay_us ? timing_delay_us : SHT21_HUM_DELAY_US;
    uint16_t temp_raw = 0;
    uint16_t hum_raw = 0;
    int rc = -1;

    if (sht21_measure(fd, SHT21_CMD_TEMP, temp_delay, &temp_raw) == 0 &&
        sht21_measure(fd, SHT21_CMD_HUM, hum_delay, &hum_raw) == 0)
    {
        out->temp_c100 = sht21_temp_c100(temp_raw);
        out->humidity_rh10 = sht21_rh10(hum_raw);
        rc = 0;
    }

    close(fd);
    return rc;
}