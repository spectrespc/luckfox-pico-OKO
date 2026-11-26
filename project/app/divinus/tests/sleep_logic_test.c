/*
 * Host test for the two-tier AOV sleep helpers (sleep_logic.c).
 *
 * Covers the tier-2 period N, the kernel aov_state classifier used to pick the
 * heartbeat path, the self-heal decision, the AOV-profile defaults and the
 * fast-resume strategy matrices.
 *
 * Run from the repository root:
 *   gcc -std=c99 -Wall -Wextra -O2 \
 *       -Iproject/app/divinus/source/src \
 *       project/app/divinus/tests/sleep_logic_test.c \
 *       project/app/divinus/source/src/sleep_logic.c \
 *       -o /tmp/sleep_logic_test && /tmp/sleep_logic_test
 */
#include <stdio.h>
#include <string.h>

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

static void test_fast_resume(void)
{
    const sleep_fast_resume_mode modes[] = {
        SLEEP_FAST_RESUME_EXEC, SLEEP_FAST_RESUME_LIVE,
        SLEEP_FAST_RESUME_STOPSTART, SLEEP_FAST_RESUME_AIQ
    };
    const char *names[] = { "exec", "live", "stopstart", "aiq" };
    const int teardown[] = { 1, 0, 1, 0 };
    const int park[] = { 0, 0, 0, 1 };
    const int restart[] = { 0, 0, 1, 0 };
    size_t i;

    for (i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        sleep_fast_resume_mode parsed = SLEEP_FAST_RESUME_EXEC;

        CHECK(strcmp(sleep_fast_resume_name(modes[i]), names[i]) == 0,
            names[i]);
        CHECK(sleep_fast_resume_from_name(names[i], &parsed) == 0 &&
            parsed == modes[i], "round trip");
        CHECK(sleep_fast_resume_teardown(modes[i]) == teardown[i],
            "teardown matrix");
        CHECK(sleep_fast_resume_park_aiq(modes[i]) == park[i],
            "park_aiq matrix");
        CHECK(sleep_fast_resume_restart_services(modes[i]) == restart[i],
            "restart matrix");
    }

    {
        sleep_fast_resume_mode parsed = SLEEP_FAST_RESUME_LIVE;

        CHECK(sleep_fast_resume_from_name("bogus", &parsed) == -1 &&
            parsed == SLEEP_FAST_RESUME_LIVE, "unknown name rejected");
        CHECK(sleep_fast_resume_from_name(NULL, &parsed) == -1, "NULL name");
        CHECK(sleep_fast_resume_from_name("exec", NULL) == -1, "NULL mode");
        CHECK(sleep_fast_resume_from_name("EXEC", &parsed) == -1,
            "names are case sensitive");
        CHECK(strcmp(sleep_fast_resume_name((sleep_fast_resume_mode)99),
            "exec") == 0, "unknown mode prints exec");
    }

    CHECK(sleep_fast_resume_allowed(SLEEP_FAST_RESUME_AIQ, 1, 1) == 1,
        "aiq allowed with pause+resume");
    CHECK(sleep_fast_resume_allowed(SLEEP_FAST_RESUME_AIQ, 0, 1) == 0,
        "aiq needs pause");
    CHECK(sleep_fast_resume_allowed(SLEEP_FAST_RESUME_AIQ, 1, 0) == 0,
        "aiq needs resume");
    CHECK(sleep_fast_resume_allowed(SLEEP_FAST_RESUME_AIQ, 0, 0) == 0,
        "aiq blocked without symbols");
    CHECK(sleep_fast_resume_allowed(SLEEP_FAST_RESUME_EXEC, 0, 0) == 1,
        "exec always allowed");
    CHECK(sleep_fast_resume_allowed(SLEEP_FAST_RESUME_LIVE, 0, 0) == 1,
        "live always allowed");
    CHECK(sleep_fast_resume_allowed(SLEEP_FAST_RESUME_STOPSTART, 0, 0) == 1,
        "stopstart always allowed");
}

int main(void)
{
    test_fast_resume();

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

    CHECK(sleep_usb_bind_on_wake(1, "motion"), "usb binds on motion wake");
    CHECK(!sleep_usb_bind_on_wake(1, "heartbeat"), "usb stays off on hb");
    CHECK(!sleep_usb_bind_on_wake(1, "timeout"), "usb stays off on timeout");
    CHECK(!sleep_usb_bind_on_wake(1, "quiet"), "usb stays off on quiet");
    CHECK(!sleep_usb_bind_on_wake(0, "motion"), "usb defer disabled");
    CHECK(!sleep_usb_bind_on_wake(1, NULL), "usb NULL state");

    CHECK(sleep_usb_wait_result(1, 1, 0, 2000) == 1, "usb wait bound");
    CHECK(sleep_usb_wait_result(0, 0, 0, 2000) == 1, "usb wait unbound");
    CHECK(sleep_usb_wait_result(1, 0, 0, 2000) == 0,
        "usb wait keeps polling");
    CHECK(sleep_usb_wait_result(1, 0, 1999, 2000) == 0,
        "usb wait before deadline");
    CHECK(sleep_usb_wait_result(1, 0, 2000, 2000) == -1,
        "usb wait deadline");
    CHECK(sleep_usb_wait_result(1, 0, 5000, 2000) == -1,
        "usb wait past deadline");
    CHECK(sleep_usb_wait_result(0, 1, 0, 0) == -1,
        "usb wait disabled checks once");
    CHECK(sleep_usb_wait_result(1, 1, 0, 0) == 1,
        "usb wait disabled but done");
    CHECK(strcmp(sleep_usb_wait_name(1), "done") == 0, "usb name done");
    CHECK(strcmp(sleep_usb_wait_name(0), "pending") == 0,
        "usb name pending");
    CHECK(strcmp(sleep_usb_wait_name(-1), "timeout") == 0,
        "usb name timeout");
    CHECK(strcmp(sleep_usb_wait_name(-2), "error") == 0, "usb name error");
    CHECK(sleep_default_usb_wait_timeout_ms() == 2000,
        "usb default wait 2 s");

    CHECK(sleep_sd_off_gate(1, 0, 0) == SLEEP_SD_GATE_OK, "sd gate ok");
    CHECK(sleep_sd_off_gate(0, 0, 0) == SLEEP_SD_GATE_NOT_MOUNTED,
        "sd gate needs a mounted card");
    CHECK(sleep_sd_off_gate(1, 1, 0) == SLEEP_SD_GATE_CLIP_BUSY,
        "sd gate blocks a busy clip");
    CHECK(sleep_sd_off_gate(1, 0, 1) == SLEEP_SD_GATE_UPDATE,
        "sd gate blocks OTA");
    CHECK(sleep_sd_off_gate(1, 1, 1) == SLEEP_SD_GATE_UPDATE,
        "sd gate prefers OTA over clips");
    CHECK(strcmp(sleep_sd_off_gate_name(SLEEP_SD_GATE_OK), "ok") == 0,
        "sd gate name ok");
    CHECK(strcmp(sleep_sd_off_gate_name(SLEEP_SD_GATE_NOT_MOUNTED),
        "not_mounted") == 0, "sd gate name not mounted");
    CHECK(strcmp(sleep_sd_off_gate_name(SLEEP_SD_GATE_CLIP_BUSY),
        "clip_busy") == 0, "sd gate name clip busy");
    CHECK(strcmp(sleep_sd_off_gate_name(SLEEP_SD_GATE_UPDATE), "update") == 0,
        "sd gate name update");

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