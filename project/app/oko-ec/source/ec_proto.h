/* ec_proto.h — кадры протокола v2 STM32 ACPI-EC (адрес 0x42).
 *
 * Транспорт уже открыт вызывающим: функции работают с готовым fd,
 * потому что адрес задаётся один раз при открытии i2c-dev.
 */

#ifndef EC_PROTO_H
#define EC_PROTO_H

#include <stdint.h>

#define EC_ADDR7 0x42

#define EC_CMD_GET_INFO 0x01
#define EC_CMD_GET_STATUS 0x07
#define EC_CMD_GET_BATTERY 0x10
#define EC_CMD_GET_CELLS 0x11
#define EC_CMD_GET_TEMP 0x12

#define EC_DEVICE_BOOT 1
#define EC_DEVICE_APP 2
#define EC_PROTO_VER 2
#define EC_INFO_PAYLOAD_LEN 12

struct ec_info
{
    uint8_t device_type;
    uint8_t proto;
    uint32_t version;
    uint32_t img_size;
    uint8_t flags;
    uint8_t extra;
};

struct ec_status
{
    uint8_t last_status;
    uint8_t update_state;
    uint32_t update_offset;
};

struct ec_battery
{
    uint16_t voltage_mv;
    int16_t current_ma;
    int16_t temp_c10;
    uint8_t soc;
    uint8_t status;
    uint16_t cells[3];
};

void ec_set_verbose(int enable);

/* 0 — успех, -1 — ошибка транспорта/статуса/длины. */
int ec_get_info(int fd, struct ec_info *out, unsigned delay_us, unsigned retries);
int ec_get_status(int fd, struct ec_status *out, unsigned delay_us, unsigned retries);
int ec_get_battery(int fd, struct ec_battery *out, unsigned delay_us, unsigned retries);
int ec_get_cells(int fd, uint16_t cells[3], unsigned delay_us, unsigned retries);
int ec_get_temp(int fd, int16_t *temp_c10, unsigned delay_us, unsigned retries);

#endif /* EC_PROTO_H */