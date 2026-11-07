#pragma once

/* Background homing / position-zero jobs for the STM8 focus and zoom motors.
 *
 * Homing is a long operation (thousands of steps at ~300 steps/s), so the HTTP
 * thread only starts a worker and reports its state; the WebUI polls
 * GET /api/focus like it does for the AF statistics.  The worker shares the
 * focus_motor transport, which serialises every I2C transaction on its own. */
#include <stdint.h>

/* Bit mask for the target motors. */
#define FOCUS_HOME_MOTOR_FOCUS (1u << 0)
#define FOCUS_HOME_MOTOR_ZOOM  (1u << 1)

typedef struct {
    char state[12];   /* idle | running | done | error */
    char motor[8];    /* focus | zoom | both               */
    int travel;       /* steps moved by the current/last job */
    int done;         /* 1 when the last job completed      */
    char error[128];  /* non-empty when state == error      */
} focus_home_status;

int focus_home_init(void);
void focus_home_shutdown(void);

/* 1 while a job is running. */
int focus_home_running(void);

/* Starts a homing job: drive towards the negative stop, then SET_POS(0).
 * Returns 0 on success, -1 when a job is already running, -2 when the worker
 * could not be started. */
int focus_home_start(unsigned int motor_mask);

/* Starts a zeroing job: SET_POS(value) without moving.  Same return codes. */
int focus_zero_start(unsigned int motor_mask, int32_t value);

void focus_home_status_get(focus_home_status *out);