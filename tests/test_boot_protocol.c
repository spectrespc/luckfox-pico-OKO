/* Host tests for the bootloader request frames (shared with oko-motor). */
#include <stdio.h>
#include <string.h>

#include "oko_boot.h"
#include "oko_crc.h"

static int failures;

static void check(int condition, const char *name)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL %s\n", name);
        failures++;
    }
}

int main(void)
{
    uint8_t erase[9];
    uint8_t write[8 + BOOT_WRITE_MAX];
    uint8_t finish[9];
    uint8_t data[BOOT_WRITE_MAX];
    size_t len;
    uint16_t crc;
    int i;

    len = oko_boot_frame_erase(erase, 0x8A00, 0x1000);
    check(len == 9, "erase length");
    check(erase[0] == BOOT_CMD_ERASE, "erase command");
    check(oko_get_be32(&erase[1]) == 0x8A00, "erase start");
    check(oko_get_be32(&erase[5]) == 0x1000, "erase size");
    check(oko_boot_frame_erase(erase, 0, 0) == 0, "erase zero rejected");

    for (i = 0; i < (int)BOOT_WRITE_MAX; i++)
        data[i] = (uint8_t)i;
    len = oko_boot_frame_write(write, 0x8A20, data, BOOT_WRITE_MAX);
    check(len == 8 + BOOT_WRITE_MAX, "write length");
    check(write[0] == BOOT_CMD_WRITE, "write command");
    check(oko_get_be32(&write[1]) == 0x8A20, "write address");
    check(write[5] == BOOT_WRITE_MAX, "write payload length");
    check(memcmp(&write[6], data, BOOT_WRITE_MAX) == 0, "write payload");
    crc = oko_crc16(&write[1], (uint16_t)(5 + BOOT_WRITE_MAX));
    check(oko_get_be16(&write[6 + BOOT_WRITE_MAX]) == crc, "write crc");

    check(oko_boot_frame_write(write, 0x8A20, data, 0) == 0,
          "empty write rejected");
    check(oko_boot_frame_write(write, 0x8A20, data, 6) == 0,
          "unaligned write rejected");

    len = oko_boot_frame_finish(finish, 4246, 0xC119BC6F);
    check(len == 9, "finish length");
    check(finish[0] == BOOT_CMD_FINISH, "finish command");
    check(oko_get_be32(&finish[1]) == 4246, "finish length field");
    check(oko_get_be32(&finish[5]) == 0xC119BC6F, "finish crc");
    check(oko_boot_frame_finish(finish, 0, 0) == 0, "zero finish rejected");

    /* The boot-side parser treats the high half of the 32-bit wire fields as
     * an address error; the host always emits zero there. */
    check(erase[1] == 0 && erase[2] == 0, "erase high half zero");
    check(write[1] == 0 && write[2] == 0, "write high half zero");

    if (failures)
    {
        fprintf(stderr, "%d protocol test(s) failed\n", failures);
        return 1;
    }
    printf("protocol: ok\n");
    return 0;
}