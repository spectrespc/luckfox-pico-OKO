#include "focus_home.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "app_config.h"
#include "focus_motor.h"

#define HOME_POLL_MS     50

/* Local wait results. */
#define WAIT_OK        0
#define WAIT_TIMEOUT   1
#define WAIT_IO       -1

static pthread_mutex_t home_lock = PTHREAD_MUTEX_INITIALIZER;
static focus_home_status job;
static int busy;
static int thread_valid;
static pthread_t worker_thread;
static unsigned int pending_mask;
static int32_t pending_value;
static int pending_zero;      /* 0 home, 1 zero */

static const char *motor_name(uint8_t motor)
{
    return motor == 0 ? "focus" : "zoom";
}

static int motor_max_pos(uint8_t motor)
{
    return motor == 0 ? app_config.focus_max_pos
                      : app_config.focus_zoom_max_pos;
}

static uint16_t motor_speed(uint8_t motor)
{
    return (uint16_t)(motor == 0 ? app_config.focus_speed
                                 : app_config.focus_zoom_speed);
}

static void job_set_error(const char *fmt, ...)
{
    va_list args;

    pthread_mutex_lock(&home_lock);
    job.state[0] = '\0';
    snprintf(job.state, sizeof(job.state), "error");
    va_start(args, fmt);
    vsnprintf(job.error, sizeof(job.error), fmt, args);
    va_end(args);
    pthread_mutex_unlock(&home_lock);
}

static void job_add_travel(int steps)
{
    pthread_mutex_lock(&home_lock);
    job.travel += steps;
    pthread_mutex_unlock(&home_lock);
}

static void job_set_state(const char *state, int done)
{
    pthread_mutex_lock(&home_lock);
    snprintf(job.state, sizeof(job.state), "%s", state);
    job.done = done;
    pthread_mutex_unlock(&home_lock);
}

/* Poll GET_STATUS until the motor is no longer RUNNING.  STATUS_ERROR is
 * treated as "stopped" and never aborts a homing run: the nFAULT input of the
 * DRV8833 is no longer wired on the lens, so a latched error is not a usable
 * feedback signal here (the move itself is open-loop). */
static int wait_motion_end(uint8_t motor, int timeout_ms)
{
    int waited = 0;

    while (waited <= timeout_ms)
    {
        uint8_t status = 0xFF;

        if (focus_motor_get_status(motor, &status) != FM_OK)
            return WAIT_IO;
        if (status != 1)
            return WAIT_OK;
        usleep(HOME_POLL_MS * 1000);
        waited += HOME_POLL_MS;
    }
    return WAIT_TIMEOUT;
}

static int zero_motor(uint8_t motor)
{
    int32_t readback = 0;
    uint8_t status = 0xFF;

    if (focus_motor_get_status(motor, &status) != FM_OK)
        return -1;
    if (status == 1)
    {
        job_set_error("%s is moving; stop it before zeroing",
                      motor_name(motor));
        return -1;
    }
    if (focus_motor_set_position(motor, pending_value) != FM_OK)
    {
        job_set_error("%s: cannot send SET_POS (%s)", motor_name(motor),
                      "I2C error");
        return -1;
    }
    usleep(10000);
    if (focus_motor_get_position(motor, &readback) != FM_OK ||
        readback != pending_value)
    {
        job_set_error("%s: firmware does not support SET_POS "
                      "(readback %ld); update to 2.0.2/2.1.1",
                      motor_name(motor), (long)readback);
        return -1;
    }
    return 0;
}

/* HOME always performs the full mechanical travel towards the negative stop,
 * independent of the current position counter and of the driver feedback.
 * Reasons: the counter is just an open-loop step count (negative readbacks can
 * come back with a corrupted top byte, KI-042, and manual moves with the lens
 * removed make the scale meaningless), and the DRV8833 nFAULT input is no
 * longer wired, so its state must not gate or abort the run.  The counter is
 * reset to zero before the move and zeroed again at the stop. */
static int home_motor(uint8_t motor)
{
    int32_t position = 0;
    uint16_t speed = motor_speed(motor);
    int travel = motor_max_pos(motor) + (int)app_config.focus_home_margin;
    int timeout_ms;
    int attempt;
    int rc;

    if (travel <= 0 || travel > 32767 || speed == 0)
    {
        job_set_error("%s: invalid homing configuration (travel %d, speed %u)",
                      motor_name(motor), travel, (unsigned int)speed);
        return -1;
    }

    /* Drop whatever is left from a manual move. */
    focus_motor_stop(motor);
    usleep(20000);

    if (focus_motor_set_speed(motor, speed) != FM_OK)
    {
        job_set_error("%s: cannot set the speed", motor_name(motor));
        return -1;
    }

    /* Reset the logical scale first: the travel below is a fixed step count,
       not a correction of the old counter value. */
    if (focus_motor_set_position(motor, 0) != FM_OK)
    {
        job_set_error("%s: cannot send SET_POS", motor_name(motor));
        return -1;
    }

    if (focus_motor_move(motor, (int16_t)-travel) != FM_OK)
    {
        job_set_error("%s: MOVE failed", motor_name(motor));
        return -1;
    }

    timeout_ms = (int)(((long)travel * 1000L) / (long)speed) + 5000;
    rc = wait_motion_end(motor, timeout_ms);
    if (rc == WAIT_IO)
    {
        job_set_error("%s: I2C error during homing", motor_name(motor));
        return -1;
    }
    if (rc == WAIT_TIMEOUT)
    {
        job_set_error("%s: homing timed out after %d steps",
                      motor_name(motor), travel);
        return -1;
    }
    job_add_travel(travel);

    /* Let the firmware persist the new position to its EEPROM first: that
       write blocks the main loop for a few tens of milliseconds, and a frame
       that arrives meanwhile can be overwritten before it is executed. */
    usleep(50000);

    /* The final SET_POS must stick: the scale has to read 0 at the stop.
       A single frame can be lost on the I2C wake path, so retry and verify. */
    for (attempt = 0; attempt < 3; attempt++)
    {
        if (focus_motor_set_position(motor, 0) != FM_OK)
        {
            job_set_error("%s: cannot send SET_POS", motor_name(motor));
            return -1;
        }
        usleep(20000);
        if (focus_motor_get_position(motor, &position) == FM_OK &&
            position == 0)
            return 0;
    }
    job_set_error("%s: SET_POS(0) did not stick (position %ld); update to "
                  "2.0.2/2.1.1", motor_name(motor), (long)position);
    return -1;
}

static void *home_worker(void *arg)
{
    unsigned int mask;
    int zero;
    uint8_t motor;
    int failed = 0;

    (void)arg;
    pthread_mutex_lock(&home_lock);
    mask = pending_mask;
    zero = pending_zero;
    pthread_mutex_unlock(&home_lock);

    for (motor = 0; motor < 2 && !failed; motor++)
    {
        if (!(mask & (1u << motor)))
            continue;
        if (zero ? zero_motor(motor) : home_motor(motor))
            failed = 1;
    }

    if (!failed)
        job_set_state("done", 1);

    pthread_mutex_lock(&home_lock);
    busy = 0;
    pthread_mutex_unlock(&home_lock);
    return NULL;
}

static int start_job(unsigned int motor_mask, int32_t value, int zero)
{
    const char *name;

    if (!motor_mask || (motor_mask & ~(FOCUS_HOME_MOTOR_FOCUS |
                                       FOCUS_HOME_MOTOR_ZOOM)))
        return -2;
    name = motor_mask == (FOCUS_HOME_MOTOR_FOCUS | FOCUS_HOME_MOTOR_ZOOM)
        ? "both" : (motor_mask & FOCUS_HOME_MOTOR_FOCUS ? "focus" : "zoom");

    pthread_mutex_lock(&home_lock);
    if (busy)
    {
        pthread_mutex_unlock(&home_lock);
        return -1;
    }
    busy = 1;
    pending_mask = motor_mask;
    pending_value = value;
    pending_zero = zero;
    job.state[0] = '\0';
    snprintf(job.state, sizeof(job.state), "running");
    snprintf(job.motor, sizeof(job.motor), "%s", name);
    job.travel = 0;
    job.done = 0;
    job.error[0] = '\0';
    pthread_mutex_unlock(&home_lock);

    if (thread_valid)
    {
        pthread_join(worker_thread, NULL);
        thread_valid = 0;
    }
    if (pthread_create(&worker_thread, NULL, home_worker, NULL) != 0)
    {
        job_set_error("cannot start the homing worker");
        pthread_mutex_lock(&home_lock);
        busy = 0;
        pthread_mutex_unlock(&home_lock);
        return -2;
    }
    thread_valid = 1;
    return 0;
}

int focus_home_init(void)
{
    pthread_mutex_lock(&home_lock);
    memset(&job, 0, sizeof(job));
    snprintf(job.state, sizeof(job.state), "idle");
    busy = 0;
    pthread_mutex_unlock(&home_lock);
    return 0;
}

void focus_home_shutdown(void)
{
    if (thread_valid)
    {
        pthread_join(worker_thread, NULL);
        thread_valid = 0;
    }
}

int focus_home_running(void)
{
    int value;
    pthread_mutex_lock(&home_lock);
    value = busy;
    pthread_mutex_unlock(&home_lock);
    return value;
}

int focus_home_start(unsigned int motor_mask)
{
    return start_job(motor_mask, 0, 0);
}

int focus_zero_start(unsigned int motor_mask, int32_t value)
{
    return start_job(motor_mask, value, 1);
}

void focus_home_status_get(focus_home_status *out)
{
    if (!out)
        return;
    pthread_mutex_lock(&home_lock);
    *out = job;
    pthread_mutex_unlock(&home_lock);
}