#include "focus_geom.h"

static int clamp_int(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void focus_geom_clamp(int *x, int *y, int *w, int *h)
{
    int left = x ? *x : 0;
    int top = y ? *y : 0;
    int width = w ? *w : FOCUS_BP_FULL;
    int height = h ? *h : FOCUS_BP_FULL;

    width = clamp_int(width, FOCUS_BP_MIN_ROI, FOCUS_BP_FULL);
    height = clamp_int(height, FOCUS_BP_MIN_ROI, FOCUS_BP_FULL);
    left = clamp_int(left, 0, FOCUS_BP_FULL - width);
    top = clamp_int(top, 0, FOCUS_BP_FULL - height);

    if (x) *x = left;
    if (y) *y = top;
    if (w) *w = width;
    if (h) *h = height;
}

void focus_geom_bp_to_px(int bx, int by, int bw, int bh,
                         int imgw, int imgh,
                         int *x, int *y, int *w, int *h)
{
    int left, top, width, height;

    if (imgw <= 0) imgw = 1;
    if (imgh <= 0) imgh = 1;

    focus_geom_clamp(&bx, &by, &bw, &bh);
    left = bx * imgw / FOCUS_BP_FULL;
    top = by * imgh / FOCUS_BP_FULL;
    width = bw * imgw / FOCUS_BP_FULL;
    height = bh * imgh / FOCUS_BP_FULL;

    if (width < 1) width = 1;
    if (height < 1) height = 1;
    if (left + width > imgw) left = imgw - width;
    if (top + height > imgh) top = imgh - height;
    if (left < 0) left = 0;
    if (top < 0) top = 0;

    if (x) *x = left;
    if (y) *y = top;
    if (w) *w = width;
    if (h) *h = height;
}

void focus_geom_px_to_bp(int x, int y, int w, int h,
                         int imgw, int imgh,
                         int *bx, int *by, int *bw, int *bh)
{
    int vx, vy, vw, vh;

    if (imgw <= 0) imgw = 1;
    if (imgh <= 0) imgh = 1;

    x = clamp_int(x, 0, imgw - 1);
    y = clamp_int(y, 0, imgh - 1);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (x + w > imgw) w = imgw - x;
    if (y + h > imgh) h = imgh - y;

    vx = (x * FOCUS_BP_FULL + imgw / 2) / imgw;
    vy = (y * FOCUS_BP_FULL + imgh / 2) / imgh;
    vw = (w * FOCUS_BP_FULL + imgw / 2) / imgw;
    vh = (h * FOCUS_BP_FULL + imgh / 2) / imgh;
    focus_geom_clamp(&vx, &vy, &vw, &vh);

    if (bx) *bx = vx;
    if (by) *by = vy;
    if (bw) *bw = vw;
    if (bh) *bh = vh;
}

void focus_geom_preview_to_sensor(int mirror, int flip, int imgw, int imgh,
                                  int *x, int *y, int *w, int *h)
{
    int left = x ? *x : 0;
    int top = y ? *y : 0;
    int width = w ? *w : imgw;
    int height = h ? *h : imgh;

    if (mirror)
        left = imgw - (left + width);
    if (flip)
        top = imgh - (top + height);

    if (x) *x = left;
    if (y) *y = top;
    if (w) *w = width;
    if (h) *h = height;
}