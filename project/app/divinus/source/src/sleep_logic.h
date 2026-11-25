/* sleep_logic.h — deterministic helpers of the two-tier AOV sleep policy.
 *
 * The policy itself needs the HAL, sysfs and the media pipeline; everything
 * that can be decided from plain numbers and the kernel's aov_state string
 * lives here so it can be unit-tested on the host
 * (tests/sleep_logic_test.c).
 */

#ifndef SLEEP_LOGIC_H
#define SLEEP_LOGIC_H

/* Sleep resume strategies of [sleep] fast_resume.  exec is the product
 * default (teardown + execvp); the others are AOV-profile experiments. */
typedef enum {
    SLEEP_FAST_RESUME_EXEC = 0,  /* teardown + execvp (current design) */
    SLEEP_FAST_RESUME_LIVE,      /* no teardown; live CSI/ISP across mem */
    SLEEP_FAST_RESUME_STOPSTART, /* teardown + exec, then re-init in-process */
    SLEEP_FAST_RESUME_AIQ        /* vendor park: pause AIQ, drain VI, resume */
} sleep_fast_resume_mode;

/* YAML/API spelling.  from_name() returns 0 on success, -1 on an unknown
 * value (the caller keeps the current/default mode). */
const char *sleep_fast_resume_name(sleep_fast_resume_mode mode);
int sleep_fast_resume_from_name(const char *name, sleep_fast_resume_mode *mode);

/* Strategy decisions, kept pure for tests/sleep_logic_test.c. */
int sleep_fast_resume_teardown(sleep_fast_resume_mode mode);
int sleep_fast_resume_park_aiq(sleep_fast_resume_mode mode);
int sleep_fast_resume_restart_services(sleep_fast_resume_mode mode);
/* 1 when the requested mode may run: aiq needs both pause and resume symbols;
 * every other mode is always allowed. */
int sleep_fast_resume_allowed(sleep_fast_resume_mode mode,
                              int pause_available, int resume_available);

/* N in "one userspace wake every N quiet cycles".  0 means "never wake
 * userspace from a quiet cycle" (the pre-two-tier behaviour) and is returned
 * when either interval is unusable. */
unsigned int sleep_heartbeat_wakes(unsigned int heartbeat_s,
                                   unsigned int wake_s);

/* Kernel states published in /sys/.../aov_state. */
int sleep_aov_state_is_heartbeat(const char *state);
int sleep_aov_state_is_timeout(const char *state);

/* The kernel only returns to Linux on a mailbox timeout after
 * OKO_AOV_TIMEOUT_LIMIT timeouts in a row, so a resumed "timeout" means the
 * MCU is unusable and AOV must be disarmed for this boot. */
int sleep_aov_state_needs_heal(const char *state);

/* Whether a full resume must bring the USB host back.  With
 * `[sleep] usb_on_motion` the host is unbound before mem and only a motion
 * wake needs it (WiFi/streaming); heartbeat and timeout wakes just probe and
 * sleep again.  Without AOV the wake is decided later by report_motion(). */
int sleep_usb_bind_on_wake(int usb_on_motion, const char *aov_state);

/* AOV-profile defaults applied only when the key is absent from the YAML. */
unsigned int sleep_default_wake_interval(int aov_available);
unsigned int sleep_default_heartbeat_interval(int aov_available);

#endif /* SLEEP_LOGIC_H */