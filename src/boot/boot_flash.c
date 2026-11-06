/*
 * boot_flash.c - in-application programming for the bootloader.
 *
 * Only word (4-byte) programming is used.  It executes from flash without a
 * RAM copy and keeps the code small.  Addresses and lengths are handled as
 * 16-bit values (the STM8S103 has 8 KiB of program memory); the wire format
 * still carries them as 32-bit big-endian fields and the host checks the high
 * half is zero.
 */
#include "boot.h"
#include "oko_crc.h"

#define FLASH_EOP_WAIT 0xFFFFu

static void flash_wait_eop(void)
{
    uint16_t timeout = FLASH_EOP_WAIT;

    while (timeout--)
    {
        if (FLASH->IAPSR & FLASH_IAPSR_EOP)
            return;
        if (FLASH->IAPSR & FLASH_IAPSR_WR_PG_DIS)
            return;
    }
}

static uint8_t flash_erase_block(uint16_t address)
{
    volatile uint8_t *p = (volatile uint8_t *)address;

    FLASH->CR2 |= FLASH_CR2_ERASE;
    FLASH->NCR2 &= (uint8_t)~FLASH_NCR2_NERASE;

    *p = 0x00;
    flash_wait_eop();

    FLASH->CR2 &= (uint8_t)~FLASH_CR2_ERASE;
    FLASH->NCR2 |= FLASH_NCR2_NERASE;

    return BOOT_ST_OK;
}

uint8_t boot_flash_erase(uint16_t start, uint16_t length)
{
    uint16_t addr;

    if (start < OKO_HDR_BASE || start >= OKO_APP_END)
        return BOOT_ST_BAD_ADDR;
    if (length == 0 || length > (OKO_APP_END - OKO_HDR_BASE))
        return BOOT_ST_BAD_ADDR;
    if ((start & (OKO_BLOCK_SIZE - 1)) != 0)
        return BOOT_ST_BAD_ADDR;
    if ((length & (OKO_BLOCK_SIZE - 1)) != 0)
        return BOOT_ST_BAD_ADDR;
    if ((uint32_t)start + length > OKO_APP_END)
        return BOOT_ST_BAD_ADDR;

    FLASH->PUKR = FLASH_RASS_KEY1;
    FLASH->PUKR = FLASH_RASS_KEY2;

    for (addr = start; addr < start + length; addr += OKO_BLOCK_SIZE)
    {
        uint8_t st;

        IWDG->KR = 0xAA;             /* erase can exceed one watchdog period */
        st = flash_erase_block(addr);
        if (st != BOOT_ST_OK)
            return st;
    }

    FLASH->IAPSR &= (uint8_t)~FLASH_IAPSR_PUL;
    return BOOT_ST_OK;
}

uint8_t boot_flash_write(uint16_t address, const uint8_t *data, uint8_t length)
{
    uint8_t i;

    if (address < OKO_HDR_BASE || address >= OKO_APP_END)
        return BOOT_ST_BAD_ADDR;
    if (length == 0 || length > BOOT_WRITE_MAX || (length & 3u) != 0)
        return BOOT_ST_BAD_ADDR;
    if ((address & 3u) != 0)
        return BOOT_ST_BAD_ADDR;
    if ((uint32_t)address + length > OKO_APP_END)
        return BOOT_ST_BAD_ADDR;

    FLASH->PUKR = FLASH_RASS_KEY1;
    FLASH->PUKR = FLASH_RASS_KEY2;

    for (i = 0; i < length; i += 4)
    {
        volatile uint8_t *p = (volatile uint8_t *)(uint16_t)(address + i);

        FLASH->CR2 |= FLASH_CR2_WPRG;
        FLASH->NCR2 &= (uint8_t)~FLASH_NCR2_NWPRG;

        p[0] = data[i];
        p[1] = data[i + 1];
        p[2] = data[i + 2];
        p[3] = data[i + 3];

        flash_wait_eop();
        IWDG->KR = 0xAA;

        FLASH->CR2 &= (uint8_t)~FLASH_CR2_WPRG;
        FLASH->NCR2 |= FLASH_NCR2_NWPRG;
    }

    FLASH->IAPSR &= (uint8_t)~FLASH_IAPSR_PUL;
    return BOOT_ST_OK;
}

/* Returns 1 when the header magic and length are plausible.  Version bytes and
 * CRC are returned even for an invalid header so BOOT_INFO can report what is
 * actually in flash. */
uint8_t boot_header_info(uint16_t *length, uint32_t *crc, uint8_t version[3])
{
    const uint8_t *h = (const uint8_t *)OKO_HDR_BASE;
    uint16_t len;

    version[0] = h[8];
    version[1] = h[9];
    version[2] = h[10];
    *crc = oko_get_be32(&h[12]);

    if (h[0] != OKO_MAGIC0 || h[1] != OKO_MAGIC1 ||
        h[2] != OKO_MAGIC2 || h[3] != OKO_MAGIC3)
        return 0;
    if (h[4] != 0 || h[5] != 0)
        return 0;
    len = oko_get_be16(&h[6]);
    if (len == 0 || len > OKO_APP_MAX_SIZE)
        return 0;
    *length = len;
    return 1;
}

int boot_app_valid(void)
{
    uint16_t length = 0;
    uint32_t crc = 0;
    uint8_t version[3];

    if (!boot_header_info(&length, &crc, version))
        return 0;
    return oko_crc32((const uint8_t *)OKO_APP_BASE, length) == crc;
}

uint8_t boot_flash_finish(uint16_t length, uint32_t crc32)
{
    uint16_t hdr_length = 0;
    uint32_t hdr_crc = 0;
    uint8_t version[3];
    uint32_t crc;

    if (length == 0 || length > OKO_APP_MAX_SIZE)
        return BOOT_ST_BAD_ADDR;
    if (!boot_header_info(&hdr_length, &hdr_crc, version))
        return BOOT_ST_VERIFY_ERR;
    if (hdr_length != length)
        return BOOT_ST_VERIFY_ERR;

    crc = oko_crc32((const uint8_t *)OKO_APP_BASE, length);
    if (crc != crc32 || crc != hdr_crc)
        return BOOT_ST_BAD_CRC;
    return BOOT_ST_OK;
}

void boot_clear_magic(void)
{
    uint16_t timeout = FLASH_EOP_WAIT;

    FLASH->DUKR = FLASH_RASS_KEY2;   /* data keys are reversed */
    FLASH->DUKR = FLASH_RASS_KEY1;

    while (timeout-- && !(FLASH->IAPSR & FLASH_IAPSR_DUL))
        ;
    if (timeout == 0)
        return;

    *((volatile uint8_t *)OKO_EEPROM_MAGIC_ADDR) = 0x00;
    flash_wait_eop();
    FLASH->IAPSR &= (uint8_t)~FLASH_IAPSR_DUL;
}