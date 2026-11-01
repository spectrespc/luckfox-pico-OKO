/*
 * Host test for the 16 px raster alignment performed by the local RV1106 HAL
 * patch in source/src/hal/plus/rk_hal.c (rk_region_setbitmap): a 660x46
 * ARGB1555 source must be placed into the aligned 672x48 BGRA5551 canvas at
 * stride 672, without bleeding into the padding columns.
 *
 * This test is host-only and is not part of the package Makefile.
 *
 * Run:
 *   gcc -Wall -Wextra -O2 -o /tmp/bmp_align_test \
 *       project/app/divinus/tests/bmp_align_test.c && /tmp/bmp_align_test
 *
 * Expected output:
 *   ok 660x46 rows placed at stride 672
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    unsigned short w = 660, h = 46;
    unsigned short aw = (w + 15) & ~15, ah = (h + 15) & ~15;
    unsigned short *src = calloc((size_t)w * h, sizeof(*src));
    unsigned short *dst = calloc((size_t)aw * ah, sizeof(*dst));
    size_t i;
    if (!src || !dst) return 2;
    src[0] = 0xFC00;          /* row 0, col 0 */
    src[w] = 0x83E0;          /* row 1, col 0 */
    src[w + 5] = 0x801F;      /* row 1, col 5 */
    for (i = 0; i < (size_t)w * h; i++) {
        unsigned short s = src[i];
        unsigned int r, g, b;
        size_t row = i / w, col = i % w;
        if (!(s & 0x8000)) continue;
        r = (s >> 10) & 0x1F; g = (s >> 5) & 0x1F; b = s & 0x1F;
        dst[row * aw + col] = (b << 11) | (g << 6) | (r << 1) | 1;
    }
    if (aw != 672 || ah != 48) { printf("align FAIL %ux%u\n", aw, ah); return 1; }
    if (dst[0] != 0x3F) { printf("r0 FAIL %#x\n", dst[0]); return 1; }
    if (dst[672] != 0x7C1) { printf("r1c0 FAIL %#x\n", dst[672]); return 1; }
    if (dst[677] != 0xF801) { printf("r1c5 FAIL %#x\n", dst[677]); return 1; }
    if (dst[660] != 0) { printf("bleed FAIL %#x\n", dst[660]); return 1; }
    printf("ok %ux%u rows placed at stride %u\n", w, h, aw);
    return 0;
}