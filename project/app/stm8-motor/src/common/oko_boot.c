#include "oko_boot.h"
#include "oko_crc.h"

int oko_header_pack(const struct oko_app_header *hdr, uint8_t out[OKO_HDR_SIZE])
{
    uint16_t i;

    if (!hdr || !out)
        return -1;

    for (i = 0; i < 4; i++)
        out[i] = hdr->magic[i];
    oko_put_be32(&out[4], hdr->length);
    out[8] = hdr->major;
    out[9] = hdr->minor;
    out[10] = hdr->patch;
    out[11] = hdr->flags;
    oko_put_be32(&out[12], hdr->crc32);
    return 0;
}

int oko_header_unpack(const uint8_t raw[OKO_HDR_SIZE], struct oko_app_header *hdr)
{
    uint16_t i;

    if (!raw || !hdr)
        return -1;

    for (i = 0; i < 4; i++)
        hdr->magic[i] = raw[i];
    hdr->length = oko_get_be32(&raw[4]);
    hdr->major = raw[8];
    hdr->minor = raw[9];
    hdr->patch = raw[10];
    hdr->flags = raw[11];
    hdr->crc32 = oko_get_be32(&raw[12]);
    return 0;
}

int oko_header_valid(const struct oko_app_header *hdr)
{
    if (!hdr)
        return 0;
    if (hdr->magic[0] != OKO_MAGIC0 || hdr->magic[1] != OKO_MAGIC1 ||
        hdr->magic[2] != OKO_MAGIC2 || hdr->magic[3] != OKO_MAGIC3)
        return 0;
    if (hdr->length == 0 || hdr->length > OKO_APP_MAX_SIZE)
        return 0;
    return 1;
}
size_t oko_boot_frame_erase(uint8_t out[9], uint16_t start, uint16_t length)
{
    if (!out || length == 0)
        return 0;
    out[0] = BOOT_CMD_ERASE;
    oko_put_be32(&out[1], start);
    oko_put_be32(&out[5], length);
    return 9;
}

size_t oko_boot_frame_write(uint8_t out[8 + BOOT_WRITE_MAX], uint16_t address,
                            const uint8_t *data, uint8_t length)
{
    uint16_t crc;

    if (!out || !data || length == 0 || length > BOOT_WRITE_MAX ||
        (length & 3u) != 0)
        return 0;
    out[0] = BOOT_CMD_WRITE;
    oko_put_be32(&out[1], address);
    out[5] = length;
    {
        uint8_t i;
        for (i = 0; i < length; i++)
            out[6 + i] = data[i];
    }
    crc = oko_crc16(&out[1], (uint16_t)(5 + length));
    oko_put_be16(&out[6 + length], crc);
    return (size_t)(8 + length);
}

size_t oko_boot_frame_finish(uint8_t out[9], uint16_t length, uint32_t crc32)
{
    if (!out || length == 0)
        return 0;
    out[0] = BOOT_CMD_FINISH;
    oko_put_be32(&out[1], length);
    oko_put_be32(&out[5], crc32);
    return 9;
}
