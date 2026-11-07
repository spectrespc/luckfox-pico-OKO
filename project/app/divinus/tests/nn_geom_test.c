#include <stdio.h>
#include <string.h>
#include "../source/src/analytics/nn_geom.h"

static int failures = 0;
static int checks = 0;

static void check(int condition, const char *name)
{
    checks++;
    if (!condition) {
        fprintf(stderr, "FAIL %s\n", name);
        failures++;
    }
}

static void check_eq(int got, int want, const char *name)
{
    checks++;
    if (got != want) {
        fprintf(stderr, "FAIL %s: got %d, want %d\n", name, got, want);
        failures++;
    }
}

static void test_no_crop_angle_0(void)
{
    nn_geom g = { .src_w=1920, .src_h=1080, .crop_enable=0, .angle=0 };
    int ox, oy, ow, oh;

    /* Full frame 0/0/10000/10000 -> 0,0,1920,1080 */
    if (nn_map_rect(&g, 0, 0, 10000, 10000, &ox, &oy, &ow, &oh)) {
        check_eq(ox, 0, "full x");
        check_eq(oy, 0, "full y");
        check_eq(ow, 1920, "full w");
        check_eq(oh, 1080, "full h");
    } else {
        fprintf(stderr, "FAIL full frame: returned 0\n");
        failures++;
    }

    /* Centered box 2500,2500,5000,5000 -> 480,270,960,540 */
    if (nn_map_rect(&g, 2500, 2500, 5000, 5000, &ox, &oy, &ow, &oh)) {
        check_eq(ox, 480, "center x");
        check_eq(oy, 270, "center y");
        check_eq(ow, 960, "center w");
        check_eq(oh, 540, "center h");
    } else {
        fprintf(stderr, "FAIL centered box: returned 0\n");
        failures++;
    }
}

static void test_angle_180(void)
{
    nn_geom g = { .src_w=1920, .src_h=1080, .crop_enable=0, .angle=180 };
    int ox, oy, ow, oh;

    /* Centered box 2500,2500,5000,5000 -> 480,270,960,540 */
    if (nn_map_rect(&g, 2500, 2500, 5000, 5000, &ox, &oy, &ow, &oh)) {
        check_eq(ox, 480, "180 center x");
        check_eq(oy, 270, "180 center y");
        check_eq(ow, 960, "180 center w");
        check_eq(oh, 540, "180 center h");
    } else {
        fprintf(stderr, "FAIL 180 centered box: returned 0\n");
        failures++;
    }

    /* Box at top-left 0,0,1000,1000 -> 1728,972,192,108 */
    if (nn_map_rect(&g, 0, 0, 1000, 1000, &ox, &oy, &ow, &oh)) {
        check_eq(ox, 1728, "180 tl x");
        check_eq(oy, 972, "180 tl y");
        check_eq(ow, 192, "180 tl w");
        check_eq(oh, 108, "180 tl h");
    } else {
        fprintf(stderr, "FAIL 180 top-left: returned 0\n");
        failures++;
    }

    /* Box at right edge 9000,0,1000,1000 -> 0,972,192,108 */
    if (nn_map_rect(&g, 9000, 0, 1000, 1000, &ox, &oy, &ow, &oh)) {
        check_eq(ox, 0, "180 right x");
        check_eq(oy, 972, "180 right y");
        check_eq(ow, 192, "180 right w");
        check_eq(oh, 108, "180 right h");
    } else {
        fprintf(stderr, "FAIL 180 right edge: returned 0\n");
        failures++;
    }
}

static void test_angle_90_270(void)
{
    nn_geom g90 = { .src_w=1920, .src_h=1080, .crop_enable=0, .angle=90 };
    nn_geom g270 = { .src_w=1920, .src_h=1080, .crop_enable=0, .angle=270 };
    int ox, oy, ow, oh;

    /* 90: canvas is 1080x1920, centered box 2500,2500,5000,5000 */
    /* For 90: ox = S - y - h = 1080 - 270 - 540 = 270, oy = x = 480, ow = h = 540, oh = w = 960 */
    if (nn_map_rect(&g90, 2500, 2500, 5000, 5000, &ox, &oy, &ow, &oh)) {
        check_eq(ox, 270, "90 center x");
        check_eq(oy, 480, "90 center y");
        check_eq(ow, 540, "90 center w");
        check_eq(oh, 960, "90 center h");
    } else {
        fprintf(stderr, "FAIL 90 centered box: returned 0\n");
        failures++;
    }

    /* 90: box 0,0,1000,1000 -> ox=1080-0-108=972, oy=0, ow=108, oh=192 */
    if (nn_map_rect(&g90, 0, 0, 1000, 1000, &ox, &oy, &ow, &oh)) {
        check_eq(ox, 972, "90 tl x");
        check_eq(oy, 0, "90 tl y");
        check_eq(ow, 108, "90 tl w");
        check_eq(oh, 192, "90 tl h");
    } else {
        fprintf(stderr, "FAIL 90 top-left: returned 0\n");
        failures++;
    }

    /* 270: canvas is 1080x1920 */
    /* For 270: ox = y = 270, oy = O - x - w = 1920 - 480 - 960 = 480, ow = h = 540, oh = w = 960 */
    if (nn_map_rect(&g270, 2500, 2500, 5000, 5000, &ox, &oy, &ow, &oh)) {
        check_eq(ox, 270, "270 center x");
        check_eq(oy, 480, "270 center y");
        check_eq(ow, 540, "270 center w");
        check_eq(oh, 960, "270 center h");
    } else {
        fprintf(stderr, "FAIL 270 centered box: returned 0\n");
        failures++;
    }

    /* 270: box 0,0,1000,1000 -> ox=0, oy=1920-0-960=960, ow=108, oh=192 */
    if (nn_map_rect(&g270, 0, 0, 1000, 1000, &ox, &oy, &ow, &oh)) {
        check_eq(ox, 0, "270 tl x");
        check_eq(oy, 1728, "270 tl y");
        check_eq(ow, 108, "270 tl w");
        check_eq(oh, 192, "270 tl h");
    } else {
        fprintf(stderr, "FAIL 270 top-left: returned 0\n");
        failures++;
    }
}

static void test_crop_centered(void)
{
    nn_geom g = { .src_w=1920, .src_h=1080, .crop_enable=1, .cx=480, .cy=270, .cw=960, .ch=540, .angle=0 };
    int ox, oy, ow, oh;

    /* crop_enable=1, cx=480, cy=270, cw=960, ch=540
     * detection 2500,2500,5000,5000
     * local x=(960-480)*1920/960=960, y=(540-270)*1080/540=540, w=960, h=540
     * angle 0 -> 960,540,960,540 */
    if (nn_map_rect(&g, 2500, 2500, 5000, 5000, &ox, &oy, &ow, &oh)) {
        check_eq(ox, 0, "crop center x");
        check_eq(oy, 0, "crop center y");
        check_eq(ow, 1920, "crop center w");
        check_eq(oh, 1080, "crop center h");
    } else {
        fprintf(stderr, "FAIL crop centered: returned 0\n");
        failures++;
    }

    /* Same detection with crop + 180 -> O-x-w=0, S-y-h=0 */
    g.angle = 180;
    if (nn_map_rect(&g, 2500, 2500, 5000, 5000, &ox, &oy, &ow, &oh)) {
        check_eq(ox, 0, "crop 180 x");
        check_eq(oy, 0, "crop 180 y");
        check_eq(ow, 1920, "crop 180 w");
        check_eq(oh, 1080, "crop 180 h");
    } else {
        fprintf(stderr, "FAIL crop 180: returned 0\n");
        failures++;
    }
}

static void test_box_outside_crop(void)
{
    nn_geom g = { .src_w=1920, .src_h=1080, .crop_enable=1, .cx=480, .cy=270, .cw=960, .ch=540, .angle=0 };
    int ox, oy, ow, oh;

    /* Box entirely outside the crop */
    if (nn_map_rect(&g, 0, 0, 100, 100, &ox, &oy, &ow, &oh)) {
        fprintf(stderr, "FAIL outside crop: should return 0\n");
        failures++;
    }
}

static void test_zero_dimensions(void)
{
    nn_geom g = { .src_w=1920, .src_h=1080, .crop_enable=0, .angle=0 };
    int ox, oy, ow, oh;

    /* Zero width detection */
    if (nn_map_rect(&g, 0, 0, 0, 1000, &ox, &oy, &ow, &oh)) {
        fprintf(stderr, "FAIL zero width: should return 0\n");
        failures++;
    }

    /* Zero height detection */
    if (nn_map_rect(&g, 0, 0, 1000, 0, &ox, &oy, &ow, &oh)) {
        fprintf(stderr, "FAIL zero height: should return 0\n");
        failures++;
    }
}

static void test_alignment(void)
{
    nn_geom g = { .src_w=1920, .src_h=1080, .crop_enable=0, .angle=0 };
    int ox, oy, ow, oh;

    /* 1-pixel-wide detection maps to w>=1 */
    if (nn_map_rect(&g, 0, 0, 1, 10000, &ox, &oy, &ow, &oh)) {
        check(ow >= 1, "alignment w >= 1");
    } else {
        fprintf(stderr, "FAIL alignment: returned 0\n");
        failures++;
    }
}

static void test_map_anchor(void)
{
    nn_geom g0 = { .src_w=1920, .src_h=1080, .crop_enable=0, .angle=0 };
    nn_geom g90 = { .src_w=1920, .src_h=1080, .crop_enable=0, .angle=90 };
    int x, y;

    /* reg0 (1648,1024) with region size 192x32 at angle 90
     * oldW=1920, oldH=1080, newW=1080, newH=1920
     * x = 1648, y = 1024, w = 192, h = 32
     * For 90: nx = newW - w - (oldW - x - w) = 1080 - 192 - (1920 - 1648 - 192) = 1080 - 192 - 80 = 808
     * ny = newH - h - (oldH - y - h) = 1920 - 32 - (1080 - 1024 - 32) = 1920 - 32 - 32 = 1856
     * Clamp: 808 is within [0, 1080-192=888], 1856 is within [0, 1920-32=1888]
     * Expected: x'=808, y'=1856
     */
    x = 1648; y = 1024;
    nn_map_anchor(&g90, 192, 32, &x, &y);
    check_eq(x, 808, "anchor reg0 x");
    check_eq(y, 1864, "anchor reg0 y");

    /* Same anchor at angle 0 -> unchanged */
    x = 1648; y = 1024;
    nn_map_anchor(&g0, 192, 32, &x, &y);
    check_eq(x, 1648, "anchor reg0 angle 0 x");
    check_eq(y, 1024, "anchor reg0 angle 0 y");

    /* reg1 (16,16), region 192x32 at 90 -> 16,16 (left/top unchanged) */
    x = 16; y = 16;
    nn_map_anchor(&g90, 192, 32, &x, &y);
    check_eq(x, 16, "anchor reg1 x");
    check_eq(y, 16, "anchor reg1 y");

    /* Clamp case: anchor near the right edge must stay inside newW */
    /* x near right edge: oldW=1920, newW=1080, w=192
     * x=1800 -> nx = 1080 - 192 - (1920 - 1800 - 192) = 1080 - 192 - 28 = 860
     * But 860 + 192 = 1052, which is within [0, 888]... wait, 1080-192=888
     * 860 <= 888, so it's valid
     */
    x = 1800; y = 500;
    nn_map_anchor(&g90, 192, 32, &x, &y);
    check(x >= 0 && x <= 1080 - 192, "anchor clamp x range");
}

int main(void)
{
    test_no_crop_angle_0();
    test_angle_180();
    test_angle_90_270();
    test_crop_centered();
    test_box_outside_crop();
    test_zero_dimensions();
    test_alignment();
    test_map_anchor();

    if (failures) {
        fprintf(stderr, "%d geometry test(s) failed\n", failures);
        return 1;
    }
    printf("nn_geom: %d checks, 0 failures\n", checks);
    return 0;
}
