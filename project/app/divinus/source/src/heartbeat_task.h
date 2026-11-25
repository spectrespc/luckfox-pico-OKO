/* heartbeat_task.h — runtime side of the sleep heartbeat snapshot (OKO).
 *
 * heartbeat.h holds the host-testable helpers; this module owns the wake
 * marker, the persisted interval gate/failure streak, the JPEG grab and the
 * HTTP PUT through [http_post].
 */
#ifndef HEARTBEAT_TASK_H
#define HEARTBEAT_TASK_H

#include <stddef.h>

/* Reads the probe marker/env and the persisted gate.  Call BEFORE
 * sleep_policy_init(): that function consumes the marker. */
void heartbeat_init(void);

/* 1 when [sleep] heartbeat is enabled AND a receiver is configured. */
int heartbeat_ready(void);

/* 1 when this process was started after a probe wake (a JPEG was expected
 * from the sleep cycle).  http_post uses it to skip its startup snapshot. */
int heartbeat_wake_start(void);

/* Wake-cycle counter since boot; it survives the exec restart of the wake
 * path and resets on a power cycle. */
unsigned int heartbeat_wake_cycle(void);

/* Interval gate: 1 when this quiet wake cycle should send a heartbeat. */
int heartbeat_due(void);

/* Confirmed motion: rebase the interval gate so the next quiet cycle does not
 * fire a stale heartbeat right after the event. */
void heartbeat_set_due(void);

/* Sends one heartbeat JPEG before the pipeline is torn down.  Returns 0 on a
 * successful PUT, -1 on failure or when the gate says "not now".  Never
 * blocks longer than retries * (3 s connect + 1 s pause). */
int heartbeat_before_suspend(void);
/* Same send, but `force` skips the CLOCK_MONOTONIC interval gate: a tier-2
 * AOV wake exists only to deliver this frame, and the monotonic clock did not
 * advance while the AP was suspended. */
int heartbeat_before_suspend_ex(int force);

/* Nested JSON object for GET /api/sleep. */
void heartbeat_status_json(char *out, size_t size);

/* Flat "heartbeat_ok":true,"heartbeat_fail_streak":N for /api/status. */
void heartbeat_status_fields(char *out, size_t size);

#endif /* HEARTBEAT_TASK_H */