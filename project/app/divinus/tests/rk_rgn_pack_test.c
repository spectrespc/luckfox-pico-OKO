/*
 * Host test for the RV1106 RGN raster helpers used by the local HAL patch
 * (source/src/hal/plus/rk_hal.c, rk_region_setbitmap): 16 px alignment and
 * the ARGB1555 -> BGRA5551 packing that keeps the alpha bit in both bit 0 and
 * bit 15.
 *
 * This test is host-only and is not part of the package Makefile.
 *
 * Run:
 *   gcc -std=c99 -Wall -Wextra -o /tmp/rk_rgn_pack_test \
 *       project/app/divinus/source/src/hal/plus/rk_rgn_pack.c \
 *       project/app/divinus/tests/rk_rgn_pack_test.c && /tmp/rk_rgn_pack_test
 *
 * Expected output:
 *   ok rk_rgn_pack
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../source/src/hal/plus/rk_rgn_pack.h"

static int failures;

#define CHECK(cond, ...) \
    do { \
        if (!(cond)) { \
            printf("FAIL "); \
            printf(__VA_ARGS__); \
            printf("\n"); \
            failures++; \
        } \
    } while (0)

static void test_pad16(void)
{
    static const struct {
        unsigned int in;
        unsigned int out;
    } cases[] = {
        { 0, 16 }, { 1, 16 }, { 15, 16 }, { 16, 16 },
        { 17, 32 }, { 660, 672 }, { 1080, 1088 },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        CHECK(rk_rgn_pad16(cases[i].in) == cases[i].out,
            "pad16(%u) = %u, expected %u", cases[i].in,
            rk_rgn_pad16(cases[i].in), cases[i].out);
}

static void test_single_pixel(void)
{
    uint16_t src[1];
    uint16_t dst[1];
    unsigned int painted;

    src[0] = 0xFC00;            /* opaque white */
    dst[0] = 0;
    painted = rk_rgn_pack_bgra5551(src, 1, 1, dst, 1);
    CHECK(painted == 1, "single opaque count %u", painted);
    CHECK(dst[0] == 0x803F, "single opaque %04X", dst[0]);

    src[0] = 0x801F;            /* opaque blue */
    dst[0] = 0;
    painted = rk_rgn_pack_bgra5551(src, 1, 1, dst, 1);
    CHECK(painted == 1, "single blue count %u", painted);
    CHECK(dst[0] == 0xF801, "single blue %04X", dst[0]);

    src[0] = 0x0000;            /* transparent */
    dst[0] = 0xDEAD;
    painted = rk_rgn_pack_bgra5551(src, 1, 1, dst, 1);
    CHECK(painted == 0, "transparent count %u", painted);
    CHECK(dst[0] == 0xDEAD, "transparent wrote %04X", dst[0]);
}

static void test_padded_raster(void)
{
    const unsigned int w = 660, h = 46;
    unsigned int aw = rk_rgn_pad16(w), ah = rk_rgn_pad16(h);
    uint16_t *src = calloc((size_t)w * h, sizeof(*src));
    uint16_t *dst = calloc((size_t)aw * ah, sizeof(*dst));
    unsigned int painted;

    if (!src || !dst) {
        printf("FAIL allocation\n");
        failures++;
        free(src);
        free(dst);
        return;
    }

    CHECK(aw == 672 && ah == 48, "align %ux%u", aw, ah);

    src[0] = 0xFC00;            /* row 0, col 0 */
    src[w] = 0x83E0;            /* row 1, col 0 */
    src[w + 5] = 0x801F;        /* row 1, col 5 */

    painted = rk_rgn_pack_bgra5551(src, w, h, dst, aw);
    CHECK(painted == 3, "padded count %u", painted);
    CHECK(dst[0] == 0x803F, "r0c0 %04X", dst[0]);
    CHECK(dst[672] == 0x87C1, "r1c0 %04X", dst[672]);
    CHECK(dst[677] == 0xF801, "r1c5 %04X", dst[677]);
    CHECK(dst[660] == 0, "bleed r0c660 %04X", dst[660]);
    CHECK(dst[671] == 0, "bleed r0c671 %04X", dst[671]);

    free(src);
    free(dst);
}

static void test_tight_stride(void)
{
    uint16_t src[4] = { 0xFC00, 0x0000, 0x0000, 0x801F };
    uint16_t dst[4] = { 0xDEAD, 0xDEAD, 0xDEAD, 0xDEAD };
    unsigned int painted;

    painted = rk_rgn_pack_bgra5551(src, 2, 2, dst, 2);
    CHECK(painted == 2, "tight count %u", painted);
    CHECK(dst[0] == 0x803F, "tight r0c0 %04X", dst[0]);
    CHECK(dst[1] == 0xDEAD, "tight r0c1 %04X", dst[1]);
    CHECK(dst[2] == 0xDEAD, "tight r1c0 %04X", dst[2]);
    CHECK(dst[3] == 0xF801, "tight r1c1 %04X", dst[3]);
}

static void test_bounds(void)
{
    const unsigned int w = 5, h = 3, stride = 8;
    uint16_t src[15];
    uint16_t dst[24];
    unsigned int painted;

    memset(src, 0, sizeof(src));
    memset(dst, 0, sizeof(dst));
    src[0] = 0xFC00;                        /* top-left */
    src[w * (h - 1) + (w - 1)] = 0x801F;    /* bottom-right */

    painted = rk_rgn_pack_bgra5551(src, w, h, dst, stride);
    CHECK(painted == 2, "bounds count %u", painted);
    CHECK(dst[0] == 0x803F, "bounds r0c0 %04X", dst[0]);
    CHECK(dst[(h - 1) * stride + (w - 1)] == 0xF801, "bounds r2c4 %04X",
        dst[(h - 1) * stride + (w - 1)]);
    CHECK(dst[w] == 0, "bounds row0 padding %04X", dst[w]);
    CHECK(dst[stride * h - 1] == 0, "bounds row2 padding %04X",
        dst[stride * h - 1]);
}

static void test_invalid(void)
{
    uint16_t src[4] = { 0xFC00, 0, 0, 0 };
    uint16_t dst[4] = { 0, 0, 0, 0 };

    CHECK(rk_rgn_pack_bgra5551(NULL, 2, 2, dst, 2) == 0, "NULL source");
    CHECK(rk_rgn_pack_bgra5551(src, 2, 2, NULL, 2) == 0, "NULL destination");
    CHECK(rk_rgn_pack_bgra5551(src, 2, 2, dst, 1) == 0, "stride < width");
}

int main(void)
{
    test_pad16();
    test_single_pixel();
    test_padded_raster();
    test_tight_stride();
    test_bounds();
    test_invalid();

    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("ok rk_rgn_pack\n");
    return 0;
}