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
#define EC_CMD_GET_POWER 0x20
#define EC_CMD_SET_POWER 0x21

#define EC_DEVICE_BOOT 1
#define EC_DEVICE_APP 2
#define EC_PROTO_VER 2
#define EC_INFO_PAYLOAD_LEN 12
#define EC_POWER_PAYLOAD_LEN 8

/* host_state, общий контракт с прошивкой STM32 (docs/protocol.md, v3). */
#define EC_HOST_OFF 0
#define EC_HOST_BOOTING 1
#define EC_HOST_AWAKE 2
#define EC_HOST_SUSPENDING 3
#define EC_HOST_SUSPENDED 4
#define EC_HOST_SHUTTING_DOWN 5
#define EC_HOST_REBOOTING 6
#define EC_HOST_ERROR 7

/* last_wake_src. */
#define EC_WAKE_NONE 0
#define EC_WAKE_BUTTON 1
#define EC_WAKE_I2C 2
#define EC_WAKE_LPTIM 3
#define EC_WAKE_RESET 4

/* power_request (EC → хост). */
#define EC_REQ_NONE 0
#define EC_REQ_SLEEP 1
#define EC_REQ_SHUTDOWN 2
#define EC_REQ_REBOOT 3

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

/* Ответ CMD_GET_POWER (8 байт, протокол v3). */
struct ec_power
{
    uint8_t host_state;
    uint8_t flags;
    uint8_t power_request;
    uint8_t last_event;
    uint8_t event_count;
    uint8_t reset_cause;
    uint8_t last_wake_src;
    uint8_t wake_count;
};

void ec_set_verbose(int enable);
int ec_verbose_enabled(void);

/* 0 — успех, -1 — ошибка транспорта/статуса/длины. */
int ec_get_info(int fd, struct ec_info *out, unsigned delay_us, unsigned retries);
int ec_get_status(int fd, struct ec_status *out, unsigned delay_us, unsigned retries);
int ec_get_battery(int fd, struct ec_battery *out, unsigned delay_us, unsigned retries);
int ec_get_cells(int fd, uint16_t cells[3], unsigned delay_us, unsigned retries);
int ec_get_temp(int fd, int16_t *temp_c10, unsigned delay_us, unsigned retries);
int ec_get_power(int fd, struct ec_power *out, unsigned delay_us, unsigned retries);
int ec_set_power(int fd, uint8_t host_state, unsigned delay_us, unsigned retries);

#endif /* EC_PROTO_H */