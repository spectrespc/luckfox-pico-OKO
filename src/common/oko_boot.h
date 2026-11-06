/*
 * oko_boot.h - shared description of the OKO STM8 flash layout and the
 * I2C bootloader protocol.
 *
 * This header is deliberately free of STM8/SPL types: the firmware, the host
 * tools (mkapp.py feeds the same offsets) and the host unit tests all include
 * it.  All multi-byte values on the wire and in the header are big-endian.
 */
#ifndef OKO_BOOT_H
#define OKO_BOOT_H

#include <stddef.h>
#include <stdint.h>

/* --- Flash map (STM8S103F3P6, 8 KiB program memory) ---------------------- */
#define OKO_BOOT_BASE        0x8000u /* bootloader, owns the reset vector   */
#define OKO_BOOT_END         0x8A00u /* first byte after the boot area      */
#define OKO_HDR_BASE         0x8A00u /* application header, 16 bytes        */
#define OKO_HDR_SIZE         16u
#define OKO_APP_BASE         0x8A10u /* application vector table            */
#define OKO_APP_END          0xA000u /* first byte after program memory     */
#define OKO_APP_MAX_SIZE     (OKO_APP_END - OKO_APP_BASE) /* 5616 bytes      */
#define OKO_BLOCK_SIZE       64u     /* flash block/page size on STM8S103   */

/* EEPROM cells owned by the bootloader (positons use 0x4000..0x4009). */
#define OKO_EEPROM_MAGIC_ADDR 0x4010u
#define OKO_EEPROM_FLAG_ADDR  0x4011u
#define OKO_EEPROM_MAGIC      0x42u  /* app -> boot on the next reset       */
#define OKO_EEPROM_FLAG_NONE  0x00u

/* --- Header magic --------------------------------------------------------- */
#define OKO_MAGIC0 'O'
#define OKO_MAGIC1 'K'
#define OKO_MAGIC2 'O'
#define OKO_MAGIC3 '8'

/* --- Shared application/bootloader command -------------------------------- */
/* Both roles answer 0x08 with [role, major, minor, patch]; the host uses it
 * to decide whether 0x10 (BOOT_INFO) is meaningful.  A running application
 * must never receive 0x10. */
#define OKO_CMD_GET_STATE   0x08u
#define OKO_ROLE_APP        0x00u
#define OKO_ROLE_BOOT       0x01u

/* --- Bootloader protocol commands ----------------------------------------- */
#define BOOT_CMD_INFO       0x10u
#define BOOT_CMD_ERASE      0x11u
#define BOOT_CMD_WRITE      0x12u
#define BOOT_CMD_FINISH     0x13u
#define BOOT_CMD_RESET      0x14u
#define BOOT_CMD_READ       0x15u

/* Largest payload of one BOOT_WRITE frame (command block size). */
#define BOOT_WRITE_MAX      32u
/* Bootloader receive buffer: cmd + 4 addr + 1 len + 32 data + 2 crc. */
#define BOOT_RX_BUF_SIZE    48u
#define BOOT_TX_BUF_SIZE    48u

/* Status byte returned by every bootloader command that answers with one. */
#define BOOT_ST_OK          0u
#define BOOT_ST_BUSY        1u
#define BOOT_ST_BAD_ADDR    2u
#define BOOT_ST_BAD_CRC     3u
#define BOOT_ST_FLASH_ERR   4u
#define BOOT_ST_VERIFY_ERR  5u
#define BOOT_ST_BAD_CMD     6u

/* --- Persistent / runtime state ------------------------------------------ */
#define BOOT_STATE_APP      0u
#define BOOT_STATE_BOOT     1u

/* Application part number reported by BOOT_CMD_INFO. */
#define OKO_BOOT_VERSION_MAJOR 1u
#define OKO_BOOT_VERSION_MINOR 0u
#define OKO_BOOT_VERSION_PATCH 0u

/*
 * Intel HEX / header helpers are implemented in C so the unit tests and the
 * firmware agree on the byte order.  They take plain byte buffers.
 */
struct oko_app_header {
    uint8_t  magic[4];
    uint32_t length;    /* payload length, OKO_APP_BASE..OKO_APP_BASE+length */
    uint8_t  major;
    uint8_t  minor;
    uint8_t  patch;
    uint8_t  flags;
    uint32_t crc32;     /* over the payload starting at OKO_APP_BASE */
};

/* Bootloader request frames.  They return the number of bytes to write to the
 * I2C bus, or 0 when the arguments are out of range. */
size_t oko_boot_frame_erase(uint8_t out[9], uint16_t start, uint16_t length);
size_t oko_boot_frame_write(uint8_t out[8 + BOOT_WRITE_MAX], uint16_t address,
                            const uint8_t *data, uint8_t length);
size_t oko_boot_frame_finish(uint8_t out[9], uint16_t length, uint32_t crc32);

/* Serialise/deserialise the 16-byte header.  Returns 0 on success. */
int oko_header_pack(const struct oko_app_header *hdr, uint8_t out[OKO_HDR_SIZE]);
int oko_header_unpack(const uint8_t raw[OKO_HDR_SIZE], struct oko_app_header *hdr);
/* True when the magic and the length are within the application slot. */
int oko_header_valid(const struct oko_app_header *hdr);

#define oko_get_be16(p) ((uint16_t)(((uint16_t)(p)[0] << 8) | (p)[1]))
#define oko_get_be32(p) (((uint32_t)(p)[0] << 24) | ((uint32_t)(p)[1] << 16) | \
                         ((uint32_t)(p)[2] << 8) | (uint32_t)(p)[3])
#define oko_put_be16(p, v) do { (p)[0] = (uint8_t)((v) >> 8); \
                                (p)[1] = (uint8_t)(v); } while (0)
#define oko_put_be32(p, v) do { (p)[0] = (uint8_t)((v) >> 24); \
                                (p)[1] = (uint8_t)((v) >> 16); \
                                (p)[2] = (uint8_t)((v) >> 8); \
                                (p)[3] = (uint8_t)(v); } while (0)

#endif /* OKO_BOOT_H */