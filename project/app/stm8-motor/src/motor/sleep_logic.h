#ifndef SLEEP_LOGIC_H
#define SLEEP_LOGIC_H

#include <stdint.h>

/* Pure, host-testable decision logic for the I2C-idle sleep mode.
 *
 * This header mirrors the design of src/motor/stepper_logic.h: no registers,
 * no volatile globals and no STM8/SPL dependencies.  src/core/main.c collects
 * the live state into sleep_conditions and uses sleep_can_enter() /
 * sleep_next_action(); tests/test_sleep_logic.c exercises the very same code
 * with a plain gcc build.
 *
 * The decision is expressed as macros on purpose: SDCC 4.2 for STM8 does not
 * inline functions (a `static inline` helper still becomes a real function in
 * the object file), and the application slot is only 5616 bytes.  Macros keep
 * a single source of truth for firmware and host tests without paying for a
 * function call per evaluation.
 *
 * Hardware background (STM8S003F3 datasheet, Table 11):
 *   - I2C address match wakes the core from both Halt and Active-halt;
 *   - the AWU wakes it from Active-halt only;
 *   - TIM4 wakes it from neither.
 * Therefore the sleep is a chain of short Active-halt slices woken by the AWU,
 * each of which reloads the IWDG (IWDG and AWU share the 128 kHz LSI, so the
 * slice/watchdog ratio is independent of the LSI tolerance).  TIM4 must never
 * be used as the wake source. */

/* Sleep phases, matching the reference STM32C011 power FSM shape:
 * AWAKE -> FORCE_SAVE (only while EEPROM is dirty) -> ARMED -> ASLEEP. */
#define SLEEP_STATE_AWAKE       0
#define SLEEP_STATE_FORCE_SAVE  1
#define SLEEP_STATE_ARMED       2
#define SLEEP_STATE_ASLEEP      3

/* Action returned by sleep_next_action(). */
#define SLEEP_ACTION_IDLE       0  /* stay awake, do nothing                */
#define SLEEP_ACTION_SAVE       1  /* persist EEPROM, then re-evaluate      */
#define SLEEP_ACTION_HALT       2  /* conditions met and EEPROM clean: halt */
#define SLEEP_ACTION_WAKE       3  /* sleep was disabled while asleep       */

/* Everything the decision depends on.  All fields are plain values so the
 * same header works both on the STM8 (built from live globals) and on the
 * host tests. */
typedef struct {
    uint32_t last_activity;  /* i2c_last_frame (ms tick snapshot)           */
    uint32_t now;            /* system_ticks snapshot                       */
    uint32_t idle_ms;        /* sleep after this much bus silence           */
    uint8_t  sleep_enabled;  /* host/firmware enable flag                   */
    uint8_t  motor_running;  /* any motor in STATUS_RUNNING                 */
    uint8_t  eeprom_dirty;   /* positions not persisted yet                 */
    uint8_t  i2c_busy;       /* transaction in progress / frame pending     */
} sleep_conditions;

/* Bus silence long enough, sleep enabled, no motion, no transaction pending.
 * The tick difference uses unsigned arithmetic, so a 32-bit system_ticks wrap
 * is handled exactly like i2c_check_timeout() does. */
#define sleep_can_enter(c)                                              \
    ((c)->sleep_enabled && !(c)->motor_running && !(c)->i2c_busy &&     \
     (uint32_t)((c)->now - (c)->last_activity) >= (c)->idle_ms)

/* Decide the next step for the main loop.  `state` is the phase carried from
 * the previous iteration; SLEEP_STATE_ASLEEP is only meaningful immediately
 * after the core woke up (see src/core/main.c). */
#define sleep_next_action(state, c)                                     \
    (((state) == SLEEP_STATE_ASLEEP)                                    \
         ? (sleep_can_enter(c) ? SLEEP_ACTION_IDLE : SLEEP_ACTION_WAKE) \
         : (!sleep_can_enter(c)                                         \
                ? SLEEP_ACTION_IDLE                                     \
                : ((c)->eeprom_dirty ? SLEEP_ACTION_SAVE                \
                                     : SLEEP_ACTION_HALT)))

#endif /* SLEEP_LOGIC_H */