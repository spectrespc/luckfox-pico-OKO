/* sleep_logic.c — pure decisions of the two-tier AOV sleep policy. */

#include "sleep_logic.h"

#include <string.h>

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