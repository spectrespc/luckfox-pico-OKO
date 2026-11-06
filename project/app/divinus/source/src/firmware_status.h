#pragma once

#include <stddef.h>

/* Cached firmware versions for the WebUI (STM32 from the oko-ec daemon, STM8
 * from the I2C controller).  The poll thread runs at 0.2 Hz. */

int firmware_status_start(void);
void firmware_status_stop(void);

/* Force an immediate poll (used after a flash job). */
void firmware_status_refresh(void);

/* Returns a malloc()ed JSON object; the caller frees it. */
char *firmware_status_json(void);

/* Cached values for /api/flash targets. */
void firmware_status_stm32(int *present, char *state, size_t state_size,
                           char *version, size_t version_size, long *age);
void firmware_status_stm8(int *present, char *state, size_t state_size,
                          char *version, size_t version_size,
                          char *boot_version, size_t boot_size, long *age);