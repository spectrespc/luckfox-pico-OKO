/* ec_proto.c — обмен кадрами протокола v2 с STM32 ACPI-EC. */

#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>

#include "ec_proto.h"
#include "i2c_transport.h"

#define EC_DELAY_US 5000u
#define EC_RETRIES 100u

#define ST_OK 0x00

static int ec_verbose = 1;

void ec_set_verbose(int enable)
{
    ec_verbose = enable ? 1 : 0;
}

static const char *status_str(uint8_t status)
{
    switch (status)
    {
    case ST_OK:
        return "OK";
    case 0x01:
        return "ERR_CMD";
    case 0x02:
        return "ERR_PARAM";
    case 0x03:
        return "ERR_STATE";
    case 0x05:
        return "ERR_FLASH";
    case 0x06:
        return "ERR_CRC";
    case 0x07:
        return "ERR_SEQ";
    case 0x08:
        return "ERR_LEN";
    default:
        return "UNKNOWN";
    }
}

static uint16_t get_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

/* Отправить кадр и получить ответ. Возврат: LEN ответа, -1 при ошибке. */
static int ec_exchange(int fd, uint8_t command, const uint8_t *payload, size_t plen,
                       uint8_t *resp, size_t cap, unsigned delay_us, unsigned retries,
                       uint8_t *status)
{
    uint8_t wbuf[64];

    if (plen > sizeof(wbuf) - 3)
    {
        return -1;
    }
    if (delay_us == 0)
    {
        delay_us = EC_DELAY_US;
    }
    if (retries == 0)
    {
        retries = EC_RETRIES;
    }

    uint16_t len = (uint16_t)(1 + plen);
    wbuf[0] = (uint8_t)(len & 0xFF);
    wbuf[1] = (uint8_t)(len >> 8);
    wbuf[2] = command;
    if (plen)
    {
        memcpy(&wbuf[3], payload, plen);
    }

    int n = i2c_transport_write_read_timed(fd, wbuf, 3 + plen, resp, cap,
                                           delay_us, retries);
    if (n < 3)
    {
        return -1;
    }

    uint16_t rlen = get_le16(resp);
    if (rlen < 1 || (size_t)rlen + 2u > (size_t)n)
    {
        if (ec_verbose)
        {
            fprintf(stderr, "EC: truncated response (LEN %u, read %d)\n", rlen, n);
        }
        return -1;
    }

    *status = resp[2];
    return (int)rlen;
}

static int ec_check_status(uint8_t status, const char *name)
{
    if (status == ST_OK)
    {
        return 0;
    }

    if (ec_verbose)
    {
        fprintf(stderr, "%s status: 0x%02X (%s)\n", name, status, status_str(status));
    }
    return -1;
}

static int ec_check_len(int len, int expected, const char *name)
{
    if (len == expected)
    {
        return 0;
    }

    if (ec_verbose)
    {
        fprintf(stderr, "%s: unexpected response length %d (expected %d)\n",
                name, len, expected);
    }
    return -1;
}

int ec_get_info(int fd, struct ec_info *out, unsigned delay_us, unsigned retries)
{
    uint8_t resp[64];
    uint8_t status = 0;
    int len = ec_exchange(fd, EC_CMD_GET_INFO, NULL, 0, resp, sizeof(resp),
                          delay_us, retries, &status);
    if (len < 0 || ec_check_status(status, "GET_INFO") < 0 ||
        ec_check_len(len, 1 + EC_INFO_PAYLOAD_LEN, "GET_INFO") < 0)
    {
        return -1;
    }

    const uint8_t *p = &resp[3];
    out->device_type = p[0];
    out->proto = p[1];
    out->version = get_le32(&p[2]);
    out->img_size = get_le32(&p[6]);
    out->flags = p[10];
    out->extra = p[11];
    return 0;
}

int ec_get_status(int fd, struct ec_status *out, unsigned delay_us, unsigned retries)
{
    uint8_t resp[64];
    uint8_t status = 0;
    int len = ec_exchange(fd, EC_CMD_GET_STATUS, NULL, 0, resp, sizeof(resp),
                          delay_us, retries, &status);
    if (len < 0 || ec_check_status(status, "GET_STATUS") < 0)
    {
        return -1;
    }
    if (len < 7)
    {
        if (ec_verbose)
        {
            fprintf(stderr, "GET_STATUS: unexpected response length %d (expected >= 7)\n",
                    len);
        }
        return -1;
    }

    const uint8_t *p = &resp[3];
    out->last_status = p[0];
    out->update_state = p[1];
    out->update_offset = get_le32(&p[3]);
    return 0;
}

int ec_get_battery(int fd, struct ec_battery *out, unsigned delay_us, unsigned retries)
{
    uint8_t resp[64];
    uint8_t status = 0;
    int len = ec_exchange(fd, EC_CMD_GET_BATTERY, NULL, 0, resp, sizeof(resp),
                          delay_us, retries, &status);
    if (len < 0 || ec_check_status(status, "GET_BATTERY_DATA") < 0 ||
        ec_check_len(len, 15, "GET_BATTERY_DATA") < 0)
    {
        return -1;
    }

    const uint8_t *p = &resp[3];
    out->voltage_mv = get_le16(&p[0]);
    out->current_ma = (int16_t)get_le16(&p[2]);
    out->temp_c10 = (int16_t)get_le16(&p[4]);
    out->soc = p[6];
    out->status = p[7];
    for (int i = 0; i < 3; i++)
    {
        out->cells[i] = get_le16(&p[8 + 2 * i]);
    }
    return 0;
}

int ec_get_cells(int fd, uint16_t cells[3], unsigned delay_us, unsigned retries)
{
    uint8_t resp[64];
    uint8_t status = 0;
    int len = ec_exchange(fd, EC_CMD_GET_CELLS, NULL, 0, resp, sizeof(resp),
                          delay_us, retries, &status);
    if (len < 0 || ec_check_status(status, "GET_CELL_VOLTAGES") < 0 ||
        ec_check_len(len, 7, "GET_CELL_VOLTAGES") < 0)
    {
        return -1;
    }

    for (int i = 0; i < 3; i++)
    {
        cells[i] = get_le16(&resp[3 + 2 * i]);
    }
    return 0;
}

int ec_get_temp(int fd, int16_t *temp_c10, unsigned delay_us, unsigned retries)
{
    uint8_t resp[64];
    uint8_t status = 0;
    int len = ec_exchange(fd, EC_CMD_GET_TEMP, NULL, 0, resp, sizeof(resp),
                          delay_us, retries, &status);
    if (len < 0 || ec_check_status(status, "GET_TEMPERATURE") < 0 ||
        ec_check_len(len, 3, "GET_TEMPERATURE") < 0)
    {
        return -1;
    }

    *temp_c10 = (int16_t)get_le16(&resp[3]);
    return 0;
}