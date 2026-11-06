/* daemon.h — снимок состояния и фоновый цикл oko-ec. */

#ifndef DAEMON_H
#define DAEMON_H

#include <stdint.h>
#include <stdio.h>

#define SNAP_EC 1u
#define SNAP_INA 2u
#define SNAP_SHT 4u
#define SNAP_ALL (SNAP_EC | SNAP_INA | SNAP_SHT)

struct oko_snapshot
{
    long ts;
    int ok;

    struct
    {
        int ok;
        int info_ok;
        uint8_t device_type;
        uint8_t proto;
        uint32_t version;
        uint32_t img_size;
        uint8_t flags;
        uint16_t voltage_mv;
        int16_t current_ma;
        int16_t temp_c10;
        uint8_t soc;
        uint8_t status;
        uint16_t cells[3];
    } ec;

    struct
    {
        int ok;
        char chip[12];
        uint8_t channel;
        uint32_t bus_uv;
        uint32_t bus_mv;
        int32_t shunt_nv;
        int32_t current_ma;
        uint32_t power_mw;
    } ina226;

    struct
    {
        int ok;
        int32_t temp_c100;
        int32_t humidity_rh10;
    } sht21;

    int error_count;
    const char *errors[3];
};

/* Опросить EC/INA/SHT21 независимо и заполнить снимок. */
void snapshot_collect(struct oko_snapshot *snap, const char *dev, uint8_t ec_addr,
                      uint8_t ina_addr, uint8_t sht_addr, uint32_t shunt_mohm,
                      uint8_t ina_channel, unsigned delay_us, unsigned retries);

/* Однострочный JSON; parts — какие блоки печатать, with_ts — добавить ts. */
void snapshot_print_json(FILE *out, const struct oko_snapshot *snap,
                         unsigned parts, int with_ts);

/* Атомарно пишет /tmp/oko-ec.json и /tmp/oko-ec.osd. */
int snapshot_write_files(const struct oko_snapshot *snap);

/* Фоновый цикл (или один цикл при once). Возврат — код выхода. */
int daemon_run(const char *dev, uint8_t ec_addr, uint8_t ina_addr, uint8_t sht_addr,
               uint32_t shunt_mohm, uint8_t ina_channel, unsigned delay_us,
               unsigned retries, unsigned interval, int once);

#endif /* DAEMON_H */