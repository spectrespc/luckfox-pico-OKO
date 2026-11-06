/* Host tests for the pure ROI geometry used by the focus assistant. */
#include <stdio.h>

#include "focus_geom.h"

static int failures;

static void check(int condition, const char *name)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL %s\n", name);
        failures++;
    }
}

static void check_eq(int got, int want, const char *name)
{
    if (got != want)
    {
        fprintf(stderr, "FAIL %s: got %d, want %d\n", name, got, want);
        failures++;
    }
}

int main(void)
{
    int x, y, w, h;

    /* Full-frame values survive. */
    x = 0; y = 0; w = 10000; h = 10000;
    focus_geom_clamp(&x, &y, &w, &h);
    check_eq(x, 0, "full x"); check_eq(y, 0, "full y");
    check_eq(w, 10000, "full w"); check_eq(h, 10000, "full h");

    /* Minimum ROI and edge clamping. */
    x = -50; y = -50; w = 500; h = 500;
    focus_geom_clamp(&x, &y, &w, &h);
    check_eq(x, 0, "negative x"); check_eq(y, 0, "negative y");
    check_eq(w, FOCUS_BP_MIN_ROI, "min w"); check_eq(h, FOCUS_BP_MIN_ROI, "min h");

    x = 9900; y = 9900; w = 5000; h = 5000;
    focus_geom_clamp(&x, &y, &w, &h);
    check_eq(x, 5000, "right edge x");
    check_eq(y, 5000, "bottom edge y");
    check_eq(w, 5000, "right edge w");
    check_eq(h, 5000, "bottom edge h");

    /* A full-width request pinned to the right edge becomes the full frame. */
    x = 9000; y = 0; w = 10000; h = 10000;
    focus_geom_clamp(&x, &y, &w, &h);
    check_eq(x, 0, "full width x");
    check_eq(w, 10000, "full width w");

    /* Basis points -> pixels. */
    focus_geom_bp_to_px(0, 0, 10000, 10000, 1920, 1080, &x, &y, &w, &h);
    check_eq(x, 0, "px full x"); check_eq(y, 0, "px full y");
    check_eq(w, 1920, "px full w"); check_eq(h, 1080, "px full h");

    focus_geom_bp_to_px(2500, 2500, 5000, 5000, 1920, 1080, &x, &y, &w, &h);
    check_eq(x, 480, "px center x"); check_eq(y, 270, "px center y");
    check_eq(w, 960, "px center w"); check_eq(h, 540, "px center h");

    /* Pixel -> basis points round trip (within one basis point). */
    focus_geom_px_to_bp(480, 270, 960, 540, 1920, 1080, &x, &y, &w, &h);
    check(x >= 2499 && x <= 2501, "bp center x");
    check(y >= 2499 && y <= 2501, "bp center y");
    check(w >= 4999 && w <= 5001, "bp center w");
    check(h >= 4999 && h <= 5001, "bp center h");

    /* Preview mirror/flip inversion. */
    x = 100; y = 50; w = 200; h = 100;
    focus_geom_preview_to_sensor(1, 1, 1920, 1080, &x, &y, &w, &h);
    check_eq(x, 1620, "mirror x");
    check_eq(y, 930, "flip y");
    check_eq(w, 200, "mirror w");
    check_eq(h, 100, "flip h");

    x = 100; y = 50; w = 200; h = 100;
    focus_geom_preview_to_sensor(0, 0, 1920, 1080, &x, &y, &w, &h);
    check_eq(x, 100, "no mirror x");
    check_eq(y, 50, "no flip y");

    if (failures)
    {
        fprintf(stderr, "%d geometry test(s) failed\n", failures);
        return 1;
    }
    printf("focus_geom: ok\n");
    return 0;
}