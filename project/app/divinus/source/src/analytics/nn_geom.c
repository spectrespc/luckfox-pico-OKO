#include <stddef.h>
#include "nn_geom.h"

int nn_map_rect(const nn_geom *g, int nx, int ny, int nw, int nh,
                int *ox, int *oy, int *ow, int *oh)
{
    int px, py, pw, ph;
    int x, y, w, h;
    int x0, y0, x1, y1;
    int O, S;

    /* Validate inputs */
    if (g == NULL || ox == NULL || oy == NULL || ow == NULL || oh == NULL)
        return 0;
    if (g->src_w <= 0 || g->src_h <= 0 || nw <= 0 || nh <= 0)
        return 0;

    /* Normalize to pixel coordinates */
    px = nx * g->src_w / 10000;
    py = ny * g->src_h / 10000;
    pw = nw * g->src_w / 10000;
    ph = nh * g->src_h / 10000;

    /* Ensure minimum size */
    if (pw < 1) pw = 1;
    if (ph < 1) ph = 1;

    /* Apply ePTZ crop if enabled */
    if (g->crop_enable && g->cw > 0 && g->ch > 0) {
        x0 = px;
        if (x0 < g->cx) x0 = g->cx;
        y0 = py;
        if (y0 < g->cy) y0 = g->cy;

        x1 = px + pw;
        if (x1 > g->cx + g->cw) x1 = g->cx + g->cw;
        y1 = py + ph;
        if (y1 > g->cy + g->ch) y1 = g->cy + g->ch;

        if (x1 <= x0 || y1 <= y0)
            return 0;

        x = (x0 - g->cx) * g->src_w / g->cw;
        y = (y0 - g->cy) * g->src_h / g->ch;
        w = (x1 - x0) * g->src_w / g->cw;
        h = (y1 - y0) * g->src_h / g->ch;
    } else {
        x = px;
        y = py;
        w = pw;
        h = ph;
    }

    /* Apply rotation */
    O = g->src_w;
    S = g->src_h;

    switch (g->angle) {
    case 0:
        *ox = x;
        *oy = y;
        *ow = w;
        *oh = h;
        break;
    case 180:
        *ox = O - x - w;
        *oy = S - y - h;
        *ow = w;
        *oh = h;
        break;
    case 90:
        *ox = S - y - h;
        *oy = x;
        *ow = h;
        *oh = w;
        break;
    case 270:
        *ox = y;
        *oy = O - x - w;
        *ow = h;
        *oh = w;
        break;
    default:
        /* Treat any other angle as 0 */
        *ox = x;
        *oy = y;
        *ow = w;
        *oh = h;
        break;
    }

    /* Validate output dimensions */
    if (*ow <= 0 || *oh <= 0)
        return 0;

    return 1;
}

void nn_map_anchor(const nn_geom *g, int w, int h, int *x, int *y)
{
    int oldW, oldH;
    int newW, newH;
    int nx, ny;

    if (g == NULL || x == NULL || y == NULL || w <= 0 || h <= 0)
        return;

    oldW = g->src_w;
    oldH = g->src_h;

    /* Compute new canvas dimensions based on rotation */
    if (g->angle == 90 || g->angle == 270) {
        newW = oldH;
        newH = oldW;
    } else {
        newW = oldW;
        newH = oldH;
    }

    /* Compute nearest edge per axis (mirror transform) */
    if (*x + w/2 < oldW/2) {
        nx = *x;
    } else {
        nx = newW - w - (oldW - *x - w);
    }

    if (*y + h/2 < oldH/2) {
        ny = *y;
    } else {
        ny = newH - h - (oldH - *y - h);
    }

    /* Clamp x */
    if (nx < 0) nx = 0;
    if (nx > newW - w) nx = (newW > w) ? newW - w : 0;

    /* Clamp y */
    if (ny < 0) ny = 0;
    if (ny > newH - h) ny = (newH > h) ? newH - h : 0;

    *x = nx;
    *y = ny;
}
