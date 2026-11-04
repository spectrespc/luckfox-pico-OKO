/* sensors.h — чтение INA226/INA3221 и SHT21 через i2c-dev. */

#ifndef SENSORS_H
#define SENSORS_H

#include <stdint.h>

enum ina_chip
{
    INA_CHIP_UNKNOWN = 0,
    INA_CHIP_226,
    INA_CHIP_3221
};

struct ina226_sample
{
    enum ina_chip chip;
    const char *chip_name;
    uint8_t channel;
    uint32_t bus_uv;
    uint32_t bus_mv;
    int32_t shunt_nv;
    int32_t current_ma;
    uint32_t power_mw;
};

struct sht21_sample
{
    int32_t temp_c100;
    int32_t humidity_rh10;
};

/* Переопределить тайминги (0 — значения по умолчанию).
 * delay_us действует на обе операции SHT21 и на INA. */
void sensors_set_timing(unsigned delay_us, unsigned retries);

/* Читает bus/shunt выбранного канала INA226 (канал 1) или INA3221
 * (каналы 1..3). Определяет чип по Die ID (0x2260/0x3220).
 * Конфигурацию и калибровку не пишет. 0 — успех. */
int ina226_read(const char *device, uint8_t addr, uint32_t shunt_mohm,
                uint8_t channel, struct ina226_sample *out);

/* No-hold измерение SHT21: 0xE3 (T) и 0xE5 (RH) с проверкой CRC8. */
int sht21_read(const char *device, uint8_t addr, struct sht21_sample *out);

#endif /* SENSORS_H */