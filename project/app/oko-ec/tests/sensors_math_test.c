/* sensors_math_test.c — хостовый тест целочисленных преобразований.
 *
 * Не входит в сборку пакета. Запуск:
 *   gcc -std=c99 -Wall -Wextra -Iproject/app/oko-ec/source \
 *       project/app/oko-ec/source/sensors_math.c \
 *       project/app/oko-ec/tests/sensors_math_test.c -o /tmp/oko-ec-test
 *   /tmp/oko-ec-test
 */

#include <stdio.h>
#include <stdint.h>

#include "sensors_math.h"

static int failures;

static void check_i32(const char *name, int32_t actual, int32_t expected)
{
    if (actual != expected)
    {
        printf("FAIL %s: got %ld, expected %ld\n", name, (long)actual, (long)expected);
        failures++;
    }
    else
    {
        printf("ok   %s = %ld\n", name, (long)actual);
    }
}

static void check_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual != expected)
    {
        printf("FAIL %s: got %lu, expected %lu\n", name,
               (unsigned long)actual, (unsigned long)expected);
        failures++;
    }
    else
    {
        printf("ok   %s = %lu\n", name, (unsigned long)actual);
    }
}

static void check_crc(const char *name, uint8_t actual, uint8_t expected)
{
    if (actual != expected)
    {
        printf("FAIL %s: got 0x%02X, expected 0x%02X\n", name, actual, expected);
        failures++;
    }
    else
    {
        printf("ok   %s = 0x%02X\n", name, actual);
    }
}

static void test_frame(const uint8_t first, const uint8_t second)
{
    uint8_t frame[3];
    frame[0] = first;
    frame[1] = second;
    frame[2] = sht21_crc8(frame, 2);

    if (sht21_check(frame) != 0)
    {
        printf("FAIL frame %02X %02X: valid CRC rejected\n", first, second);
        failures++;
    }
    if (sht21_crc8(frame, 3) != 0)
    {
        printf("FAIL frame %02X %02X: crc8(frame||crc) != 0\n", first, second);
        failures++;
    }

    frame[2] ^= 0x01;
    if (sht21_check(frame) == 0)
    {
        printf("FAIL frame %02X %02X: corrupted CRC accepted\n", first, second);
        failures++;
    }
}

int main(void)
{
    check_u32("bus_uv(0x0000)", ina226_bus_uv(0x0000), 0u);
    check_u32("bus_uv(0x0BB8)", ina226_bus_uv(0x0BB8), 3750000u);

    check_i32("shunt_nv(0x7FFF)", ina226_shunt_nv(0x7FFF), 81917500);
    check_i32("shunt_nv(0x8000)", ina226_shunt_nv(0x8000), -81920000);

    check_u32("ina3221_bus_uv(0x0008)", ina3221_bus_uv(0x0008), 8000u);
    check_u32("ina3221_bus_uv(0x0010)", ina3221_bus_uv(0x0010), 16000u);
    check_u32("ina3221_bus_uv(0xFFF8)", ina3221_bus_uv(0xFFF8), 0u);
    check_i32("ina3221_shunt_nv(0x0008)", ina3221_shunt_nv(0x0008), 40000);
    check_i32("ina3221_shunt_nv(0x00C8)", ina3221_shunt_nv(0x00C8), 1000000);
    check_i32("ina3221_shunt_nv(0xFFF8)", ina3221_shunt_nv(0xFFF8), -40000);

    check_i32("current_ma(81917500, 100)", ina226_current_ma(81917500, 100), 819);
    check_i32("current_ma(1000000, 100)", ina226_current_ma(1000000, 100), 10);
    check_i32("current_ma(-81920000, 100)", ina226_current_ma(-81920000, 100), -819);

    check_u32("power_mw(3750000, 819)", ina226_power_mw(3750000, 819), 3071u);
    check_u32("power_mw(3750000, -10)", ina226_power_mw(3750000, -10), 0u);

    check_i32("sht21_temp_c100(0)", sht21_temp_c100(0), -4685);
    check_i32("sht21_temp_c100(65535)", sht21_temp_c100(65535), 12886);

    check_i32("sht21_rh10(0)", sht21_rh10(0), 0);
    check_i32("sht21_rh10(65535)", sht21_rh10(65535), 1000);

    const uint8_t zero = 0x00;
    const uint8_t one = 0x01;
    check_crc("crc8({0x00})", sht21_crc8(&zero, 1), 0x00);
    check_crc("crc8({0x01})", sht21_crc8(&one, 1), 0x31);

    test_frame(0x00, 0x00);
    test_frame(0x61, 0x9C);
    test_frame(0x12, 0x34);
    test_frame(0xBE, 0xEF);

    if (failures)
    {
        printf("\n%d test(s) failed\n", failures);
        return 1;
    }

    printf("\nall tests passed\n");
    return 0;
}