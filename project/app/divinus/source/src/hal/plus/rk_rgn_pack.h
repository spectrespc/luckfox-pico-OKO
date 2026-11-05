#pragma once

#include <stddef.h>
#include <stdint.h>

/* RV1106 RGN overlay raster helpers.  This module is HAL-free so the packing
 * can be exercised by a host test (tests/rk_rgn_pack_test.c). */

/* Aligns a dimension to 16 pixels, with a minimum of 16. */
unsigned int rk_rgn_pad16(unsigned int value);

/* Packs a tight ARGB1555 source (bit 15 = opaque) into a BGRA5551 canvas.
 * Transparent source pixels are skipped, so the caller must zero the whole
 * destination first; the padding between width and stride is never touched.
 * Returns the number of pixels written. */
unsigned int rk_rgn_pack_bgra5551(const uint16_t *src,
    unsigned int width, unsigned int height,
    uint16_t *dst, unsigned int stride);