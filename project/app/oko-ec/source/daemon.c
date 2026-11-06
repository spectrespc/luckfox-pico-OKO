/* daemon.c — периодический опрос EC/INA226/SHT21 и файлы состояния. */

#define _GNU_SOURCE

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "daemon.h"
#include "ec_lock.h"
#include "ec_proto.h"
#include "i2c_transport.h"
#include "sensors.h"

#define JSON_PATH "/tmp/oko-ec.json"
#define OSD_PATH "/tmp/oko-ec.osd"
#define PID_PATH "/tmp/oko-ec.pid"

static volatile sig_atomic_t stop_flag;
static volatile sig_atomic_t pid_written;

static void on_signal(int signo)
{
    (void)signo;
    if (pid_written)
    {
        unlink(PID_PATH);
        pid_written = 0;
    }
    stop_flag = 1;
}

static void snapshot_clear(struct oko_snapshot *snap)
{
    memset(snap, 0, sizeof(*snap));
}

static void add_error(struct oko_snapshot *snap, const char *name)
{
    if (snap->error_count < (int)(sizeof(snap->errors) / sizeof(snap->errors[0])))
    {
        snap->errors[snap->error_count++] = name;
    }
}

void snapshot_collect(struct oko_snapshot *snap, const char *dev, uint8_t ec_addr,
                      uint8_t ina_addr, uint8_t sht_addr, uint32_t shunt_mohm,
                      uint8_t ina_channel, unsigned delay_us, unsigned retries)
{
    snapshot_clear(snap);
    snap->ts = (long)time(NULL);

    int fd = i2c_transport_open(dev, ec_addr);
    if (fd >= 0)
    {
        struct ec_info info;
        struct ec_battery battery;
        if (ec_get_info(fd, &info, delay_us, retries) == 0)
        {
            snap->ec.info_ok = 1;
            snap->ec.device_type = info.device_type;
            snap->ec.proto = info.proto;
            snap->ec.version = info.version;
            snap->ec.img_size = info.img_size;
            snap->ec.flags = info.flags;
        }
        if (snap->ec.info_ok && info.device_type == EC_DEVICE_APP &&
            ec_get_battery(fd, &battery, delay_us, retries) == 0)
        {
            snap->ec.ok = 1;
            snap->ec.voltage_mv = battery.voltage_mv;
            snap->ec.current_ma = battery.current_ma;
            snap->ec.temp_c10 = battery.temp_c10;
            snap->ec.soc = battery.soc;
            snap->ec.status = battery.status;
            for (int i = 0; i < 3; i++)
            {
                snap->ec.cells[i] = battery.cells[i];
            }
        }
        close(fd);
    }
    if (!snap->ec.ok)
    {
        add_error(snap, "ec");
    }

    struct ina226_sample ina;
    if (ina226_read(dev, ina_addr, shunt_mohm, ina_channel, &ina) == 0)
    {
        snap->ina226.ok = 1;
        snprintf(snap->ina226.chip, sizeof(snap->ina226.chip), "%s",
                 ina.chip_name ? ina.chip_name : "INA");
        snap->ina226.channel = ina.channel;
        snap->ina226.bus_uv = ina.bus_uv;
        snap->ina226.bus_mv = ina.bus_mv;
        snap->ina226.shunt_nv = ina.shunt_nv;
        snap->ina226.current_ma = ina.current_ma;
        snap->ina226.power_mw = ina.power_mw;
    }
    else
    {
        add_error(snap, "ina226");
    }

    struct sht21_sample sht;
    if (sht21_read(dev, sht_addr, &sht) == 0)
    {
        snap->sht21.ok = 1;
        snap->sht21.temp_c100 = sht.temp_c100;
        snap->sht21.humidity_rh10 = sht.humidity_rh10;
    }
    else
    {
        add_error(snap, "sht21");
    }

    snap->ok = snap->ec.ok && snap->ina226.ok && snap->sht21.ok;
}

static void print_i32(FILE *out, int32_t value)
{
    fprintf(out, "%ld", (long)value);
}

static void print_u32(FILE *out, uint32_t value)
{
    fprintf(out, "%lu", (unsigned long)value);
}

void snapshot_print_json(FILE *out, const struct oko_snapshot *snap,
                         unsigned parts, int with_ts)
{
    fprintf(out, "{");
    if (with_ts)
    {
        fprintf(out, "\"ts\":%ld,", snap->ts);
    }
    fprintf(out, "\"ok\":%d", snap->ok);

    if (parts & SNAP_EC)
    {
        fprintf(out, ",\"ec\":{\"ok\":%d,\"info_ok\":%d,\"device_type\":%u,"
                     "\"proto\":%u,\"version\":%lu,\"img_size\":%lu,"
                     "\"flags\":%u,\"voltage_mv\":%u,\"current_ma\":%d,"
                     "\"temp_c10\":%d,\"soc\":%u,\"status\":%u,\"cells\":[%u,%u,%u]}",
                snap->ec.ok, snap->ec.info_ok, (unsigned)snap->ec.device_type,
                (unsigned)snap->ec.proto, (unsigned long)snap->ec.version,
                (unsigned long)snap->ec.img_size, (unsigned)snap->ec.flags,
                (unsigned)snap->ec.voltage_mv, (int)snap->ec.current_ma,
                (int)snap->ec.temp_c10, (unsigned)snap->ec.soc,
                (unsigned)snap->ec.status, (unsigned)snap->ec.cells[0],
                (unsigned)snap->ec.cells[1], (unsigned)snap->ec.cells[2]);
    }

    if (parts & SNAP_INA)
    {
        fprintf(out, ",\"ina226\":{\"ok\":%d,\"chip\":\"%s\",\"channel\":%u,"
                     "\"bus_mv\":",
                snap->ina226.ok,
                snap->ina226.chip[0] ? snap->ina226.chip : "unknown",
                (unsigned)snap->ina226.channel);
        print_u32(out, snap->ina226.bus_mv);
        fprintf(out, ",\"bus_uv\":");
        print_u32(out, snap->ina226.bus_uv);
        fprintf(out, ",\"shunt_nv\":");
        print_i32(out, snap->ina226.shunt_nv);
        fprintf(out, ",\"current_ma\":");
        print_i32(out, snap->ina226.current_ma);
        fprintf(out, ",\"power_mw\":");
        print_u32(out, snap->ina226.power_mw);
        fprintf(out, "}");
    }

    if (parts & SNAP_SHT)
    {
        fprintf(out, ",\"sht21\":{\"ok\":%d,\"temp_c100\":", snap->sht21.ok);
        print_i32(out, snap->sht21.temp_c100);
        fprintf(out, ",\"humidity_rh10\":");
        print_i32(out, snap->sht21.humidity_rh10);
        fprintf(out, "}");
    }

    fprintf(out, ",\"errors\":[");
    for (int i = 0; i < snap->error_count; i++)
    {
        fprintf(out, "%s\"%s\"", i ? "," : "", snap->errors[i]);
    }
    fprintf(out, "]}\n");
}

static int write_json(const struct oko_snapshot *snap)
{
    char tmp[] = JSON_PATH ".tmp";
    FILE *out = fopen(tmp, "w");
    if (!out)
    {
        return -1;
    }

    snapshot_print_json(out, snap, SNAP_ALL, 1);
    if (fclose(out) != 0)
    {
        unlink(tmp);
        return -1;
    }
    if (rename(tmp, JSON_PATH) < 0)
    {
        unlink(tmp);
        return -1;
    }
    return 0;
}

static void format_temp_c100(char *buf, size_t size, int32_t t)
{
    int32_t frac = t % 100;
    const char *sign = "";
    if (frac < 0)
    {
        frac = -frac;
    }
    if (t < 0 && t > -100)
    {
        sign = "-";
    }
    snprintf(buf, size, "%s%ld.%02ldC", sign, (long)(t / 100), (long)frac);
}

static void format_temp_c10(char *buf, size_t size, int32_t t)
{
    int32_t frac = t % 10;
    const char *sign = "";
    if (frac < 0)
    {
        frac = -frac;
    }
    if (t < 0 && t > -10)
    {
        sign = "-";
    }
    snprintf(buf, size, "%s%ld.%ldC", sign, (long)(t / 10), (long)frac);
}

static void format_temp(char *buf, size_t size, const struct oko_snapshot *snap)
{
    if (snap->sht21.ok)
    {
        format_temp_c100(buf, size, snap->sht21.temp_c100);
    }
    else if (snap->ec.ok)
    {
        format_temp_c10(buf, size, snap->ec.temp_c10);
    }
    else
    {
        buf[0] = '\0';
    }
}

static void format_voltage_mv(char *buf, size_t size, uint32_t mv)
{
    snprintf(buf, size, "%lu.%02luV", (unsigned long)(mv / 1000u),
             (unsigned long)((mv % 1000u) / 10u));
}

static void format_voltage_uv(char *buf, size_t size, uint32_t uv)
{
    snprintf(buf, size, "%lu.%02luV", (unsigned long)(uv / 1000000u),
             (unsigned long)((uv % 1000000u) / 10000u));
}

static void format_current_ma(char *buf, size_t size, int32_t ma)
{
    const char *sign = "";
    uint32_t magnitude;
    if (ma < 0)
    {
        sign = "-";
        magnitude = (uint32_t)(-(int64_t)ma);
    }
    else
    {
        magnitude = (uint32_t)ma;
    }
    snprintf(buf, size, "%s%lu.%02luA", sign, (unsigned long)(magnitude / 1000u),
             (unsigned long)((magnitude % 1000u) / 10u));
}

static void format_power_mw(char *buf, size_t size, uint32_t mw)
{
    snprintf(buf, size, "%lu.%02luW", (unsigned long)(mw / 1000u),
             (unsigned long)((mw % 1000u) / 10u));
}

static void format_cell_mv(char *buf, size_t size, uint16_t mv)
{
    snprintf(buf, size, "%u.%03u", (unsigned)(mv / 1000u),
             (unsigned)(mv % 1000u));
}

static void format_sht_line(char *buf, size_t size, const struct oko_snapshot *snap)
{
    char temp[32];
    format_temp_c100(temp, sizeof(temp), snap->sht21.temp_c100);
    snprintf(buf, size, "SHT21 %s %ld.%ld%%RH", temp,
             (long)(snap->sht21.humidity_rh10 / 10),
             (long)(snap->sht21.humidity_rh10 % 10));
}

static void format_ec_line(char *buf, size_t size, const struct oko_snapshot *snap)
{
    char voltage[16];
    char current[16];
    char temp[16];
    char cell0[16];
    char cell1[16];
    char cell2[16];

    format_voltage_mv(voltage, sizeof(voltage), snap->ec.voltage_mv);
    format_current_ma(current, sizeof(current), snap->ec.current_ma);
    format_temp_c10(temp, sizeof(temp), snap->ec.temp_c10);
    format_cell_mv(cell0, sizeof(cell0), snap->ec.cells[0]);
    format_cell_mv(cell1, sizeof(cell1), snap->ec.cells[1]);
    format_cell_mv(cell2, sizeof(cell2), snap->ec.cells[2]);

    snprintf(buf, size, "STM32 %s %s %s %u%% %s/%s/%sV", voltage, current, temp,
             (unsigned)snap->ec.soc, cell0, cell1, cell2);
}

static void format_ina_line(char *buf, size_t size, const struct oko_snapshot *snap)
{
    char voltage[16];
    char current[16];
    char power[16];

    format_voltage_uv(voltage, sizeof(voltage), snap->ina226.bus_uv);
    format_current_ma(current, sizeof(current), snap->ina226.current_ma);
    format_power_mw(power, sizeof(power), snap->ina226.power_mw);

    snprintf(buf, size, "%s %s %s %s",
             snap->ina226.chip[0] ? snap->ina226.chip : "INA",
             voltage, current, power);
}

static int write_osd(const struct oko_snapshot *snap)
{
    char tmp[] = OSD_PATH ".tmp";
    char temp[32];
    char sht_line[64];
    char ec_line[160];
    char ina_line[80];
    int osd_ok = (snap->ec.ok || snap->sht21.ok || snap->ina226.ok) ? 1 : 0;

    format_temp(temp, sizeof(temp), snap);
    if (snap->sht21.ok)
    {
        format_sht_line(sht_line, sizeof(sht_line), snap);
    }
    if (snap->ec.ok)
    {
        format_ec_line(ec_line, sizeof(ec_line), snap);
    }
    if (snap->ina226.ok)
    {
        format_ina_line(ina_line, sizeof(ina_line), snap);
    }

    FILE *out = fopen(tmp, "w");
    if (!out)
    {
        return -1;
    }

    fprintf(out, "TS %ld\n", snap->ts);
    fprintf(out, "OK %d\n", osd_ok);
    if (snap->sht21.ok)
    {
        fprintf(out, "SHT %s\n", sht_line);
    }
    if (snap->ec.ok)
    {
        fprintf(out, "EC %s\n", ec_line);
    }
    if (snap->ina226.ok)
    {
        fprintf(out, "INA %s\n", ina_line);
    }
    if (temp[0] != '\0')
    {
        fprintf(out, "T %s\n", temp);
    }
    if (snap->ec.ok)
    {
        fprintf(out, "BAT %u%%\n", (unsigned)snap->ec.soc);
    }

    if (fclose(out) != 0)
    {
        unlink(tmp);
        return -1;
    }
    if (rename(tmp, OSD_PATH) < 0)
    {
        unlink(tmp);
        return -1;
    }
    return 0;
}

int snapshot_write_files(const struct oko_snapshot *snap)
{
    int rc = 0;
    if (write_json(snap) < 0)
    {
        rc = -1;
    }
    if (write_osd(snap) < 0)
    {
        rc = -1;
    }
    return rc;
}

static int already_running(void)
{
    FILE *f = fopen(PID_PATH, "r");
    if (!f)
    {
        return 0;
    }

    long pid = 0;
    int scanned = fscanf(f, "%ld", &pid);
    fclose(f);

    if (scanned != 1 || pid <= 0)
    {
        return 0;
    }
    if (kill((pid_t)pid, 0) == 0)
    {
        return 1;
    }
    return errno == EPERM ? 1 : 0;
}

static int write_pid(void)
{
    FILE *f = fopen(PID_PATH, "w");
    if (!f)
    {
        return -1;
    }
    fprintf(f, "%ld\n", (long)getpid());
    if (fclose(f) != 0)
    {
        unlink(PID_PATH);
        return -1;
    }
    pid_written = 1;
    return 0;
}

static void log_transition(const char *name, int previous, int current)
{
    if (previous == current)
    {
        return;
    }
    if (previous < 0)
    {
        printf("oko-ec: %s: %s\n", name, current ? "ok" : "error");
    }
    else
    {
        printf("oko-ec: %s: %s -> %s\n", name,
               previous ? "ok" : "error", current ? "ok" : "error");
    }
    fflush(stdout);
}

int daemon_run(const char *dev, uint8_t ec_addr, uint8_t ina_addr, uint8_t sht_addr,
               uint32_t shunt_mohm, uint8_t ina_channel, unsigned delay_us,
               unsigned retries, unsigned interval, int once)
{
    struct oko_snapshot snap;
    int previous[3] = {-1, -1, -1};

    i2c_transport_set_verbose(0);
    ec_set_verbose(0);

    if (!once)
    {
        if (already_running())
        {
            fprintf(stderr, "oko-ec: daemon already running (%s)\n", PID_PATH);
            return 1;
        }
        signal(SIGTERM, on_signal);
        signal(SIGINT, on_signal);

        if (write_pid() < 0)
        {
            fprintf(stderr, "oko-ec: cannot write %s: %s\n", PID_PATH, strerror(errno));
            return 1;
        }
    }

    while (!stop_flag)
    {
        if (ec_lock_take(0))
        {
            /* "oko-ec flash" owns the bus; skip this cycle. */
            for (unsigned i = 0; i < 10u && !stop_flag; i++)
                usleep(100000);
            continue;
        }
        snapshot_collect(&snap, dev, ec_addr, ina_addr, sht_addr, shunt_mohm,
                         ina_channel, delay_us, retries);
        ec_lock_release();
        if (snapshot_write_files(&snap) < 0 && !once)
        {
            fprintf(stderr, "oko-ec: cannot write state files: %s\n", strerror(errno));
        }

        if (!once)
        {
            log_transition("ec", previous[0], snap.ec.ok);
            log_transition("ina226", previous[1], snap.ina226.ok);
            log_transition("sht21", previous[2], snap.sht21.ok);
            previous[0] = snap.ec.ok;
            previous[1] = snap.ina226.ok;
            previous[2] = snap.sht21.ok;
        }

        if (once)
        {
            break;
        }

        for (unsigned i = 0; i < interval * 10u && !stop_flag; i++)
        {
            usleep(100000);
        }
    }

    if (pid_written)
    {
        unlink(PID_PATH);
        pid_written = 0;
    }
    return 0;
}