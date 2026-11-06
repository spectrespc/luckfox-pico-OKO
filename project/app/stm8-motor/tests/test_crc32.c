/* Host tests for the CRC helpers shared by firmware and tools. */
#include <stdio.h>
#include <string.h>

#include "oko_crc.h"

static int failures;

static void expect_u32(const char *name, uint32_t got, uint32_t want)
{
    if (got != want)
    {
        fprintf(stderr, "FAIL %s: got 0x%08X, want 0x%08X\n", name, got, want);
        failures++;
    }
}

static void expect_u16(const char *name, uint16_t got, uint16_t want)
{
    if (got != want)
    {
        fprintf(stderr, "FAIL %s: got 0x%04X, want 0x%04X\n", name, got, want);
        failures++;
    }
}

int main(void)
{
    static const uint8_t empty[] = { 0 };
    static const uint8_t digits[] = "123456789";
    static const uint8_t fox[] = "The quick brown fox jumps over the lazy dog";

    expect_u32("crc32 empty", oko_crc32(empty, 0), 0x00000000u);
    expect_u32("crc32 digits", oko_crc32(digits, 9), 0xCBF43926u);
    expect_u32("crc32 fox", oko_crc32(fox, (uint32_t)(sizeof(fox) - 1)),
               0x414FA339u);

    expect_u16("crc16 digits", oko_crc16(digits, 9), 0x4B37u);
    expect_u16("crc16 empty", oko_crc16(empty, 0), 0xFFFFu);

    if (failures)
    {
        fprintf(stderr, "%d CRC test(s) failed\n", failures);
        return 1;
    }
    printf("crc: ok\n");
    return 0;
}