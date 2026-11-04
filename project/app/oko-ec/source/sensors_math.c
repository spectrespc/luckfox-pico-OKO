/* sensors_math.c — целочисленные преобразования INA226/SHT21. */

#include "sensors_math.h"

int32_t ina226_shunt_nv(uint16_t raw)
{
    return (int32_t)(int16_t)raw * 2500;
}

uint32_t ina226_bus_uv(uint16_t raw)
{
    return (uint32_t)raw * 1250u;
}

int32_t ina3221_shunt_nv(uint16_t raw)
{
    return ((int32_t)(int16_t)raw / 8) * 40000;
}

uint32_t ina3221_bus_uv(uint16_t raw)
{
    int32_t value = (int32_t)(int16_t)raw / 8;
    if (value < 0)
    {
        value = 0;
    }
    return (uint32_t)value * 8000u;
}

int32_t ina226_current_ma(int32_t shunt_nv, uint32_t shunt_mohm)
{
    if (shunt_mohm == 0)
    {
        return 0;
    }
    return shunt_nv / ((int32_t)shunt_mohm * 1000);
}

uint32_t ina226_power_mw(uint32_t bus_uv, int32_t current_ma)
{
    if (current_ma <= 0)
    {
        return 0;
    }
    return (uint32_t)((int64_t)bus_uv * current_ma / 1000000);
}

int32_t sht21_temp_c100(uint16_t raw)
{
    return -4685 + (int32_t)(17572LL * raw / 65536LL);
}

int32_t sht21_rh10(uint16_t raw)
{
    int32_t rh = -60 + (int32_t)(1250LL * raw / 65536LL);
    if (rh < 0)
    {
        rh = 0;
    }
    if (rh > 1000)
    {
        rh = 1000;
    }
    return rh;
}

uint8_t sht21_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++)
        {
            if (crc & 0x80)
            {
                crc = (uint8_t)((crc << 1) ^ 0x31);
            }
            else
            {
                crc = (uint8_t)(crc << 1);
            }
        }
    }
    return crc;
}

int sht21_check(const uint8_t frame[3])
{
    return sht21_crc8(frame, 2) == frame[2] ? 0 : -1;
}