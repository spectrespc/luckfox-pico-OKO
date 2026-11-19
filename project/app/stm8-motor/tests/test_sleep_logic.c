/*
 * Host-side unit tests for the I2C-idle sleep decision logic.
 *
 * The logic under test lives in src/motor/sleep_logic.h, shared verbatim with
 * the firmware (src/core/main.c includes it), so the tests exercise the real
 * code rather than a copy.
 *
 * Build and run with: make -C tests test
 */

#include <stdint.h>
#include <stdio.h>

#include "sleep_logic.h"   /* the code under test */

static int tests_run = 0;
static int tests_failed = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        tests_run++;                                                     \
        if (!(cond)) {                                                   \
            tests_failed++;                                              \
            printf("FAIL: %s (line %d)\n", #cond, __LINE__);             \
        }                                                                \
    } while (0)

/* A quiet, enabled, clean, idle-satisfied baseline. */
static sleep_conditions base(void)
{
    sleep_conditions c;

    c.last_activity = 1000;
    c.now = 5000;              /* 4000 ms of silence */
    c.idle_ms = 3000;
    c.sleep_enabled = 1;
    c.motor_running = 0;
    c.eeprom_dirty = 0;
    c.i2c_busy = 0;
    return c;
}

static void test_silence_gate(void)
{
    sleep_conditions c = base();

    CHECK(sleep_can_enter(&c));
    CHECK(sleep_next_action(SLEEP_STATE_AWAKE, &c) == SLEEP_ACTION_HALT);

    /* One millisecond short of the idle window: stay awake. */
    c.now = c.last_activity + c.idle_ms - 1;
    CHECK(!sleep_can_enter(&c));
    CHECK(sleep_next_action(SLEEP_STATE_AWAKE, &c) == SLEEP_ACTION_IDLE);

    /* Exactly at the boundary: enter. */
    c.now = c.last_activity + c.idle_ms;
    CHECK(sleep_can_enter(&c));
    CHECK(sleep_next_action(SLEEP_STATE_AWAKE, &c) == SLEEP_ACTION_HALT);

    /* Brand new activity: stay awake. */
    c.now = c.last_activity;
    CHECK(!sleep_can_enter(&c));
    CHECK(sleep_next_action(SLEEP_STATE_AWAKE, &c) == SLEEP_ACTION_IDLE);
}

static void test_running_motors_block_sleep(void)
{
    sleep_conditions c = base();

    c.motor_running = 1;
    CHECK(!sleep_can_enter(&c));
    /* Even with a dirty EEPROM the save must wait: Flash programming stalls
       the TIM4 ISR and would skip DDA steps / nFAULT checks. */
    c.eeprom_dirty = 1;
    CHECK(sleep_next_action(SLEEP_STATE_AWAKE, &c) == SLEEP_ACTION_IDLE);
}

static void test_dirty_eeprom_forces_save_first(void)
{
    sleep_conditions c = base();

    c.eeprom_dirty = 1;
    CHECK(sleep_can_enter(&c));   /* the save does not need the bus idle */
    CHECK(sleep_next_action(SLEEP_STATE_AWAKE, &c) == SLEEP_ACTION_SAVE);
    CHECK(sleep_next_action(SLEEP_STATE_FORCE_SAVE, &c) == SLEEP_ACTION_SAVE);

    /* After a successful save the very next evaluation halts. */
    c.eeprom_dirty = 0;
    CHECK(sleep_next_action(SLEEP_STATE_FORCE_SAVE, &c) == SLEEP_ACTION_HALT);
    CHECK(sleep_next_action(SLEEP_STATE_AWAKE, &c) == SLEEP_ACTION_HALT);
}

static void test_busy_i2c_blocks_sleep(void)
{
    sleep_conditions c = base();

    /* An unfinished transaction or a frame waiting for the main loop must not
       be abandoned by Halt: the caller must service it first. */
    c.i2c_busy = 1;
    CHECK(!sleep_can_enter(&c));
    CHECK(sleep_next_action(SLEEP_STATE_AWAKE, &c) == SLEEP_ACTION_IDLE);
}

static void test_disabled_never_sleeps(void)
{
    sleep_conditions c = base();

    c.sleep_enabled = 0;
    CHECK(!sleep_can_enter(&c));
    CHECK(sleep_next_action(SLEEP_STATE_AWAKE, &c) == SLEEP_ACTION_IDLE);

    c.eeprom_dirty = 1;
    CHECK(sleep_next_action(SLEEP_STATE_AWAKE, &c) == SLEEP_ACTION_IDLE);

    /* Disabled while asleep: the caller must be told to resume. */
    CHECK(sleep_next_action(SLEEP_STATE_ASLEEP, &c) == SLEEP_ACTION_WAKE);
    c.sleep_enabled = 1;
    c.eeprom_dirty = 0;
    CHECK(sleep_next_action(SLEEP_STATE_ASLEEP, &c) == SLEEP_ACTION_IDLE);
}

static void test_tick_wrap(void)
{
    sleep_conditions c = base();

    /* system_ticks is 32-bit and wraps after ~49.7 days.  Unsigned subtraction
       must keep the idle timer correct across the wrap. */
    c.last_activity = 0xFFFFFF00u;
    c.now = 0x00000100u;             /* 512 ms later */
    c.idle_ms = 3000;
    CHECK(!sleep_can_enter(&c));

    c.idle_ms = 512;
    CHECK(sleep_can_enter(&c));
    CHECK(sleep_next_action(SLEEP_STATE_AWAKE, &c) == SLEEP_ACTION_HALT);

    c.idle_ms = 513;
    CHECK(!sleep_can_enter(&c));

    /* 1 ms of silence across the wrap. */
    c.last_activity = 0xFFFFFFFFu;
    c.now = 0x00000000u;
    c.idle_ms = 1;
    CHECK(sleep_can_enter(&c));
    c.idle_ms = 2;
    CHECK(!sleep_can_enter(&c));
}

int main(void)
{
    test_silence_gate();
    test_running_motors_block_sleep();
    test_dirty_eeprom_forces_save_first();
    test_busy_i2c_blocks_sleep();
    test_disabled_never_sleeps();
    test_tick_wrap();

    printf("%d checks, %d failed\n", tests_run, tests_failed);
    if (tests_failed == 0)
        printf("ALL TESTS PASSED\n");
    return tests_failed == 0 ? 0 : 1;
}