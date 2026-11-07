#pragma once

/* Focus/zoom motor client for the STM8S103F3P6 on /dev/i2c-3 (0x60).
 *
 * All I2C access is serialised with an internal mutex and an advisory flock on
 * /run/oko-motor.lock.  The flash worker suspends this module while oko-motor
 * owns the bus. */
#include <stdint.h>

#define FM_OK              0
#define FM_ERR_IO         -1
#define FM_ERR_NO_DEVICE  -2
#define FM_ERR_SUSPENDED  -3
#define FM_ERR_BUSY       -4
#define FM_ERR_PARAM      -5

typedef struct {
    int enabled;            /* focus.motor in the configuration */
    int present;
    int bootloader;         /* the application requested an update */
    int has_app_version;
    uint8_t app_version[3];
    int has_boot_version;
    uint8_t boot_version[3];
    int status;             /* last motor status byte, -1 if unknown */
    int has_position;
    int32_t position;
    int zoom_status;        /* zoom status byte, -1 if unknown */
    int has_zoom_position;
    int32_t zoom_position;
} fm_snapshot;

int focus_motor_init(const char *device, unsigned int address);
void focus_motor_shutdown(void);
void focus_motor_set_enabled(int enabled);
int focus_motor_enabled(void);

/* Probe the controller and refresh the cached snapshot. */
int focus_motor_probe(void);
/* Copy the cached snapshot. */
void focus_motor_snapshot(fm_snapshot *out);

int focus_motor_move(uint8_t motor, int16_t steps);
int focus_motor_set_position(uint8_t motor, int32_t position);
int focus_motor_stop(uint8_t motor);
int focus_motor_stop_all(void);
int focus_motor_set_speed(uint8_t motor, uint16_t speed);
int focus_motor_get_position(uint8_t motor, int32_t *position);
int focus_motor_get_status(uint8_t motor, uint8_t *status);
int focus_motor_reset(void);

int focus_motor_read_app_version(uint8_t version[3]);
int focus_motor_read_boot_info(uint8_t info[8]);

void focus_motor_suspend(int on);
int focus_motor_suspended(void);
/* True when another process (oko-motor / flash) owns the lock file. */
int focus_motor_busy(void);

const char *focus_motor_error(int code);