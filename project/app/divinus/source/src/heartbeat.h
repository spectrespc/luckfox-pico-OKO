/* heartbeat.h — pure helpers of the sleep heartbeat snapshot (OKO).
 *
 * The heartbeat is one JPEG with proof-of-life headers sent at the end of a
 * quiet wake window, before the camera suspends again.  This translation
 * unit has no HAL, media or network dependencies so the URL derivation, the
 * header block, the interval gate and the failure streak run in a host test
 * (tests/heartbeat_test.c).
 */
#ifndef HEARTBEAT_H
#define HEARTBEAT_H

#include <stddef.h>

/* Derives the heartbeat endpoint from the configured frame URL: the last
 * path segment "frame" becomes "heartbeat" (/ingest/frame ->
 * /ingest/heartbeat).  A URL without that segment gets "/heartbeat"
 * appended.  The input is the raw config value: strftime expansion must run
 * on the result, not before it. */
void heartbeat_derive_url(char *out, size_t size, const char *frame_url);

/* Builds the CRLF-terminated extra header block for the PUT:
 *
 *   X-Heartbeat: 1
 *   X-Heartbeat-Ts: <epoch ms>
 *   X-OKO-Idle-S: <motion-free seconds of this cycle>
 *   X-OKO-Motion-Area: <permille, -1 when IVS is off>
 *   X-OKO-Sleep: quiet
 *   X-OKO-Wake-Cycle: <counter since boot>
 *
 * Returns the number of bytes written, excluding the terminating NUL. */
size_t heartbeat_build_headers(char *out, size_t size,
    unsigned long long ts_ms, unsigned int idle_s, int area_permille,
    unsigned int wake_cycle);

/* Interval gate.  gate_s is the monotonic second of the previous send or
 * gate rebase (0 = never); interval_s 0 means "every quiet cycle". */
int heartbeat_interval_due(long now_s, long gate_s, unsigned int interval_s);

/* Failure streak: a successful send resets it, a failed one increments it. */
int heartbeat_streak_next(int streak, int ok);

#endif /* HEARTBEAT_H */