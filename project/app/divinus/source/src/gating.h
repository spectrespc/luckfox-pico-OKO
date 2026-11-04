#pragma once

#include <stdbool.h>

/* Feature gating: a combination that has not been verified on the target
 * hardware, or that the runtime cannot support, is refused with a reason the
 * web UI can show.  The table lives in code, not in the configuration. */
typedef struct {
    bool ok;
    const char *reason;
    const char *hint;
} gating_result;

/* feature: "eptz", "rotate", "venc", "substream", "aiisp", "ivs", "iq",
 *          "hdr", "osd_all", "record" */
gating_result gating_check(const char *feature);

/* True when /mnt/sdcard is a real mount point (not the oem symlink). */
bool gating_sd_mounted(void);
/* Fills capacity/free in bytes; returns false when the card is not mounted. */
bool gating_sd_space(unsigned long long *total, unsigned long long *freeBytes);