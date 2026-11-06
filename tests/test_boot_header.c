/* Host tests for the 16-byte application header. */
#include <stdio.h>
#include <string.h>

#include "oko_boot.h"

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
    struct oko_app_header hdr;
    struct oko_app_header back;
    uint8_t raw[OKO_HDR_SIZE];

    memset(&hdr, 0, sizeof(hdr));
    hdr.magic[0] = OKO_MAGIC0;
    hdr.magic[1] = OKO_MAGIC1;
    hdr.magic[2] = OKO_MAGIC2;
    hdr.magic[3] = OKO_MAGIC3;
    hdr.length = 4246;
    hdr.major = 2;
    hdr.minor = 0;
    hdr.patch = 1;
    hdr.flags = 0;
    hdr.crc32 = 0xC119BC6F;

    check(oko_header_pack(&hdr, raw) == 0, "pack");
    check(memcmp(raw, "OKO8", 4) == 0, "magic bytes");
    check(raw[4] == 0x00 && raw[5] == 0x00 && raw[6] == 0x10 && raw[7] == 0x96,
          "length big-endian");
    check(raw[8] == 2 && raw[9] == 0 && raw[10] == 1 && raw[11] == 0,
          "version bytes");
    check(raw[12] == 0xC1 && raw[13] == 0x19 && raw[14] == 0xBC &&
          raw[15] == 0x6F, "crc big-endian");

    check(oko_header_unpack(raw, &back) == 0, "unpack");
    check(back.length == hdr.length && back.crc32 == hdr.crc32 &&
          back.major == hdr.major && back.minor == hdr.minor &&
          back.patch == hdr.patch, "round trip");
    check(oko_header_valid(&back) == 1, "valid header");

    back.length = 0;
    check(oko_header_valid(&back) == 0, "zero length rejected");
    back.length = OKO_APP_MAX_SIZE + 1;
    check(oko_header_valid(&back) == 0, "too long rejected");
    back.length = OKO_APP_MAX_SIZE;
    check(oko_header_valid(&back) == 1, "maximum length accepted");
    back.magic[0] = 'X';
    check(oko_header_valid(&back) == 0, "bad magic rejected");

    check(OKO_HDR_BASE + OKO_HDR_SIZE == OKO_APP_BASE, "header/app boundary");
    check(OKO_APP_BASE + OKO_APP_MAX_SIZE == OKO_APP_END, "app slot size");

    if (failures)
    {
        fprintf(stderr, "%d header test(s) failed\n", failures);
        return 1;
    }
    printf("header: ok\n");
    return 0;
}