#include <stdio.h>
#include <string.h>

#include "../source/src/analytics/box_draw.h"

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

static void test_normalize(void)
{
    int nx, ny, nw, nh;

    box_normalize_rect(0, 0, 640, 360, 640, 360, &nx, &ny, &nw, &nh);
    check_eq(nx, 0, "full x");
    check_eq(ny, 0, "full y");
    check_eq(nw, 10000, "full w");
    check_eq(nh, 10000, "full h");

    box_normalize_rect(160, 90, 320, 180, 640, 360, &nx, &ny, &nw, &nh);
    check_eq(nx, 2500, "quarter x");
    check_eq(ny, 2500, "quarter y");
    check_eq(nw, 5000, "half w");
    check_eq(nh, 5000, "half h");

    /* Clipped to the frame: -10,-10,20,20 -> 0,0,10,10 -> 156,277 */
    box_normalize_rect(-10, -10, 20, 20, 640, 360, &nx, &ny, &nw, &nh);
    check_eq(nx, 0, "clip x");
    check_eq(ny, 0, "clip y");
    check_eq(nw, 156, "clip w");
    check_eq(nh, 277, "clip h");

    box_normalize_rect(700, 0, 20, 20, 640, 360, &nx, &ny, &nw, &nh);
    check_eq(nw, 0, "outside w");
    check_eq(nh, 0, "outside h");

    box_normalize_rect(0, 0, 10, 10, 0, 0, &nx, &ny, &nw, &nh);
    check_eq(nw, 0, "zero source");
}

static void test_draw(void)
{
    unsigned char canvas[64 * 16 / 4];

    memset(canvas, 0, sizeof(canvas));
    /* 8x4 rectangle at 0,0; top and bottom lines are one line thick. */
    box_draw_rect_2bpp(canvas, 64, 16, 0, 0, 8, 4, 1, 0);
    check(canvas[0] == 0xff && canvas[1] == 0xff && canvas[2] == 0x00,
        "top line bytes");
    /* Second row: left edge byte plus right edge byte. */
    check(canvas[16] == 0x03 && canvas[18] == 0xc0, "edge bytes");
    /* Rows outside the rectangle stay clear. */
    check(canvas[64] == 0, "row after rectangle");

    /* Misaligned or too small rectangles are rejected without writing. */
    memset(canvas, 0, sizeof(canvas));
    box_draw_rect_2bpp(canvas, 64, 16, 2, 0, 8, 4, 1, 0);
    check(canvas[0] == 0, "misaligned rejected");
    box_draw_rect_2bpp(canvas, 64, 16, 0, 0, 10, 4, 1, 0);
    check(canvas[0] == 0, "unaligned width rejected");

    /* LUT 1 uses the dimmer bit pattern. */
    memset(canvas, 0, sizeof(canvas));
    box_draw_rect_2bpp(canvas, 64, 16, 0, 0, 8, 4, 1, 1);
    check(canvas[0] == 0xaa && canvas[1] == 0xaa, "lut 1 bytes");
}

int main(void)
{
    test_normalize();
    test_draw();

    if (failures) {
        fprintf(stderr, "%d/%d checks failed\n", failures, checks);
        return 1;
    }
    printf("ok box_draw (%d checks)\n", checks);
    return 0;
}