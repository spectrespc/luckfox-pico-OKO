/* tools/bootloader_tool/main.c
 *
 * Унифицированная утилита прошивки устройств проекта OKO.
 *
 * Цели:
 *   ec    — STM32F103, контроллер батареи (ACPI EC), адрес I2C 0x42.
 *           Прошивка через I2C-протокол загрузчика (протокол v2, один слот).
 *   motor — STM8S103F3P6, эмулятор шаговых двигателей.
 *           Прошивка через внешнюю утилиту stm8flash (ST-Link v2).
 *
 * Локальные дополнения OKO: датчики INA226 (0x41) и SHT21 (0x40) на той же
 * шине, команды scan/battery/cells/temp/sensors и демон oko-ec daemon.
 *
 * Использование:
 *   oko-ec [опции] info
 *   oko-ec [опции] status
 *   oko-ec [опции] battery|cells|temp
 *   oko-ec [опции] ina226|sht21|sensors
 *   oko-ec [опции] scan
 *   oko-ec [опции] flash <file.bin> <version>
 *   oko-ec [опции] enter-update
 *   oko-ec [опции] reset
 *   oko-ec [опции] daemon [--interval N] [--once]
 *   oko-ec [опции] ec <i2c-dev> info|status|flash|enter-update|reset   (совместимость)
 *   oko-ec motor stlink <file.ihx> [--verify]                          (без изменений)
 *
 * Протокол (цель ec):
 *   Кадр запроса:  [LEN_LO][LEN_HI][CMD][PAYLOAD...]
 *   Кадр ответа:   [LEN_LO][LEN_HI][STATUS][PAYLOAD...]
 *   LEN = количество байт после него (STATUS + PAYLOAD).
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "crc_host.h"
#include "daemon.h"
#include "ec_proto.h"
#include "i2c_transport.h"
#include "sensors.h"

/* ========================================================================
 * Константы протокола (протокол v2, один слот приложения)
 * ======================================================================== */

#define I2C_ADDR7      0x42
#define I2C_ADDR_MOTOR 0x39 /* STM8 эмулятор моторов                   */

#define CMD_GET_INFO      0x01
#define CMD_BEGIN_UPDATE  0x02
#define CMD_WRITE_DATA    0x03
#define CMD_VERIFY_UPDATE 0x04
#define CMD_COMMIT        0x05
#define CMD_GET_STATUS    0x07
#define CMD_RESET         0x08
#define CMD_ENTER_UPDATE  0x0E

#define ST_OK        0x00
#define ST_ERR_CMD   0x01
#define ST_ERR_PARAM 0x02
#define ST_ERR_STATE 0x03
#define ST_ERR_FLASH 0x05
#define ST_ERR_CRC   0x06
#define ST_ERR_SEQ   0x07
#define ST_ERR_LEN   0x08

/* GET_INFO: payload 12 байт, одинаков для загрузчика и прошивки:
 *   [0]     device_type: 1 = загрузчик, 2 = приложение
 *   [1]     версия протокола (2)
 *   [2..5]  версия прошивки, LE32
 *   [6..9]  размер образа, LE32
 *   [10]    флаги: бит 0 = pending (ожидает подтверждения запуска)
 *   [11]    дополнительно (для приложения: число ячеек батареи)
 */
#define DEVICE_TYPE_BOOT       1 /* STM32 загрузчик  */
#define DEVICE_TYPE_APP        2 /* STM32 прошивка   */
#define DEVICE_TYPE_MOTOR_BOOT 3 /* STM8 загрузчик   */
#define DEVICE_TYPE_MOTOR_APP  4 /* STM8 прошивка    */
#define PROTO_VER              2
#define INFO_PAYLOAD_LEN       12
#define INFO_FLAG_PENDING      (1U << 0)

/*
 * Размер чанка данных при записи прошивки.
 * Кадр WRITE_DATA: 2(LEN) + 1(CMD) + 4(offset) + 1(len) + N(data) = 8+N.
 * CP2112 ограничивает запись ~61 байтом: 8 + 32 = 40 байт — безопасно.
 */
#define CHUNK_SIZE 32

/* Размер буфера чтения (максимальный ответ: 2 + 1 + 12 = 15 байт). */
#define I2C_READ_BUF_SIZE 64

/* ========================================================================
 * Параметры командной строки
 * ======================================================================== */

#define EC_DEFAULT_DEV "/dev/i2c-3"
#define EC_DEFAULT_DELAY_US 5000u
#define EC_DEFAULT_RETRIES 100u

struct options
{
    const char *dev;
    uint8_t ec_addr;
    uint8_t ina_addr;
    uint8_t ina_channel;
    uint8_t sht_addr;
    uint32_t shunt_mohm;
    unsigned delay_us;
    unsigned retries;
    int json;
    int quiet;
};

static unsigned g_ec_delay_us = EC_DEFAULT_DELAY_US;
static unsigned g_ec_retries = EC_DEFAULT_RETRIES;

static unsigned ec_delay(const struct options *opt)
{
    return opt->delay_us ? opt->delay_us : EC_DEFAULT_DELAY_US;
}

static unsigned ec_retries(const struct options *opt)
{
    return opt->retries ? opt->retries : EC_DEFAULT_RETRIES;
}

static int ec_io(int fd, const uint8_t *wbuf, size_t wlen, uint8_t *rbuf, size_t rlen)
{
    return i2c_transport_write_read_timed(fd, wbuf, wlen, rbuf, rlen,
                                          g_ec_delay_us, g_ec_retries) < 0
               ? -1
               : 0;
}

/* ========================================================================
 * Вспомогательные функции
 * ======================================================================== */

static const char *status_str(uint8_t status)
{
    switch (status)
    {
    case ST_OK:
        return "OK";
    case ST_ERR_CMD:
        return "ERR_CMD";
    case ST_ERR_PARAM:
        return "ERR_PARAM";
    case ST_ERR_STATE:
        return "ERR_STATE";
    case ST_ERR_FLASH:
        return "ERR_FLASH";
    case ST_ERR_CRC:
        return "ERR_CRC";
    case ST_ERR_SEQ:
        return "ERR_SEQ";
    case ST_ERR_LEN:
        return "ERR_LEN";
    default:
        return "UNKNOWN";
    }
}

static void put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)(v >> 8);
}

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void print_centi(char *buf, size_t size, int32_t value, int scale, char suffix)
{
    int32_t frac = value % scale;
    const char *sign = "";
    if (frac < 0)
    {
        frac = -frac;
    }
    if (value < 0 && value > -scale)
    {
        sign = "-";
    }
    if (scale == 100)
    {
        snprintf(buf, size, "%s%ld.%02ld%c", sign, (long)(value / scale), (long)frac,
                 suffix);
    }
    else
    {
        snprintf(buf, size, "%s%ld.%ld%c", sign, (long)(value / scale), (long)frac,
                 suffix);
    }
}

/* ========================================================================
 * Команды протокола (цель ec)
 * ======================================================================== */

static int require_app(int fd, unsigned delay_us, unsigned retries)
{
    struct ec_info info;

    if (ec_get_info(fd, &info, delay_us, retries) < 0)
    {
        return -1;
    }
    if (info.device_type != EC_DEVICE_APP)
    {
        fprintf(stderr,
                "oko-ec: device is in bootloader (device_type %u); battery data is "
                "available only in the application. Run \"oko-ec reset\" and retry.\n",
                info.device_type);
        return -1;
    }
    return 0;
}

static int cmd_info(int fd, const struct options *opt)
{
    struct ec_info info;

    if (ec_get_info(fd, &info, ec_delay(opt), ec_retries(opt)) < 0)
    {
        return -1;
    }

    if (opt->json)
    {
        printf("{\"ok\":1,\"info\":{\"device_type\":%u,\"proto\":%u,"
               "\"version\":%lu,\"img_size\":%lu,\"flags\":%u,\"extra\":%u}}\n",
               (unsigned)info.device_type, (unsigned)info.proto,
               (unsigned long)info.version, (unsigned long)info.img_size,
               (unsigned)info.flags, (unsigned)info.extra);
        return 0;
    }

    if (opt->quiet)
    {
        printf("%u\n%u\n%lu\n%lu\n%u\n%u\n", (unsigned)info.device_type,
               (unsigned)info.proto, (unsigned long)info.version,
               (unsigned long)info.img_size, (unsigned)info.flags,
               (unsigned)info.extra);
        return 0;
    }

    printf("device_type: %u (%s)\n", (unsigned)info.device_type,
           info.device_type == DEVICE_TYPE_BOOT         ? "bootloader (STM32)"
           : info.device_type == DEVICE_TYPE_APP        ? "application (STM32)"
           : info.device_type == DEVICE_TYPE_MOTOR_BOOT ? "bootloader (STM8)"
           : info.device_type == DEVICE_TYPE_MOTOR_APP  ? "application (STM8)"
                                                        : "unknown");
    printf("proto      : %u\n", (unsigned)info.proto);
    printf("version    : 0x%08lX\n", (unsigned long)info.version);
    printf("img_size   : %lu bytes (%lu.%02lu KB)\n", (unsigned long)info.img_size,
           (unsigned long)(info.img_size / 1024),
           (unsigned long)((info.img_size % 1024) * 100 / 1024));
    printf("flags      : 0x%02X (pending: %s)\n", (unsigned)info.flags,
           (info.flags & INFO_FLAG_PENDING) ? "yes" : "no");

    if (info.device_type == DEVICE_TYPE_APP)
    {
        printf("cells      : %u\n", (unsigned)info.extra);
    }
    else if (info.device_type == DEVICE_TYPE_MOTOR_APP)
    {
        printf("motor_flags: 0x%02X\n", (unsigned)info.extra);
    }

    return 0;
}

static int cmd_status(int fd, const struct options *opt)
{
    struct ec_status status;

    if (ec_get_status(fd, &status, ec_delay(opt), ec_retries(opt)) < 0)
    {
        return -1;
    }

    if (opt->json)
    {
        printf("{\"ok\":1,\"status\":{\"last_status\":%u,\"update_state\":%u,"
               "\"update_offset\":%lu}}\n",
               (unsigned)status.last_status, (unsigned)status.update_state,
               (unsigned long)status.update_offset);
        return 0;
    }

    if (opt->quiet)
    {
        printf("%u\n%u\n%lu\n", (unsigned)status.last_status,
               (unsigned)status.update_state, (unsigned long)status.update_offset);
        return 0;
    }

    printf("Response length: 7, status: 0x%02X (%s)\n", ST_OK, status_str(ST_OK));
    printf("\n=== Update Status ===\n");
    printf("Last status   : 0x%02X (%s)\n", (unsigned)status.last_status,
           status_str(status.last_status));
    printf("Update state  : %u (%s)\n", (unsigned)status.update_state,
           status.update_state == 0   ? "IDLE"
           : status.update_state == 1 ? "ACTIVE"
           : status.update_state == 2 ? "VERIFIED"
                                      : "UNKNOWN");
    printf("Update offset : %lu bytes\n", (unsigned long)status.update_offset);
    return 0;
}

static int cmd_battery(int fd, const struct options *opt)
{
    struct ec_battery battery;

    if (require_app(fd, ec_delay(opt), ec_retries(opt)) < 0)
    {
        return -1;
    }
    if (ec_get_battery(fd, &battery, ec_delay(opt), ec_retries(opt)) < 0)
    {
        return -1;
    }

    if (opt->json)
    {
        struct oko_snapshot snap;
        memset(&snap, 0, sizeof(snap));
        snap.ok = 1;
        snap.ec.ok = 1;
        snap.ec.voltage_mv = battery.voltage_mv;
        snap.ec.current_ma = battery.current_ma;
        snap.ec.temp_c10 = battery.temp_c10;
        snap.ec.soc = battery.soc;
        snap.ec.status = battery.status;
        for (int i = 0; i < 3; i++)
        {
            snap.ec.cells[i] = battery.cells[i];
        }
        snapshot_print_json(stdout, &snap, SNAP_EC, 0);
        return 0;
    }

    if (opt->quiet)
    {
        printf("%u %d %d %u %u %u %u %u\n", (unsigned)battery.voltage_mv,
               (int)battery.current_ma, (int)battery.temp_c10,
               (unsigned)battery.soc, (unsigned)battery.status,
               (unsigned)battery.cells[0], (unsigned)battery.cells[1],
               (unsigned)battery.cells[2]);
        return 0;
    }

    printf("voltage_mv : %u\n", (unsigned)battery.voltage_mv);
    printf("current_ma : %d\n", (int)battery.current_ma);
    printf("temp_c10   : %d\n", (int)battery.temp_c10);
    printf("soc        : %u %%\n", (unsigned)battery.soc);
    printf("status     : 0x%02X (discharge: %s, charge: %s, full: %s, error: %s)\n",
           (unsigned)battery.status,
           (battery.status & 0x01) ? "yes" : "no",
           (battery.status & 0x02) ? "yes" : "no",
           (battery.status & 0x04) ? "yes" : "no",
           (battery.status & 0x08) ? "yes" : "no");
    printf("cells      : %u %u %u mV\n", (unsigned)battery.cells[0],
           (unsigned)battery.cells[1], (unsigned)battery.cells[2]);
    return 0;
}

static int cmd_cells(int fd, const struct options *opt)
{
    uint16_t cells[3];

    if (require_app(fd, ec_delay(opt), ec_retries(opt)) < 0)
    {
        return -1;
    }
    if (ec_get_cells(fd, cells, ec_delay(opt), ec_retries(opt)) < 0)
    {
        return -1;
    }

    if (opt->json)
    {
        printf("{\"ok\":1,\"cells\":[%u,%u,%u]}\n", (unsigned)cells[0],
               (unsigned)cells[1], (unsigned)cells[2]);
        return 0;
    }

    if (opt->quiet)
    {
        printf("%u %u %u\n", (unsigned)cells[0], (unsigned)cells[1],
               (unsigned)cells[2]);
        return 0;
    }

    printf("cell0: %u mV\ncell1: %u mV\ncell2: %u mV\n", (unsigned)cells[0],
           (unsigned)cells[1], (unsigned)cells[2]);
    return 0;
}

static int cmd_temp(int fd, const struct options *opt)
{
    int16_t temp_c10;

    if (require_app(fd, ec_delay(opt), ec_retries(opt)) < 0)
    {
        return -1;
    }
    if (ec_get_temp(fd, &temp_c10, ec_delay(opt), ec_retries(opt)) < 0)
    {
        return -1;
    }

    if (opt->json)
    {
        printf("{\"ok\":1,\"temp_c10\":%d}\n", (int)temp_c10);
        return 0;
    }

    if (opt->quiet)
    {
        printf("%d\n", (int)temp_c10);
        return 0;
    }

    char formatted[32];
    print_centi(formatted, sizeof(formatted), temp_c10, 10, 'C');
    printf("temp_c10 : %d (%s)\n", (int)temp_c10, formatted);
    return 0;
}

static int cmd_ina226(const struct options *opt)
{
    struct ina226_sample sample;

    if (ina226_read(opt->dev, opt->ina_addr, opt->shunt_mohm, opt->ina_channel,
                    &sample) < 0)
    {
        return -1;
    }

    if (opt->json)
    {
        printf("{\"ok\":1,\"ina226\":{\"ok\":1,\"chip\":\"%s\",\"channel\":%u,"
               "\"bus_mv\":%lu,\"bus_uv\":%lu,"
               "\"shunt_nv\":%ld,\"current_ma\":%ld,\"power_mw\":%lu}}\n",
               sample.chip_name, (unsigned)sample.channel,
               (unsigned long)sample.bus_mv, (unsigned long)sample.bus_uv,
               (long)sample.shunt_nv, (long)sample.current_ma,
               (unsigned long)sample.power_mw);
        return 0;
    }

    if (opt->quiet)
    {
        printf("%lu %lu %ld %ld %lu\n", (unsigned long)sample.bus_uv,
               (unsigned long)sample.bus_mv, (long)sample.shunt_nv,
               (long)sample.current_ma, (unsigned long)sample.power_mw);
        return 0;
    }

    printf("chip       : %s (channel %u)\n", sample.chip_name,
           (unsigned)sample.channel);
    printf("bus_uv     : %lu uV\n", (unsigned long)sample.bus_uv);
    printf("bus_mv     : %lu mV\n", (unsigned long)sample.bus_mv);
    printf("shunt_nv   : %ld nV\n", (long)sample.shunt_nv);
    printf("current_ma : %ld mA\n", (long)sample.current_ma);
    printf("power_mw   : %lu mW\n", (unsigned long)sample.power_mw);
    return 0;
}

static int cmd_sht21(const struct options *opt)
{
    struct sht21_sample sample;

    if (sht21_read(opt->dev, opt->sht_addr, &sample) < 0)
    {
        return -1;
    }

    if (opt->json)
    {
        printf("{\"ok\":1,\"sht21\":{\"ok\":1,\"temp_c100\":%ld,"
               "\"humidity_rh10\":%ld}}\n",
               (long)sample.temp_c100, (long)sample.humidity_rh10);
        return 0;
    }

    if (opt->quiet)
    {
        printf("%ld %ld\n", (long)sample.temp_c100, (long)sample.humidity_rh10);
        return 0;
    }

    char temp[32];
    print_centi(temp, sizeof(temp), sample.temp_c100, 100, 'C');
    printf("temp_c100     : %ld (%s)\n", (long)sample.temp_c100, temp);
    printf("humidity_rh10 : %ld (%ld.%ld %%)\n", (long)sample.humidity_rh10,
           (long)(sample.humidity_rh10 / 10), (long)(sample.humidity_rh10 % 10));
    return 0;
}

static void print_snapshot_text(const struct oko_snapshot *snap)
{
    char temp[32];

    if (snap->ec.ok)
    {
        printf("[ec] voltage_mv=%u current_ma=%d temp_c10=%d soc=%u status=0x%02X "
               "cells=%u,%u,%u\n",
               (unsigned)snap->ec.voltage_mv, (int)snap->ec.current_ma,
               (int)snap->ec.temp_c10, (unsigned)snap->ec.soc,
               (unsigned)snap->ec.status, (unsigned)snap->ec.cells[0],
               (unsigned)snap->ec.cells[1], (unsigned)snap->ec.cells[2]);
    }
    else
    {
        printf("[ec] unavailable\n");
    }

    if (snap->ina226.ok)
    {
        printf("[%s] channel=%u bus_mv=%lu bus_uv=%lu shunt_nv=%ld current_ma=%ld "
               "power_mw=%lu\n",
               snap->ina226.chip, (unsigned)snap->ina226.channel,
               (unsigned long)snap->ina226.bus_mv,
               (unsigned long)snap->ina226.bus_uv,
               (long)snap->ina226.shunt_nv, (long)snap->ina226.current_ma,
               (unsigned long)snap->ina226.power_mw);
    }
    else
    {
        printf("[ina] unavailable\n");
    }

    if (snap->sht21.ok)
    {
        print_centi(temp, sizeof(temp), snap->sht21.temp_c100, 100, 'C');
        printf("[sht21] temp_c100=%ld (%s) humidity_rh10=%ld\n",
               (long)snap->sht21.temp_c100, temp,
               (long)snap->sht21.humidity_rh10);
    }
    else
    {
        printf("[sht21] unavailable\n");
    }
}

static int cmd_sensors(const struct options *opt)
{
    struct oko_snapshot snap;

    snapshot_collect(&snap, opt->dev, opt->ec_addr, opt->ina_addr, opt->sht_addr,
                     opt->shunt_mohm, opt->ina_channel, ec_delay(opt),
                     ec_retries(opt));

    int any_ok = snap.ec.ok || snap.ina226.ok || snap.sht21.ok;

    if (opt->json)
    {
        snapshot_print_json(stdout, &snap, SNAP_ALL, 0);
        return any_ok ? 0 : 1;
    }

    if (opt->quiet)
    {
        printf("%u %d %d %u %u %u %u %u\n", (unsigned)snap.ec.voltage_mv,
               (int)snap.ec.current_ma, (int)snap.ec.temp_c10,
               (unsigned)snap.ec.soc, (unsigned)snap.ec.status,
               (unsigned)snap.ec.cells[0], (unsigned)snap.ec.cells[1],
               (unsigned)snap.ec.cells[2]);
        printf("%lu %lu %ld %ld %lu\n", (unsigned long)snap.ina226.bus_uv,
               (unsigned long)snap.ina226.bus_mv, (long)snap.ina226.shunt_nv,
               (long)snap.ina226.current_ma, (unsigned long)snap.ina226.power_mw);
        printf("%ld %ld\n", (long)snap.sht21.temp_c100,
               (long)snap.sht21.humidity_rh10);
        return any_ok ? 0 : 1;
    }

    print_snapshot_text(&snap);
    return any_ok ? 0 : 1;
}

static void json_add_device(int *first, unsigned addr, const char *name)
{
    printf("%s{\"addr\":%u,\"name\":", *first ? "" : ",", addr);
    if (name)
    {
        printf("\"%s\"", name);
    }
    else
    {
        printf("null");
    }
    printf("}");
    *first = 0;
}

static int cmd_scan(const struct options *opt)
{
    unsigned delay_us = ec_delay(opt);
    unsigned retries = ec_retries(opt);
    int first = 1;
    int found = 0;
    int fd;

    i2c_transport_set_verbose(0);
    ec_set_verbose(0);
    sensors_set_timing(opt->delay_us, opt->retries);

    if (access(opt->dev, R_OK | W_OK) != 0)
    {
        if (opt->json)
        {
            printf("{\"ok\":0,\"devices\":[]}\n");
        }
        else
        {
            fprintf(stderr, "oko-ec: cannot open %s: %s\n", opt->dev, strerror(errno));
        }
        return -1;
    }

    if (opt->json)
    {
        printf("{\"ok\":1,\"devices\":[");
    }
    else
    {
        printf("Scanning %s:\n", opt->dev);
    }

    fd = i2c_transport_open(opt->dev, opt->ec_addr);
    int ec_present = 0;
    if (fd >= 0)
    {
        struct ec_info info;
        ec_present = ec_get_info(fd, &info, delay_us, retries) == 0;
        close(fd);
    }
    if (ec_present)
    {
        found++;
        if (opt->json)
        {
            json_add_device(&first, opt->ec_addr, "ec");
        }
        else
        {
            printf("0x%02X ec      present (STM32 ACPI-EC)\n", (unsigned)opt->ec_addr);
        }
    }
    else if (!opt->json)
    {
        printf("0x%02X ec      absent\n", (unsigned)opt->ec_addr);
    }

    struct ina226_sample ina;
    int ina_present = ina226_read(opt->dev, opt->ina_addr, opt->shunt_mohm,
                                  opt->ina_channel, &ina) == 0;
    const char *ina_name = ina_present ? (ina.chip == INA_CHIP_3221 ? "ina3221" : "ina226")
                                       : "ina226";
    if (ina_present)
    {
        found++;
        if (opt->json)
        {
            json_add_device(&first, opt->ina_addr, ina_name);
        }
        else
        {
            printf("0x%02X %-7s present (%s monitor, channel %u)\n",
                   (unsigned)opt->ina_addr, ina_name,
                   ina.chip == INA_CHIP_3221 ? "INA3221 triple-channel" : "INA226",
                   (unsigned)ina.channel);
        }
    }
    else if (!opt->json)
    {
        printf("0x%02X ina     absent\n", (unsigned)opt->ina_addr);
    }

    struct sht21_sample sht;
    int sht_present = sht21_read(opt->dev, opt->sht_addr, &sht) == 0;
    if (sht_present)
    {
        found++;
        if (opt->json)
        {
            json_add_device(&first, opt->sht_addr, "sht21");
        }
        else
        {
            printf("0x%02X sht21   present (SHT21 temperature/humidity)\n",
                   (unsigned)opt->sht_addr);
        }
    }
    else if (!opt->json)
    {
        printf("0x%02X sht21   absent\n", (unsigned)opt->sht_addr);
    }

    for (unsigned addr = 0x03; addr <= 0x77; addr++)
    {
        if (addr == opt->ec_addr || addr == opt->ina_addr || addr == opt->sht_addr)
        {
            continue;
        }
        if (i2c_transport_probe(opt->dev, (uint8_t)addr) == 1)
        {
            found++;
            if (opt->json)
            {
                json_add_device(&first, addr, NULL);
            }
            else
            {
                printf("0x%02X unknown present\n", addr);
            }
        }
    }

    if (opt->json)
    {
        printf("]}\n");
    }
    else if (!found)
    {
        printf("no devices found\n");
    }

    return 0;
}

/* ========================================================================
 * Прошивка EC (OTA)
 * ======================================================================== */

static uint8_t *read_file(const char *path, uint32_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        fprintf(stderr, "Cannot open file: %s\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len <= 0)
    {
        fprintf(stderr, "File is empty\n");
        fclose(f);
        return NULL;
    }

    uint8_t *buf = malloc((size_t)len);
    if (!buf)
    {
        fprintf(stderr, "Out of memory\n");
        fclose(f);
        return NULL;
    }

    if (fread(buf, 1, (size_t)len, f) != (size_t)len)
    {
        fprintf(stderr, "Read error\n");
        free(buf);
        fclose(f);
        return NULL;
    }

    fclose(f);
    *size = (uint32_t)len;
    return buf;
}

static int cmd_flash_blob(int fd, uint8_t *data, uint32_t file_size, uint32_t version,
                          const char *file_path)
{
    uint32_t crc = crc32_sw(data, file_size);

    printf("File    : %s\n", file_path);
    printf("Size    : %u bytes (%u.%02u KB)\n", file_size, file_size / 1024,
           (file_size % 1024) * 100 / 1024);
    printf("CRC32   : 0x%08X\n", crc);
    printf("Version : 0x%08X\n\n", version);

    uint8_t wbuf[260];
    uint8_t rbuf[I2C_READ_BUF_SIZE];
    uint8_t status;

    /* ---- BEGIN_UPDATE: len(4) + crc(4) + version(4) ---- */
    put_le16(wbuf, 13);
    wbuf[2] = CMD_BEGIN_UPDATE;
    put_le32(&wbuf[3], file_size);
    put_le32(&wbuf[7], crc);
    put_le32(&wbuf[11], version);

    printf("Sending BEGIN_UPDATE...\n");
    if (ec_io(fd, wbuf, 15, rbuf, I2C_READ_BUF_SIZE) < 0)
    {
        return -1;
    }

    status = rbuf[2];
    printf("Status: 0x%02X (%s)\n", status, status_str(status));

    if (status != ST_OK)
    {
        return -1;
    }

    /* ---- WRITE_DATA ---- */
    uint32_t offset = 0;
    uint32_t total_chunks = (file_size + CHUNK_SIZE - 1) / CHUNK_SIZE;

    printf("Writing %u chunks (chunk size: %u bytes)...\n", total_chunks, CHUNK_SIZE);

    for (uint32_t chunk = 0; chunk < total_chunks; chunk++)
    {
        uint32_t chunk_len = file_size - offset;
        if (chunk_len > CHUNK_SIZE)
        {
            chunk_len = CHUNK_SIZE;
        }

        put_le16(wbuf, (uint16_t)(6 + chunk_len));
        wbuf[2] = CMD_WRITE_DATA;
        put_le32(&wbuf[3], offset);
        wbuf[7] = (uint8_t)chunk_len;
        memcpy(&wbuf[8], &data[offset], chunk_len);

        if (ec_io(fd, wbuf, 8 + chunk_len, rbuf, I2C_READ_BUF_SIZE) < 0)
        {
            return -1;
        }

        status = rbuf[2];
        if (status != ST_OK)
        {
            fprintf(stderr, "\nWrite failed at offset %u: 0x%02X (%s)\n", offset, status,
                    status_str(status));
            return -1;
        }

        offset += chunk_len;

        printf("\rProgress: %u / %u bytes (%u%%)", offset, file_size,
               offset * 100u / file_size);
        fflush(stdout);
    }

    printf("\n");

    /* ---- VERIFY_UPDATE ---- */
    put_le16(wbuf, 1);
    wbuf[2] = CMD_VERIFY_UPDATE;

    printf("Verifying...\n");
    if (ec_io(fd, wbuf, 3, rbuf, I2C_READ_BUF_SIZE) < 0)
    {
        return -1;
    }

    status = rbuf[2];
    printf("Verify status: 0x%02X (%s)\n", status, status_str(status));

    if (status != ST_OK)
    {
        return -1;
    }

    /* ---- COMMIT: единственный слот всегда активируется ---- */
    put_le16(wbuf, 1);
    wbuf[2] = CMD_COMMIT;

    printf("Committing...\n");
    if (ec_io(fd, wbuf, 3, rbuf, I2C_READ_BUF_SIZE) < 0)
    {
        return -1;
    }

    status = rbuf[2];
    printf("Commit status: 0x%02X (%s)\n", status, status_str(status));

    if (status != ST_OK)
    {
        return -1;
    }

    printf("\nFirmware flashed successfully!\n");
    return 0;
}

/* EC-путь: линейный .bin, дальше общий код */
static int cmd_flash(int fd, const char *file_path, uint32_t version)
{
    uint32_t size;
    uint8_t *data = read_file(file_path, &size);
    if (!data)
    {
        return -1;
    }
    int rc = cmd_flash_blob(fd, data, size, version, file_path);
    free(data);
    return rc;
}

static int cmd_enter_update(int fd)
{
    uint8_t wbuf[3];
    uint8_t rbuf[I2C_READ_BUF_SIZE];

    put_le16(wbuf, 1);
    wbuf[2] = CMD_ENTER_UPDATE;

    printf("Отправка команды входа в режим обновления...\n");

    if (ec_io(fd, wbuf, 3, rbuf, I2C_READ_BUF_SIZE) < 0)
    {
        /* Прошивка могла перезагрузиться до отправки ответа */
        printf("Ответ не получен (возможно, устройство уже перезагружается)\n");
    }
    else
    {
        uint8_t status = rbuf[2];
        printf("Статус: 0x%02X (%s)\n", status, status_str(status));
        if (status != ST_OK)
        {
            return -1;
        }
    }

    /* Ждём, пока устройство перезагрузится в режим обновления */
    printf("Ожидание перезагрузки в режим обновления (3 сек)...\n");
    usleep(3000000);

    return 0;
}

static int cmd_reset(int fd)
{
    uint8_t wbuf[3];

    put_le16(wbuf, 1);
    wbuf[2] = CMD_RESET;

    printf("Sending RESET command...\n");

    if (i2c_transport_write_only(fd, wbuf, 3) < 0)
    {
        return -1;
    }

    printf("Reset sent.\n");
    return 0;
}

/* ========================================================================
 * Цель motor: STM8S103F3P6
 *   — через I2C (тот же протокол v2, адрес 0x39): info/status/flash/
 *     enter-update/reset — flash принимает .ihx (SDCC) или .bin;
 *   — через ST-Link (stm8flash) для первичной прошивки: motor stlink.
 * ======================================================================== */

/* Образ приложения STM8 всегда начинается с базового адреса слота */
#define MOTOR_APP_BASE 0x9200u

/*
 * Разбор Intel HEX (.ihx, вывод SDCC/ASIX) в линейный образ.
 * Пробелы заполняются 0x00 — значением СТЁРТОЙ Flash у STM8S!
 * Возвращает данные (malloc) или NULL; base/size — границы образа.
 */
static uint8_t *read_ihx(const char *path, uint32_t *base, uint32_t *size)
{
    FILE *f = fopen(path, "r");
    if (!f)
    {
        fprintf(stderr, "Cannot open file: %s\n", path);
        return NULL;
    }

    uint32_t lo = 0xFFFFFFFFu, hi = 0;
    uint32_t seg_base = 0; /* для расширенных записей 04 */
    char line[1024];
    uint8_t *tmp = NULL; /* второй проход упрощён: два прохода */

    for (int pass = 0; pass < 2; pass++)
    {
        rewind(f);
        if (pass == 1)
        {
            if (lo == 0xFFFFFFFFu || hi == 0 || hi < lo)
            {
                fprintf(stderr, "Пустой или битый .ihx: %s\n", path);
                return NULL;
            }
            uint32_t n = hi - lo + 1;
            tmp = malloc(n);
            if (!tmp)
            {
                fclose(f);
                return NULL;
            }
            memset(tmp, 0x00, n); /* стёртое состояние STM8 = 0x00 */
            seg_base = 0;
        }

        while (fgets(line, sizeof(line), f))
        {
            if (line[0] != ':')
                continue;
            int ln = (int)strtol(line + 1, NULL, 16);
            int addr = (int)strtol(line + 3, NULL, 16);
            int typ = (int)strtol(line + 7, NULL, 16);
            if (typ == 5)
                break;
            if (typ != 0 && typ != 1 && typ != 2 && typ != 4)
                continue;
            uint32_t a;
            if (typ == 0)
            {
                a = seg_base + (uint32_t)addr;
                for (int i = 0; i < ln; i++)
                {
                    uint32_t v = (uint32_t)strtol(line + 9 + 2 * i, NULL, 16);
                    if (pass == 0)
                    {
                        if (a + i < lo)
                            lo = a + i;
                        if (a + i > hi)
                            hi = a + i;
                    }
                    else
                    {
                        tmp[a + i - lo] = (uint8_t)v;
                    }
                }
            }
            else if (typ == 2)
            { /* extended segment address */
                seg_base = ((uint32_t)strtol(line + 9, NULL, 16)) << 4;
            }
            else if (typ == 4)
            { /* extended linear address */
                seg_base = ((uint32_t)strtol(line + 9, NULL, 16)) << 16;
            }
        }
    }
    fclose(f);
    *base = lo;
    *size = hi - lo + 1;
    return tmp;
}

/*
 * I2C-прошивка STM8: принимает .ihx (преобразует в линейный образ слота)
 * или .bin (уже линейный образ, начинающийся с APP_BASE).
 */
static int cmd_flash_motor(int fd, const char *file_path, uint32_t version)
{
    uint8_t *blob;
    uint32_t size;

    if (strstr(file_path, ".ihx"))
    {
        uint32_t base;
        blob = read_ihx(file_path, &base, &size);
        if (!blob)
            return -1;
        if (base != MOTOR_APP_BASE)
        {
            fprintf(stderr,
                    "Образ .ihx начинается с 0x%04X, а слот приложения — 0x%04X.\n"
                    "Пересоберите приложение: make (ld: HOME=0x9200).\n",
                    base, MOTOR_APP_BASE);
            free(blob);
            return -1;
        }
        printf("Образ .ihx: база 0x%04X, %u байт\n", base, size);
    }
    else
    {
        blob = read_file(file_path, &size);
        if (!blob)
            return -1;
    }

    int rc = cmd_flash_blob(fd, blob, size, version, file_path);
    free(blob);
    return rc;
}

/* ========================================================================
 * Цель motor (ST-Link), сохранена как 'motor stlink'
 * ======================================================================== */

static int motor_flash_stlink(const char *file_path, bool verify)
{
    const char *stm8flash_path = getenv("STM8FLASH");
    if (!stm8flash_path || !stm8flash_path[0])
    {
        stm8flash_path = "stm8flash";
    }

    /* -w записывает образ; при необходимости отдельно проверяем через -v. */
    pid_t pid = fork();
    if (pid < 0)
    {
        fprintf(stderr, "fork: %s\n", strerror(errno));
        return -1;
    }

    if (pid == 0)
    {
        execlp(stm8flash_path, stm8flash_path, "-c", "stlinkv2", "-p", "stm8s103f3", "-w",
               file_path, (char *)NULL);
        fprintf(stderr,
                "Не удалось запустить %s: %s\n"
                "Установите stm8flash или задайте путь в переменной "
                "окружения STM8FLASH.\n",
                stm8flash_path, strerror(errno));
        _exit(127);
    }

    int wstatus = 0;
    if (waitpid(pid, &wstatus, 0) < 0)
    {
        fprintf(stderr, "waitpid: %s\n", strerror(errno));
        return -1;
    }

    if (!WIFEXITED(wstatus) || WEXITSTATUS(wstatus) != 0)
    {
        fprintf(stderr, "stm8flash завершился с ошибкой\n");
        return -1;
    }

    printf("STM8: образ %s записан\n", file_path);

    if (!verify)
    {
        return 0;
    }

    pid = fork();
    if (pid < 0)
    {
        fprintf(stderr, "fork: %s\n", strerror(errno));
        return -1;
    }

    if (pid == 0)
    {
        execlp(stm8flash_path, stm8flash_path, "-c", "stlinkv2", "-p", "stm8s103f3", "-v",
               file_path, (char *)NULL);
        fprintf(stderr, "Не удалось запустить %s: %s\n", stm8flash_path, strerror(errno));
        _exit(127);
    }

    wstatus = 0;
    if (waitpid(pid, &wstatus, 0) < 0)
    {
        fprintf(stderr, "waitpid: %s\n", strerror(errno));
        return -1;
    }

    if (!WIFEXITED(wstatus) || WEXITSTATUS(wstatus) != 0)
    {
        fprintf(stderr, "STM8: верификация завершилась с ошибкой\n");
        return -1;
    }

    printf("STM8: верификация пройдена\n");
    return 0;
}

/* ========================================================================
 * Использование
 * ======================================================================== */

static void print_usage(const char *prog)
{
    printf("Утилита OKO: STM32 ACPI-EC, INA226 и SHT21 на I2C\n\n"
           "Использование:\n"
           "  %s [опции] <команда> [аргументы]\n"
           "\nКоманды:\n"
           "  info                       - информация об EC (загрузчик или прошивка)\n"
           "  status                     - статус обновления (загрузчик)\n"
           "  battery                    - напряжение, ток, температура, SOC, ячейки\n"
           "  cells                      - напряжения трёх ячеек, мВ\n"
           "  temp                       - температура EC, 0.1 °C\n"
           "  ina226                     - INA226: bus/shunt/ток/мощность\n"
           "  sht21                      - SHT21: температура и влажность\n"
           "  sensors                    - EC + INA226 + SHT21 одним блоком\n"
           "  scan                       - поиск устройств на шине\n"
           "  flash <file.bin> <version> - прошивка STM32 через загрузчик\n"
           "  enter-update               - перевод прошивки в режим обновления\n"
           "  reset                      - перезагрузка устройства\n"
           "  daemon [--interval N] [--once]\n"
           "                             - демон: /tmp/oko-ec.json и /tmp/oko-ec.osd\n"
           "  ec <i2c-dev> info|status|flash|enter-update|reset\n"
           "                             - совместимость с bootloader_tool\n"
           "  motor stlink <file.ihx> [--verify]\n"
           "                             - первичная прошивка STM8 (ST-Link)\n"
           "\nОпции:\n"
           "  --dev <path>         шина I2C (env OKO_EC_DEV, по умолчанию %s)\n"
           "  --ec-addr <n>        адрес STM32 (по умолчанию 0x42)\n"
           "  --ina-addr <n>       адрес INA226/INA3221 (по умолчанию 0x41)\n"
           "  --ina-channel <n>    канал INA3221 1..3 (по умолчанию 1)\n"
           "  --sht-addr <n>       адрес SHT21 (по умолчанию 0x40)\n"
           "  --shunt-mohm <n>     сопротивление шунта INA (по умолчанию 100)\n"
           "  --delay-us <n>       задержка между записью и чтением, мкс\n"
           "  --retries <n>        число попыток чтения\n"
           "  --json               вывод одной строкой JSON\n"
           "  --quiet              только значения без пояснений\n"
           "\nПримеры:\n"
           "  %s scan\n"
           "  %s info\n"
           "  %s battery --json\n"
           "  %s sensors\n"
           "  %s flash build/app.bin 0x00010001\n"
           "  %s daemon --interval 2\n",
           prog, EC_DEFAULT_DEV, prog, prog, prog, prog, prog, prog);
}

/* ========================================================================
 * Разбор опций
 * ======================================================================== */

/* 0 — не совпало, 1 — совпало, -1 — ошибка (нет значения). */
static int option_value(int argc, char **argv, int *index, const char *arg,
                        const char *name, const char **value)
{
    size_t n = strlen(name);

    if (strncmp(arg, name, n) != 0 || (arg[n] != '\0' && arg[n] != '='))
    {
        return 0;
    }

    if (arg[n] == '=')
    {
        *value = &arg[n + 1];
        return 1;
    }

    if (*index + 1 >= argc)
    {
        fprintf(stderr, "oko-ec: %s requires a value\n", name);
        return -1;
    }

    *value = argv[++(*index)];
    return 1;
}

static int parse_number(const char *text, unsigned long *out)
{
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 0);
    if (end == text || *end != '\0')
    {
        return -1;
    }
    *out = value;
    return 0;
}

static int parse_options(int argc, char **argv, int *first_arg, struct options *opt)
{
    const char *env_dev = getenv("OKO_EC_DEV");

    memset(opt, 0, sizeof(*opt));
    opt->dev = (env_dev && env_dev[0]) ? env_dev : EC_DEFAULT_DEV;
    opt->ec_addr = I2C_ADDR7;
    opt->ina_addr = 0x41;
    opt->ina_channel = 1;
    opt->sht_addr = 0x40;
    opt->shunt_mohm = 100;

    const char *env_shunt = getenv("OKO_EC_SHUNT_MOHM");
    if (env_shunt && env_shunt[0])
    {
        unsigned long value;
        if (parse_number(env_shunt, &value) == 0 && value > 0)
        {
            opt->shunt_mohm = (uint32_t)value;
        }
    }

    const char *env_channel = getenv("OKO_EC_INA_CHANNEL");
    if (env_channel && env_channel[0])
    {
        unsigned long value;
        if (parse_number(env_channel, &value) == 0 && value >= 1 && value <= 3)
        {
            opt->ina_channel = (uint8_t)value;
        }
    }

    int i = 1;
    for (; i < argc; i++)
    {
        const char *arg = argv[i];
        const char *value = NULL;
        int matched;

        if (arg[0] != '-' || strcmp(arg, "-") == 0)
        {
            break;
        }
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0)
        {
            break;
        }
        if (strcmp(arg, "--json") == 0)
        {
            opt->json = 1;
            continue;
        }
        if (strcmp(arg, "--quiet") == 0)
        {
            opt->quiet = 1;
            continue;
        }

        matched = option_value(argc, argv, &i, arg, "--dev", &value);
        if (matched < 0)
        {
            return -1;
        }
        if (matched > 0)
        {
            opt->dev = value;
            continue;
        }

        matched = option_value(argc, argv, &i, arg, "--ec-addr", &value);
        if (matched < 0)
        {
            return -1;
        }
        if (matched > 0)
        {
            unsigned long n;
            if (parse_number(value, &n) != 0 || n > 0x7F)
            {
                fprintf(stderr, "oko-ec: invalid --ec-addr\n");
                return -1;
            }
            opt->ec_addr = (uint8_t)n;
            continue;
        }

        matched = option_value(argc, argv, &i, arg, "--ina-addr", &value);
        if (matched < 0)
        {
            return -1;
        }
        if (matched > 0)
        {
            unsigned long n;
            if (parse_number(value, &n) != 0 || n > 0x7F)
            {
                fprintf(stderr, "oko-ec: invalid --ina-addr\n");
                return -1;
            }
            opt->ina_addr = (uint8_t)n;
            continue;
        }

        matched = option_value(argc, argv, &i, arg, "--ina-channel", &value);
        if (matched < 0)
        {
            return -1;
        }
        if (matched > 0)
        {
            unsigned long n;
            if (parse_number(value, &n) != 0 || n < 1 || n > 3)
            {
                fprintf(stderr, "oko-ec: invalid --ina-channel\n");
                return -1;
            }
            opt->ina_channel = (uint8_t)n;
            continue;
        }

        matched = option_value(argc, argv, &i, arg, "--sht-addr", &value);
        if (matched < 0)
        {
            return -1;
        }
        if (matched > 0)
        {
            unsigned long n;
            if (parse_number(value, &n) != 0 || n > 0x7F)
            {
                fprintf(stderr, "oko-ec: invalid --sht-addr\n");
                return -1;
            }
            opt->sht_addr = (uint8_t)n;
            continue;
        }

        matched = option_value(argc, argv, &i, arg, "--shunt-mohm", &value);
        if (matched < 0)
        {
            return -1;
        }
        if (matched > 0)
        {
            unsigned long n;
            if (parse_number(value, &n) != 0 || n == 0)
            {
                fprintf(stderr, "oko-ec: invalid --shunt-mohm\n");
                return -1;
            }
            opt->shunt_mohm = (uint32_t)n;
            continue;
        }

        matched = option_value(argc, argv, &i, arg, "--delay-us", &value);
        if (matched < 0)
        {
            return -1;
        }
        if (matched > 0)
        {
            unsigned long n;
            if (parse_number(value, &n) != 0)
            {
                fprintf(stderr, "oko-ec: invalid --delay-us\n");
                return -1;
            }
            opt->delay_us = (unsigned)n;
            continue;
        }

        matched = option_value(argc, argv, &i, arg, "--retries", &value);
        if (matched < 0)
        {
            return -1;
        }
        if (matched > 0)
        {
            unsigned long n;
            if (parse_number(value, &n) != 0 || n == 0)
            {
                fprintf(stderr, "oko-ec: invalid --retries\n");
                return -1;
            }
            opt->retries = (unsigned)n;
            continue;
        }

        fprintf(stderr, "oko-ec: unknown option %s\n", arg);
        return -1;
    }

    *first_arg = i;
    return 0;
}

static int open_ec(const char *dev, uint8_t addr)
{
    int fd = i2c_transport_open(dev, addr);
    if (fd < 0)
    {
        return -1;
    }
    return fd;
}

/* ========================================================================
 * main
 * ======================================================================== */

int main(int argc, char **argv)
{
    struct options opt;
    int index = 0;

    if (parse_options(argc, argv, &index, &opt) < 0)
    {
        return 1;
    }

    if (index >= argc)
    {
        print_usage(argv[0]);
        return 1;
    }

    g_ec_delay_us = ec_delay(&opt);
    g_ec_retries = ec_retries(&opt);
    sensors_set_timing(opt.delay_us, opt.retries);

    if (opt.quiet)
    {
        i2c_transport_set_verbose(0);
        ec_set_verbose(0);
    }

    const char *command = argv[index];

    if (strcmp(command, "--help") == 0 || strcmp(command, "-h") == 0)
    {
        print_usage(argv[0]);
        return 0;
    }

    /* ---------------- Совместимость: ec <i2c-dev> <cmd> ---------------- */
    if (strcmp(command, "ec") == 0)
    {
        if (argc < index + 3)
        {
            print_usage(argv[0]);
            return 1;
        }

        const char *device = argv[index + 1];
        const char *sub = argv[index + 2];

        int fd = open_ec(device, opt.ec_addr);
        if (fd < 0)
        {
            return 1;
        }

        int ret = -1;

        if (strcmp(sub, "info") == 0)
        {
            ret = cmd_info(fd, &opt);
        }
        else if (strcmp(sub, "status") == 0)
        {
            ret = cmd_status(fd, &opt);
        }
        else if (strcmp(sub, "battery") == 0)
        {
            ret = cmd_battery(fd, &opt);
        }
        else if (strcmp(sub, "cells") == 0)
        {
            ret = cmd_cells(fd, &opt);
        }
        else if (strcmp(sub, "temp") == 0)
        {
            ret = cmd_temp(fd, &opt);
        }
        else if (strcmp(sub, "flash") == 0)
        {
            if (argc < index + 5)
            {
                fprintf(stderr, "Usage: %s ec <device> flash <file> <version>\n", argv[0]);
            }
            else
            {
                uint32_t version = (uint32_t)strtoul(argv[index + 4], NULL, 0);
                ret = cmd_flash(fd, argv[index + 3], version);
            }
        }
        else if (strcmp(sub, "enter-update") == 0)
        {
            ret = cmd_enter_update(fd);
        }
        else if (strcmp(sub, "reset") == 0)
        {
            ret = cmd_reset(fd);
        }
        else
        {
            fprintf(stderr, "Unknown ec command: %s\n\n", sub);
            print_usage(argv[0]);
        }

        close(fd);
        return (ret == 0) ? 0 : 1;
    }

    /* ---------------- Цель: motor (STM8) ---------------- */
    if (strcmp(command, "motor") == 0)
    {
        /* Первичная прошивка по ST-Link (SWIM) */
        if (argc >= index + 3 && strcmp(argv[index + 1], "stlink") == 0)
        {
            bool verify = (argc >= index + 4 && strcmp(argv[index + 3], "--verify") == 0);
            return motor_flash_stlink(argv[index + 2], verify) == 0 ? 0 : 1;
        }

        /* Обмен по I2C: тот же протокол v2, адрес 0x39 */
        if (argc >= index + 3)
        {
            const char *device = argv[index + 1];
            const char *sub = argv[index + 2];

            int fd = i2c_transport_open(device, I2C_ADDR_MOTOR);
            if (fd < 0)
            {
                return 1;
            }

            int ret = -1;

            if (strcmp(sub, "info") == 0)
            {
                ret = cmd_info(fd, &opt);
            }
            else if (strcmp(sub, "status") == 0)
            {
                ret = cmd_status(fd, &opt);
            }
            else if (strcmp(sub, "flash") == 0)
            {
                if (argc < index + 5)
                {
                    fprintf(stderr,
                            "Usage: %s motor <device> flash <file.ihx|file.bin> <version>\n",
                            argv[0]);
                }
                else
                {
                    uint32_t version = (uint32_t)strtoul(argv[index + 4], NULL, 0);
                    ret = cmd_flash_motor(fd, argv[index + 3], version);
                }
            }
            else if (strcmp(sub, "enter-update") == 0)
            {
                ret = cmd_enter_update(fd);
            }
            else if (strcmp(sub, "reset") == 0)
            {
                ret = cmd_reset(fd);
            }
            else
            {
                fprintf(stderr, "Unknown motor command: %s\n\n", sub);
                print_usage(argv[0]);
            }

            close(fd);
            return (ret == 0) ? 0 : 1;
        }

        print_usage(argv[0]);
        return 1;
    }

    /* ---------------- Цель: EC и датчики ---------------- */
    if (strcmp(command, "scan") == 0)
    {
        return cmd_scan(&opt) == 0 ? 0 : 1;
    }

    if (strcmp(command, "ina226") == 0)
    {
        return cmd_ina226(&opt) == 0 ? 0 : 1;
    }

    if (strcmp(command, "sht21") == 0)
    {
        return cmd_sht21(&opt) == 0 ? 0 : 1;
    }

    if (strcmp(command, "sensors") == 0)
    {
        return cmd_sensors(&opt) == 0 ? 0 : 1;
    }

    if (strcmp(command, "daemon") == 0)
    {
        unsigned interval = 2;
        int once = 0;

        for (int i = index + 1; i < argc; i++)
        {
            const char *arg = argv[i];
            if (strcmp(arg, "--once") == 0)
            {
                once = 1;
                continue;
            }
            if (strncmp(arg, "--interval", 10) == 0 &&
                (arg[10] == '\0' || arg[10] == '='))
            {
                const char *value;
                if (arg[10] == '=')
                {
                    value = &arg[11];
                }
                else if (i + 1 < argc)
                {
                    value = argv[++i];
                }
                else
                {
                    fprintf(stderr, "oko-ec: --interval requires a value\n");
                    return 1;
                }
                unsigned long n;
                if (parse_number(value, &n) != 0 || n < 1)
                {
                    fprintf(stderr, "oko-ec: invalid --interval\n");
                    return 1;
                }
                interval = (unsigned)n;
                continue;
            }
            fprintf(stderr, "oko-ec: unknown daemon option %s\n", arg);
            return 1;
        }

        return daemon_run(opt.dev, opt.ec_addr, opt.ina_addr, opt.sht_addr,
                          opt.shunt_mohm, opt.ina_channel, ec_delay(&opt),
                          ec_retries(&opt), interval, once);
    }

    int fd = open_ec(opt.dev, opt.ec_addr);
    if (fd < 0)
    {
        return 1;
    }

    int ret;
    if (strcmp(command, "info") == 0)
    {
        ret = cmd_info(fd, &opt);
    }
    else if (strcmp(command, "status") == 0)
    {
        ret = cmd_status(fd, &opt);
    }
    else if (strcmp(command, "battery") == 0)
    {
        ret = cmd_battery(fd, &opt);
    }
    else if (strcmp(command, "cells") == 0)
    {
        ret = cmd_cells(fd, &opt);
    }
    else if (strcmp(command, "temp") == 0)
    {
        ret = cmd_temp(fd, &opt);
    }
    else if (strcmp(command, "flash") == 0)
    {
        if (argc < index + 3)
        {
            fprintf(stderr, "Usage: %s flash <file> <version>\n", argv[0]);
            ret = -1;
        }
        else
        {
            uint32_t version = (uint32_t)strtoul(argv[index + 2], NULL, 0);
            ret = cmd_flash(fd, argv[index + 1], version);
        }
    }
    else if (strcmp(command, "enter-update") == 0)
    {
        ret = cmd_enter_update(fd);
    }
    else if (strcmp(command, "reset") == 0)
    {
        ret = cmd_reset(fd);
    }
    else
    {
        fprintf(stderr, "Unknown command: %s\n\n", command);
        close(fd);
        print_usage(argv[0]);
        return 1;
    }

    close(fd);
    return (ret == 0) ? 0 : 1;
}