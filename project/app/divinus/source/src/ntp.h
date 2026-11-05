#pragma once

#include <stdbool.h>

/* OKO: NTP control for the WebUI.
 *
 * The image ships the reference ntpd and starts it from /etc/init.d/S49ntp
 * with the distribution configuration.  divinus only takes the daemon over
 * when a custom server is configured or when NTP is switched off, so the
 * default configuration keeps the vendor daemon untouched. */

#define NTP_BIN "/usr/sbin/ntpd"

bool ntp_tool_available(void);
bool ntp_running(void);

/* Applies app_config.ntp_enable / app_config.ntp_server to the running
 * daemon.  Returns 0 on success, -1 when ntpd cannot be started. */
int ntp_apply(void);

/* Startup hook: honours a disabled NTP or a custom server and otherwise
 * leaves the distribution daemon alone. */
void ntp_apply_startup(void);

/* Schedules ntp_apply_startup() a few seconds after boot: RkLunch starts
 * divinus before /etc/init.d/S49ntp, so applying immediately would race the
 * vendor daemon.  No thread is started in the default configuration. */
void ntp_startup_schedule(void);