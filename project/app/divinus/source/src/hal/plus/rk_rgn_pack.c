#include "rk_rgn_pack.h"

unsigned int rk_rgn_pad16(unsigned int value)
{
    unsigned int aligned = (value + 15u) & ~15u;

    return aligned < 16u ? 16u : aligned;
}

unsigned int rk_rgn_pack_bgra5551(const uint16_t *src,
    unsigned int width, unsigned int height,
    uint16_t *dst, unsigned int stride)
{
    unsigned int painted = 0;

    if (!src || !dst || !width || !height || stride < width)
        return 0;

    for (unsigned int row = 0; row < height; row++) {
        const uint16_t *line = src + (size_t)row * width;
        uint16_t *out = dst + (size_t)row * stride;

        for (unsigned int col = 0; col < width; col++) {
            uint16_t s = line[col];
            unsigned int r, g, b;

            if (!(s & 0x8000u)) continue;
            r = (s >> 10) & 0x1Fu;
            g = (s >> 5) & 0x1Fu;
            b = s & 0x1Fu;
            out[col] = (uint16_t)((b << 11) | (g << 6) | (r << 1) | 1u | 0x8000u);
            painted++;
        }
    }

    return painted;
}