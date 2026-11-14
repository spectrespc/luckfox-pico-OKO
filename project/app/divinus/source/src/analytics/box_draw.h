#ifndef BOX_DRAW_H
#define BOX_DRAW_H

/* Shared drawing helpers for the 2BPP overlay regions (YOLO boxes and IVS
 * rectangles).  The canvas is one byte per four pixels; only two palette
 * entries are available. */

/* Draws a rectangle into a 2BPP canvas whose rows are `stride` pixels wide.
 * `lut` selects the palette entry (0 or 1).  The vendor limit of four pixels
 * per line is kept; x and the width must be multiples of 4. */
void box_draw_rect_2bpp(unsigned char *buffer, unsigned int stride,
    unsigned int height, int rx, int ry, int rw, int rh, int line, int lut);

/* Converts a rectangle in source pixels (srcW x srcH) to the 0..10000
 * coordinate space used by nn_map_rect().  The rectangle is clipped to the
 * source frame; an empty result yields zeros. */
void box_normalize_rect(int x, int y, int w, int h, int srcW, int srcH,
    int *nx, int *ny, int *nw, int *nh);

#endif