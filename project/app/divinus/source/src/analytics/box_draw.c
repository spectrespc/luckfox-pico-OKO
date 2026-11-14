#include "box_draw.h"

#include <string.h>

static const unsigned char rgn_lut_0_left[4] = {0x03, 0x0f, 0x3f, 0xff};
static const unsigned char rgn_lut_0_right[4] = {0xc0, 0xf0, 0xfc, 0xff};
static const unsigned char rgn_lut_1_left[4] = {0x02, 0x0a, 0x2a, 0xaa};
static const unsigned char rgn_lut_1_right[4] = {0x80, 0xa0, 0xa8, 0xaa};

void box_draw_rect_2bpp(unsigned char *buffer, unsigned int stride,
    unsigned int height, int rx, int ry, int rw, int rh, int line, int lut)
{
    unsigned char *ptr = buffer;
    unsigned char value = lut == 1 ? 0xaa : 0xff;

    (void)height;

    if (!buffer || line < 1 || line > 4 || rw < 4 || rh < line * 2 ||
        rx < 0 || ry < 0 || (rx & 3) || (rw & 3))
        return;

    ptr += (stride * (unsigned int)ry + (unsigned int)rx) >> 2;

    /* Top line. */
    for (int i = 0; i < line; i++) {
        memset(ptr, value, (size_t)((rw + 3) >> 2));
        ptr += stride >> 2;
    }

    /* Left and right edges. */
    for (int i = 0; i < rh - line * 2; i++) {
        if (lut == 1) {
            *ptr = rgn_lut_1_left[line - 1];
            *(ptr + ((rw + 3) >> 2)) = rgn_lut_1_right[line - 1];
        } else {
            *ptr = rgn_lut_0_left[line - 1];
            *(ptr + ((rw + 3) >> 2)) = rgn_lut_0_right[line - 1];
        }
        ptr += stride >> 2;
    }

    /* Bottom line. */
    for (int i = 0; i < line; i++) {
        memset(ptr, value, (size_t)((rw + 3) >> 2));
        ptr += stride >> 2;
    }
}

static int scale_10000(int value, int size)
{
    long long scaled = (long long)value * 10000 / size;

    if (scaled < 0) scaled = 0;
    if (scaled > 10000) scaled = 10000;
    return (int)scaled;
}

void box_normalize_rect(int x, int y, int w, int h, int srcW, int srcH,
    int *nx, int *ny, int *nw, int *nh)
{
    if (!nx || !ny || !nw || !nh)
        return;

    *nx = *ny = *nw = *nh = 0;
    if (srcW <= 0 || srcH <= 0)
        return;

    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x >= srcW || y >= srcH || w <= 0 || h <= 0)
        return;
    if (x + w > srcW) w = srcW - x;
    if (y + h > srcH) h = srcH - y;
    if (w <= 0 || h <= 0)
        return;

    *nx = scale_10000(x, srcW);
    *ny = scale_10000(y, srcH);
    *nw = scale_10000(w, srcW);
    *nh = scale_10000(h, srcH);
    /* Rounding may push the far edge past the frame by one unit. */
    if (*nx + *nw > 10000) *nw = 10000 - *nx;
    if (*ny + *nh > 10000) *nh = 10000 - *ny;
}