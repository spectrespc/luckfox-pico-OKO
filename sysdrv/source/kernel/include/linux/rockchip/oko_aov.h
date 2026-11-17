/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * OKO AOV wake proxy state shared between the rockchip-pm sysfs driver
 * (drivers/soc/rockchip/rockchip_pm_config.c) and the RV1106 suspend path
 * (arch/arm/mach-rockchip/rv1106_pm.c).
 *
 * The driver owns the attributes and the user policy; the PM code only reads
 * the enabled flag and publishes the last MCU decision.  Both files are
 * built-in for the AOV profile (CONFIG_RV1106_HPMCU_FAST_WAKEUP=y), which is
 * also the only configuration where this interface exists.
 */

#ifndef __ROCKCHIP_OKO_AOV_H
#define __ROCKCHIP_OKO_AOV_H

#include <linux/compiler.h>
#include <linux/types.h>

enum oko_aov_state {
	OKO_AOV_OFF = 0,
	OKO_AOV_UNSUPPORTED,
	OKO_AOV_ARMED,
	OKO_AOV_QUIET,
	OKO_AOV_MOTION,
	OKO_AOV_TIMEOUT,
};

#ifdef CONFIG_RV1106_HPMCU_FAST_WAKEUP
extern bool oko_aov_enabled;
extern u32 oko_aov_state;

static inline void oko_aov_set_state(enum oko_aov_state state)
{
	WRITE_ONCE(oko_aov_state, state);
}
#else
/* Keep the PM code compilable when the feature is off; the constant folds
 * away and the interface is never exposed to user space.  Do not use a
 * macro named oko_aov_state: it would rename the enum tag in the inline
 * helper below. */
static const bool oko_aov_enabled __maybe_unused = false;

static inline void oko_aov_set_state(enum oko_aov_state state)
{
}
#endif

#endif /* __ROCKCHIP_OKO_AOV_H */