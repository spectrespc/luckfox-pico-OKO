#ifndef MAIN_H
#define MAIN_H

#include <stdint.h>
#include "stepper.h"

#define I2C_SLAVE_ADDR      0x60
#define MAX_STEP_SPEED      5000
#define I2C_TIMEOUT_MS    2000

/* I2C-idle sleep mode (src/motor/sleep_logic.h, src/hal/hw_config.c).
 *
 * The idle state is Wait mode (WFI): the CPU clock is gated until the next
 * interrupt, while fMASTER keeps running so TIM4 (1 ms), the IWDG and the I2C
 * stay alive.  Active-halt was the original design, but the AWU of the bench
 * STM8S003F3P6 never counts, so HALT was only ever exited by the IWDG reset
 * (bench finding 2026-11-21, see hw_config.c).
 *
 * SLEEP_WAKE_MS is the wake granularity: one TIM4 tick.
 *
 * BOARD=test enables the feature by default; the production build keeps it
 * off and the host turns it on explicitly (oko-motor sleep on). */
#ifndef SLEEP_DEFAULT_ON
#define SLEEP_DEFAULT_ON 0
#endif
#define SLEEP_IDLE_MS        3000
#define SLEEP_IDLE_DEFAULT_S 3
#define SLEEP_IDLE_MAX_S     60
#define SLEEP_WAKE_MS        1      /* one TIM4 tick */

/* Application version reported by I2C command 0xFF and written into the
 * application header by tools/mkapp.py.  Bump it on every user-visible
 * firmware change; the WebUI shows these three bytes.
 *
 * The #ifndef guards let the Makefile override the version for a BOARD
 * variant (-DAPP_VERSION_MINOR=3 for BOARD=test) without a redefinition
 * error; the default build keeps the values below. */
#ifndef APP_VERSION_MAJOR
#define APP_VERSION_MAJOR   2
#endif
#ifndef APP_VERSION_MINOR
#define APP_VERSION_MINOR   2
#endif
#ifndef APP_VERSION_PATCH
#define APP_VERSION_PATCH   0
#endif

/* Shared with ISRs: must be volatile (and read atomically where 32-bit). */
extern volatile uint32_t system_ticks;
extern volatile uint32_t i2c_last_activity;
/* Last real bus activity (address/data/stop).  Unlike i2c_last_activity this
 * is NOT re-armed by i2c_slave_reset(), so the idle sleep timer keeps running
 * while the 2 s self-reset keeps re-initialising an idle peripheral. */
extern volatile uint32_t i2c_last_frame;
extern volatile uint8_t i2c_need_reset;
extern volatile uint8_t i2c_cmd_ready;
extern volatile uint8_t eeprom_dirty;
extern uint32_t eeprom_last_save_time;

/* Sleep state and diagnostic counters (GET_SLEEP 0x0A). */
extern volatile uint8_t sleep_enabled;
extern volatile uint16_t sleep_idle_ms;
extern volatile uint8_t g_sleep_state;
extern volatile uint16_t g_sleep_entries;
extern volatile uint8_t g_wake_by_i2c;
extern volatile uint8_t g_wake_by_timer;

void hw_clock_init(void);
void hw_gpio_init(void);
void hw_iwdg_init(void);
void hw_tim4_init(void);
void hw_i2c_init(void);
void hw_sleep_enter(void);
void hw_reset(void);

void i2c_process_command(void);
void i2c_slave_reset(void);
uint8_t i2c_check_timeout(void);
uint8_t i2c_is_busy(void);
void i2c_isr_handler(void);

void eeprom_init(void);
void eeprom_periodic_save(void);
uint8_t eeprom_force_save(void);

void stepper_init(void);
void stepper_move(uint8_t motor, int16_t steps);
void stepper_set_position(uint8_t motor, int32_t position);
void stepper_stop(uint8_t motor);
void stepper_set_speed(uint8_t motor, uint16_t speed);
void stepper_tick(void);

#endif