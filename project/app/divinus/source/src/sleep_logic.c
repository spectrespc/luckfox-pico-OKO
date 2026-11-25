/* sleep_logic.c — pure decisions of the two-tier AOV sleep policy. */

#include "sleep_logic.h"

#include <string.h>

const char *sleep_fast_resume_name(sleep_fast_resume_mode mode)
{
    switch (mode)
    {
    case SLEEP_FAST_RESUME_LIVE:
        return "live";
    case SLEEP_FAST_RESUME_STOPSTART:
        return "stopstart";
    case SLEEP_FAST_RESUME_AIQ:
        return "aiq";
    default:
        return "exec";
    }
}

int sleep_fast_resume_from_name(const char *name, sleep_fast_resume_mode *mode)
{
    sleep_fast_resume_mode value;

    if (!name || !mode)
    {
        return -1;
    }
    if (strcmp(name, "exec") == 0)
    {
        value = SLEEP_FAST_RESUME_EXEC;
    }
    else if (strcmp(name, "live") == 0)
    {
        value = SLEEP_FAST_RESUME_LIVE;
    }
    else if (strcmp(name, "stopstart") == 0)
    {
        value = SLEEP_FAST_RESUME_STOPSTART;
    }
    else if (strcmp(name, "aiq") == 0)
    {
        value = SLEEP_FAST_RESUME_AIQ;
    }
    else
    {
        return -1;
    }
    *mode = value;
    return 0;
}

int sleep_fast_resume_teardown(sleep_fast_resume_mode mode)
{
    /* The live and aiq strategies keep the vendor pipeline up across mem. */
    return mode == SLEEP_FAST_RESUME_EXEC ||
           mode == SLEEP_FAST_RESUME_STOPSTART;
}

int sleep_fast_resume_park_aiq(sleep_fast_resume_mode mode)
{
    return mode == SLEEP_FAST_RESUME_AIQ;
}

int sleep_fast_resume_restart_services(sleep_fast_resume_mode mode)
{
    /* Only stopstart re-runs the service init in the same process. */
    return mode == SLEEP_FAST_RESUME_STOPSTART;
}

int sleep_fast_resume_allowed(sleep_fast_resume_mode mode,
                              int pause_available, int resume_available)
{
    if (mode == SLEEP_FAST_RESUME_AIQ)
    {
        return pause_available && resume_available;
    }
    return 1;
}

unsigned int sleep_heartbeat_wakes(unsigned int heartbeat_s,
                                   unsigned int wake_s)
{
    unsigned int wakes;

    if (wake_s == 0)
    {
        return 0;
    }
    /* heartbeat=true with interval 0 keeps the old "due every cycle" meaning
     * (see heartbeat_interval_due); in the two-tier scheme that is N=1. */
    if (heartbeat_s == 0)
    {
        return 1;
    }
    wakes = heartbeat_s / wake_s;
    return wakes ? wakes : 1;
}

int sleep_aov_state_is_heartbeat(const char *state)
{
    return state && strcmp(state, "heartbeat") == 0;
}

int sleep_aov_state_is_timeout(const char *state)
{
    return state && strcmp(state, "timeout") == 0;
}

int sleep_aov_state_needs_heal(const char *state)
{
    return sleep_aov_state_is_timeout(state);
}

int sleep_usb_bind_on_wake(int usb_on_motion, const char *aov_state)
{
    if (!usb_on_motion || !aov_state)
    {
        return 0;
    }
    return strcmp(aov_state, "motion") == 0;
}

unsigned int sleep_default_wake_interval(int aov_available)
{
    /* The product profile keeps its historical 10 s cadence; the AOV profile
     * samples the MCU every 2 s. */
    return aov_available ? 2 : 10;
}

unsigned int sleep_default_heartbeat_interval(int aov_available)
{
    return aov_available ? 300 : 0;
}