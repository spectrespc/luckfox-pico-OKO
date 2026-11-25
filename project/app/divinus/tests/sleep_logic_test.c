/*
 * Host test for the two-tier AOV sleep helpers (sleep_logic.c).
 *
 * Covers the tier-2 period N, the kernel aov_state classifier used to pick the
 * heartbeat path, the self-heal decision and the AOV-profile defaults.
 *
 * Run from the repository root:
 *   gcc -std=c99 -Wall -Wextra -O2 \
 *       -Iproject/app/divinus/source/src \
 *       project/app/divinus/tests/sleep_logic_test.c \
 *       project/app/divinus/source/src/sleep_logic.c \
 *       -o /tmp/sleep_logic_test && /tmp/sleep_logic_test
 */
#include <stdio.h>

#include "sleep_logic.h"

static int failures;

#define CHECK(cond, name)                       \
    do {                                        \
        if (cond) {                             \
            printf("ok %s\n", name);            \
        } else {                                \
            printf("FAIL %s\n", name);          \
            failures++;                         \
        }                                       \
    } while (0)

int main(void)
{
    CHECK(sleep_heartbeat_wakes(300, 2) == 150, "N 300/2");
    CHECK(sleep_heartbeat_wakes(5, 2) == 2, "N 5/2");
    CHECK(sleep_heartbeat_wakes(2, 2) == 1, "N 2/2");
    CHECK(sleep_heartbeat_wakes(1, 60) == 1, "N shorter than a cycle");
    CHECK(sleep_heartbeat_wakes(0, 2) == 1, "N interval 0 every cycle");
    CHECK(sleep_heartbeat_wakes(300, 0) == 0, "N no wake interval");

    CHECK(sleep_aov_state_is_heartbeat("heartbeat"), "state heartbeat");
    CHECK(!sleep_aov_state_is_heartbeat("quiet"), "state quiet is not hb");
    CHECK(!sleep_aov_state_is_heartbeat(NULL), "state NULL is not hb");
    CHECK(sleep_aov_state_is_timeout("timeout"), "state timeout");
    CHECK(!sleep_aov_state_is_timeout("motion"), "state motion is not timeout");
    CHECK(sleep_aov_state_needs_heal("timeout"), "heal on timeout");
    CHECK(!sleep_aov_state_needs_heal("heartbeat"), "no heal on heartbeat");
    CHECK(!sleep_aov_state_needs_heal("quiet"), "no heal on quiet");

    CHECK(sleep_default_wake_interval(1) == 2, "aov default interval 2 s");
    CHECK(sleep_default_wake_interval(0) == 10, "product default interval");
    CHECK(sleep_default_heartbeat_interval(1) == 300, "aov default heartbeat");
    CHECK(sleep_default_heartbeat_interval(0) == 0, "product no heartbeat");

    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("all sleep logic helpers passed\n");
    return 0;
}