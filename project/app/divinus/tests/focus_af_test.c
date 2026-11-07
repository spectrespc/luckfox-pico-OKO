/* Host test for the focus AF decision rules.
 *
 * Build/run:
 *   gcc -Wall -Wextra -O2 -I source/src tests/focus_af_test.c \
 *       source/src/focus_af_algo.c -o /tmp/focus_af_test
 *   /tmp/focus_af_test
 */
#include <stdio.h>

#include "focus_af_algo.h"

static int failures;

static void check(int ok, const char *what)
{
    if (!ok)
    {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

static void test_direction(void)
{
    check(af_pick_direction(100, 150, 90) == 1, "plus wins");
    check(af_pick_direction(100, 90, 150) == -1, "minus wins");
    check(af_pick_direction(100, 150, 200) == -1, "larger minus wins");
    check(af_pick_direction(100, 200, 150) == 1, "larger plus wins");
    check(af_pick_direction(100, 100, 100) == 0, "flat is a local peak");
    check(af_pick_direction(100, 90, 90) == 0, "both drops is a local peak");
    check(af_pick_direction(0, 1, 0) == 1, "a tiny gain still counts");
}

static void test_tracker(void)
{
    af_tracker tracker;

    af_tracker_reset(&tracker, 3, 100, 0);
    check(af_tracker_update(&tracker, 120, 10) == 0, "a gain continues");
    check(tracker.best_metric == 120 && tracker.best_position == 10,
          "a gain moves the best point");
    check(tracker.misses == 0, "a gain resets the patience counter");
    check(af_tracker_update(&tracker, 119, 20) == 0, "miss 1 continues");
    check(af_tracker_update(&tracker, 118, 30) == 0, "miss 2 continues");
    check(tracker.best_position == 10, "a miss keeps the best point");
    check(af_tracker_update(&tracker, 117, 40) == 1, "miss 3 stops");
    check(af_tracker_update(&tracker, 117, 50) == 1, "the counter stays full");

    af_tracker_reset(&tracker, 1, 100, 0);
    check(af_tracker_update(&tracker, 100, 10) == 1,
          "patience 1 stops on the first non-improvement");

    af_tracker_reset(&tracker, 3, 0, 0);
    check(af_tracker_update(&tracker, 0, 10) == 0,
          "an equal metric does not beat the best");
    check(tracker.misses == 1, "an equal metric counts as a miss");

    af_tracker_reset(&tracker, 3, 500, 7);
    check(tracker.best_metric == 500 && tracker.best_position == 7,
          "reset keeps the seed sample");
}

static void test_window(void)
{
    int32_t lo, hi;

    af_window(500, 100, 900, 150, &lo, &hi);
    check(lo == 350 && hi == 650, "window centres on the position");

    af_window(200, 100, 900, 150, &lo, &hi);
    check(lo == 100 && hi == 350, "window stops at the low bound");

    af_window(800, 100, 900, 150, &lo, &hi);
    check(lo == 650 && hi == 900, "window stops at the high bound");

    af_window(500, 100, 900, 5000, &lo, &hi);
    check(lo == 100 && hi == 900, "window never exceeds the scale");

    af_window(500, 100, 900, 0, &lo, &hi);
    check(lo == 500 && hi == 500, "span 0 is a single point");

    af_window(500, 100, 900, -10, &lo, &hi);
    check(lo == 500 && hi == 500, "a negative span stays a single point");

    af_window(50, 100, 900, 25, &lo, &hi);
    check(lo == 100 && hi == 125, "a position below the scale is clamped");

    af_window(1000, 100, 900, 25, &lo, &hi);
    check(lo == 875 && hi == 900, "a position above the scale is clamped");

    af_window(500, 100, 900, 400, &lo, &hi);
    check(lo == 100 && hi == 900, "a span as wide as the scale is kept");

    af_window(500, 100, 900, 401, &lo, &hi);
    check(lo == 100 && hi == 900, "a span wider than the scale is clamped");
}

int main(void)
{
    test_direction();
    test_tracker();
    test_window();
    if (failures)
    {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("focus_af_test: ok\n");
    return 0;
}