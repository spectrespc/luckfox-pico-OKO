/*
 * oko_crc.c - CRC helpers shared by the bootloader, the host tools and the
 * unit tests.
 *
 * CRC16: poly 0xA001 (reflected 0x8005), init 0xFFFF, no final xor.  This is
 *        the same variant the upstream EEPROM code uses.
 * CRC32: poly 0xEDB88320 (reflected), init 0xFFFFFFFF, xorout 0xFFFFFFFF,
 *        i.e. the standard zlib/crc32 value.
 */
#include "oko_crc.h"

uint16_t oko_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFu;
    uint8_t i;

    while (len--)
    {
        crc ^= *data++;
        for (i = 0; i < 8; i++)
        {
            if (crc & 0x0001u)
                crc = (uint16_t)((crc >> 1) ^ 0xA001u);
            else
                crc >>= 1;
        }
    }
    return crc;
}

uint32_t oko_crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t i;

    while (len--)
    {
        crc ^= *data++;
        for (i = 0; i < 8; i++)
        {
            if (crc & 1u)
                crc = (crc >> 1) ^ 0xEDB88320u;
            else
                crc >>= 1;
        }
    }
    return crc ^ 0xFFFFFFFFu;
}