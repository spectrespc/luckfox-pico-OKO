/*
 * Host test for the 16 px raster alignment and BGRA5551 packing performed by
 * the local RV1106 HAL patch in source/src/hal/plus/rk_hal.c
 * (rk_region_setbitmap): a 660x46 ARGB1555 source must be placed into the
 * aligned 672x48 BGRA5551 canvas at stride 672, without bleeding into the
 * padding columns.
 *
 * The packing itself lives in source/src/hal/plus/rk_rgn_pack.c so the same
 * code is covered by tests/rk_rgn_pack_test.c.  This test is host-only and is
 * not part of the package Makefile.
 *
 * Run:
 *   gcc -std=c99 -Wall -Wextra -O2 \
 *       -Iproject/app/divinus/source/src/hal/plus \
 *       project/app/divinus/source/src/hal/plus/rk_rgn_pack.c \
 *       project/app/divinus/tests/bmp_align_test.c -o /tmp/bmp_align_test &&
 *   /tmp/bmp_align_test
 *
 * Expected output:
 *   ok 660x46 rows placed at stride 672
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rk_rgn_pack.h"

int main(void) {
    unsigned int w = 660, h = 46;
    unsigned int aw = rk_rgn_pad16(w), ah = rk_rgn_pad16(h);
    unsigned short *src = calloc((size_t)w * h, sizeof(*src));
    unsigned short *dst = calloc((size_t)aw * ah, sizeof(*dst));
    if (!src || !dst) return 2;
    src[0] = 0xFC00;          /* row 0, col 0 */
    src[w] = 0x83E0;          /* row 1, col 0 */
    src[w + 5] = 0x801F;      /* row 1, col 5 */
    if (aw != 672 || ah != 48) { printf("align FAIL %ux%u\n", aw, ah); return 1; }
    rk_rgn_pack_bgra5551(src, w, h, dst, aw);
    if (dst[0] != 0x803F) { printf("r0 FAIL %#x\n", dst[0]); return 1; }
    if (dst[672] != 0x87C1) { printf("r1c0 FAIL %#x\n", dst[672]); return 1; }
    if (dst[677] != 0xF801) { printf("r1c5 FAIL %#x\n", dst[677]); return 1; }
    if (dst[660] != 0) { printf("bleed FAIL %#x\n", dst[660]); return 1; }
    free(src);
    free(dst);
    printf("ok %ux%u rows placed at stride %u\n", w, h, aw);
    return 0;
}