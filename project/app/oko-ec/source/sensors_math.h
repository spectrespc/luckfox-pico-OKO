/* sensors_math.h — чистые преобразования датчиков INA226/INA3221/SHT21.
 *
 * Без ввода-вывода и без плавающей точки: значения представлены в
 * целочисленных единицах (нВ, мкВ, мА, мВт, 0.01 °C, 0.1 %RH).
 */

#ifndef SENSORS_MATH_H
#define SENSORS_MATH_H

#include <stddef.h>
#include <stdint.h>

/* INA226: shunt voltage, LSB 2.5 мкВ (знаковый). Возврат в нВ. */
int32_t ina226_shunt_nv(uint16_t raw);

/* INA226: bus voltage, LSB 1.25 мВ. Возврат в мкВ. */
uint32_t ina226_bus_uv(uint16_t raw);

/* INA3221: shunt voltage, 13 бит в D15:D3, LSB 40 мкВ. Возврат в нВ. */
int32_t ina3221_shunt_nv(uint16_t raw);

/* INA3221: bus voltage, 13 бит в D15:D3, LSB 8 мВ. Возврат в мкВ. */
uint32_t ina3221_bus_uv(uint16_t raw);

/* Ток из shunt-напряжения: shunt_nv / (shunt_mohm * 1000), мА. */
int32_t ina226_current_ma(int32_t shunt_nv, uint32_t shunt_mohm);

/* Мощность: bus_uv * current_ma / 1e6, мВт; при токе < 0 — 0. */
uint32_t ina226_power_mw(uint32_t bus_uv, int32_t current_ma);

/* SHT21: температура в 0.01 °C. */
int32_t sht21_temp_c100(uint16_t raw);

/* SHT21: влажность в 0.1 %RH, клампится в 0..1000. */
int32_t sht21_rh10(uint16_t raw);

/* SHT21: CRC8, полином 0x31, init 0, MSB-first, без отражения. */
uint8_t sht21_crc8(const uint8_t *data, size_t len);

/* Проверка 3-байтного кадра SHT21: CRC последнего байта. 0 — верно. */
int sht21_check(const uint8_t frame[3]);

#endif /* SENSORS_MATH_H */