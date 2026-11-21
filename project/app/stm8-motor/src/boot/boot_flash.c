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

/* Result of waiting for one program/erase operation.  The previous version
 * returned silently on WR_PG_DIS, so a rejected erase/program looked like a
 * successful update: the host reached PROGRESS 100 while the flash kept the
 * old application (KI-075). */
#define FLASH_RES_OK        0u
#define FLASH_RES_PROTECTED 1u
#define FLASH_RES_TIMEOUT   2u

/* ---------------------------------------------------------------------------
 * Block erase from RAM.
 *
 * Word programming may execute from Flash (bench-verified: a word written by
 * this bootloader lands in Flash), but a *block erase* must not: the Flash
 * controller stops serving instruction fetches for the duration of the erase,
 * so a wait loop running from Flash deadlocks and the IWDG resets the MCU long
 * before the block completes.  The bench symptom was an erase that reported
 * success while Flash kept its old content, a ~0.5 s I2C blackout and
 * `SCL hold by slave` (KI-075).  ST's SPL documents the same rule: the call and
 * execution of FLASH_WaitForLastOperation "must be done from RAM in case of
 * block operation".
 *
 * ERASE_STUB_BYTES is assembled from src/boot/erase_stub.asm (sdasstm8 -l),
 * copied to RAM at boot and called through a function pointer.  It is position
 * independent and talks only to fixed SFR/RAM cells:
 *   0x0200 flash_stub_addr   (16-bit block address)
 *   0x0202 flash_stub_result (0 ok, 1 write protected, 2 timeout)
 *   0x0203 flash_stub_outer  (timeout counter, private)
 * Regenerate the array from the listing whenever the stub changes.
 * ------------------------------------------------------------------------- */
__at(0x0200) volatile uint16_t flash_stub_addr;
__at(0x0202) volatile uint8_t  flash_stub_result;
__at(0x0203) volatile uint8_t  flash_stub_outer;

static const uint8_t erase_stub_bytes[] = {
    0x72, 0x15, 0x50, 0x5F, 0x72, 0x1A, 0x50, 0x5B, 0x72, 0x1B, 0x50, 0x5C,
    0xCE, 0x02, 0x00, 0x4F, 0xF7, 0xA6, 0x10, 0xC7, 0x02, 0x03, 0x90, 0xAE,
    0xFF, 0xFF, 0x72, 0x00, 0x50, 0x5F, 0x13, 0x72, 0x04, 0x50, 0x5F, 0x12,
    0x90, 0x5A, 0x26, 0xF2, 0x72, 0x5A, 0x02, 0x03, 0x26, 0xE8, 0xA6, 0x02,
    0x20, 0x05, 0xA6, 0x01, 0x20, 0x01, 0x4F, 0xC7, 0x02, 0x02, 0x72, 0x1B,
    0x50, 0x5B, 0x72, 0x1A, 0x50, 0x5C, 0x81
};

static uint8_t erase_stub_ram[sizeof(erase_stub_bytes)];

void boot_flash_init(void)
{
    uint8_t i;

    for (i = 0; i < (uint8_t)sizeof(erase_stub_bytes); i++)
        erase_stub_ram[i] = erase_stub_bytes[i];
}

/* EOP is a sticky flag: it must be cleared before the operation, otherwise the
 * first poll sees the stale bit and the loop never waits for the self-timed
 * erase/program to finish (the next trigger is then ignored by the controller). */
static void flash_clear_eop(void)
{
    FLASH->IAPSR &= (uint8_t)~FLASH_IAPSR_EOP;
}

static uint8_t flash_wait_eop(void)
{
    uint16_t timeout = FLASH_EOP_WAIT;

    while (timeout--)
    {
        uint8_t iapsr = FLASH->IAPSR;

        if (iapsr & FLASH_IAPSR_WR_PG_DIS)
            return FLASH_RES_PROTECTED;
        if (iapsr & FLASH_IAPSR_EOP)
            return FLASH_RES_OK;
    }
    return FLASH_RES_TIMEOUT;
}

static uint8_t flash_erase_block(uint16_t address)
{
    void (*stub)(void) = (void (*)(void))erase_stub_ram;

    flash_stub_addr = address;
    flash_stub_result = 3;
    stub();

    return flash_stub_result == 0 ? BOOT_ST_OK : BOOT_ST_FLASH_ERR;
}

uint8_t boot_flash_erase(uint16_t start, uint16_t length)
{
    uint16_t addr;

    if (start < OKO_HDR_BASE || (start & (OKO_BLOCK_SIZE - 1)) != 0)
        return BOOT_ST_BAD_ADDR;
    if (length == 0 || (length & (OKO_BLOCK_SIZE - 1)) != 0)
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

        flash_clear_eop();
        FLASH->CR2 |= FLASH_CR2_WPRG;
        FLASH->NCR2 &= (uint8_t)~FLASH_NCR2_NWPRG;

        p[0] = data[i];
        p[1] = data[i + 1];
        p[2] = data[i + 2];
        p[3] = data[i + 3];

        if (flash_wait_eop() != FLASH_RES_OK)
        {
            FLASH->CR2 &= (uint8_t)~FLASH_CR2_WPRG;
            FLASH->NCR2 |= FLASH_NCR2_NWPRG;
            FLASH->IAPSR &= (uint8_t)~FLASH_IAPSR_PUL;
            return BOOT_ST_FLASH_ERR;
        }
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