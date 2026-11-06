/*
 * boot.c - OKO STM8S103F3P6 bootloader entry point.
 *
 * Layout (see src/common/oko_boot.h):
 *   0x8000..0x89FF  bootloader (this image)
 *   0x8A00..0x8A0F  application header ("OKO8", length, version, CRC32)
 *   0x8A10..0x9FFF  application
 *
 * SDCC generates the interrupt vector table in the module that contains
 * main().  Boot mode keeps interrupts disabled, so boot itself only needs the
 * reset vector; the application's I2C (vector 19) and TIM4 (vector 23) vectors
 * are forwarded by naked trampolines that read the addresses from the
 * application's own vector table at OKO_APP_BASE.
 *
 * SDCC numbers the table entries as (interrupt_number + 2): vector 19 is at
 * OKO_APP_BASE + 21*4 and vector 23 at OKO_APP_BASE + 25*4.  A vector entry is
 * four bytes: 82 00 <addr_hi> <addr_lo>, so the handler address lives at
 * offset 2 inside the entry.
 */
#include "boot.h"

void boot_jump_app(void) __naked
{
    /* Entry 0 of the application table: reset vector. */
    __asm__("ldw x, 0x8A12");
    __asm__("jp (x)");
}

/* Vector 19 (I2C): jump to the application's handler at 0x8A10 + 21*4 + 2. */
void I2C_IRQHandler(void) __naked __interrupt(19)
{
    __asm__("ldw x, 0x8A66");
    __asm__("jp (x)");
}

/* Vector 23 (TIM4): jump to the application's handler at 0x8A10 + 25*4 + 2. */
void TIM4_UPD_OVF_IRQHandler(void) __naked __interrupt(23)
{
    __asm__("ldw x, 0x8A76");
    __asm__("jp (x)");
}

void main(void)
{
    uint8_t magic;

    boot_hw_init();

    magic = *((const uint8_t *)(uint16_t)OKO_EEPROM_MAGIC_ADDR);
    if (magic == OKO_EEPROM_MAGIC)
    {
        /* The application asked for an update: clear the request and stay. */
        boot_clear_magic();
    }
    else if (boot_app_valid())
    {
        boot_jump_app();
    }

    for (;;)
    {
        IWDG->KR = 0xAA;           /* reload */
        boot_i2c_poll();
    }
}