#pragma once

/* Boot-stage marker for the start-up budget.
 *
 * Writes one line to /dev/kmsg ("divinus-boot: <stage> dt=<us>"), so the
 * kernel stamps it with seconds since boot and the line shows up both in
 * dmesg and on the UART console.  The delta is measured with CLOCK_MONOTONIC
 * between consecutive calls and makes the per-stage cost visible without
 * extra tooling.  Missing /dev/kmsg is not an error: the markers are simply
 * absent and the camera starts as usual. */
void boot_mark(const char *stage);