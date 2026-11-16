// Copyright 2022 Rockchip Electronics Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
/*
 * OKO: AP-side sleep policy for the wakeup-IPC (thunder boot) line.
 *
 * Behaviour:
 *   - the rockit IVS thread reports every accepted motion detection;
 *   - when no motion was reported for [aov] sleep_timeout_s (default 600 s)
 *     the process stops the stream implicitly by entering system suspend:
 *     PMU_WAKEUP_TIMEOUT_CNT is programmed for [aov] wake_interval_s and
 *     "mem" is written to /sys/power/state;
 *   - the PMU timeout wakes the AP; after resume the process runs a
 *     wake_check_s motion-detection window.  Motion keeps the AP awake and
 *     restarts the idle timer, no motion sends it back to suspend.
 *
 * The MCU is NOT a wake proxy in this image: the thunder-boot firmware parks
 * after handing the ISP over, so the periodic PMU timeout is the only wake
 * source (see [aov] in rkipc-300w.ini and the kernel fragment
 * rv1106-pmu-timeout.config).
 *
 * /sys/power/state semantics: the write blocks until the system resumes, so
 * the code after the write is the resume path.
 */
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "aov_sleep.h"
#include "common.h"
#include "param.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "aov_sleep"

#define PMU_PHYS_BASE 0xff300000UL
#define PMU_MAP_SIZE 0x1000UL
#define PMU_WAKEUP_TIMEOUT_CNT_OFF 0x048UL

#define DEFAULT_SLEEP_TIMEOUT_S 600
#define DEFAULT_WAKE_INTERVAL_S 10
#define DEFAULT_WAKE_CHECK_S 3
#define DEFAULT_PMU_TIMEOUT_HZ 32

static pthread_t aov_thread_id;
static int aov_thread_run;
static int aov_started;

/* Monotonic milliseconds; the policy must not depend on wall clock jumps. */
static int64_t aov_now_ms(void) {
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static volatile int aov_motion_seq;

void rkipc_aov_sleep_report_motion(void) {
	/* Picked up by the policy thread on its next tick.  A plain int store is
	 * enough: a lost update only delays the decision by one second. */
	aov_motion_seq++;
}

static int aov_pmu_open(void **map) {
	int fd;
	void *regs;

	fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (fd < 0) {
		LOG_ERROR("open /dev/mem failed: %s\n", strerror(errno));
		return -1;
	}
	regs = mmap(NULL, PMU_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
	            PMU_PHYS_BASE);
	if (regs == MAP_FAILED) {
		LOG_ERROR("mmap PMU failed: %s\n", strerror(errno));
		close(fd);
		return -1;
	}
	*map = regs;
	return fd;
}

static int aov_pmu_set_timeout(void *regs, int wake_interval_s, int pmu_hz) {
	volatile uint32_t *cnt;
	uint32_t value;

	if (!regs)
		return -1;
	cnt = (volatile uint32_t *)((char *)regs + PMU_WAKEUP_TIMEOUT_CNT_OFF);
	value = (uint32_t)wake_interval_s * (uint32_t)pmu_hz;
	*cnt = value;
	LOG_INFO("PMU wakeup timeout set to %u (%d s @ %d Hz)\n", value,
	         wake_interval_s, pmu_hz);
	return 0;
}

static int aov_system_suspend(void) {
	int fd;
	ssize_t ret;

	/* Push pending filesystem state before the AP stops. */
	sync();

	fd = open("/sys/power/state", O_WRONLY);
	if (fd < 0) {
		LOG_ERROR("open /sys/power/state failed: %s\n", strerror(errno));
		return -1;
	}
	ret = write(fd, "mem", 3);
	if (ret != 3)
		LOG_ERROR("write mem failed: %s\n", strerror(errno));
	close(fd);
	return ret == 3 ? 0 : -1;
}

static void *aov_policy_thread(void *arg) {
	int64_t now, last_motion_ms, wake_deadline_ms = 0;
	int motion_seen = aov_motion_seq;
	int check_window = 0;
	int suspend_failures = 0;
	int sleep_timeout_s = rk_param_get_int("aov:sleep_timeout_s", DEFAULT_SLEEP_TIMEOUT_S);
	int wake_interval_s = rk_param_get_int("aov:wake_interval_s", DEFAULT_WAKE_INTERVAL_S);
	int wake_check_s = rk_param_get_int("aov:wake_check_s", DEFAULT_WAKE_CHECK_S);
	int pmu_hz = rk_param_get_int("aov:pmu_timeout_hz", DEFAULT_PMU_TIMEOUT_HZ);
	void *pmu_regs = NULL;
	int pmu_fd = -1;

	LOG_INFO("policy: sleep %d s, wake interval %d s, check window %d s, pmu %d Hz\n",
	         sleep_timeout_s, wake_interval_s, wake_check_s, pmu_hz);

	if (sleep_timeout_s <= 0 || wake_interval_s <= 0 || wake_check_s < 0 || pmu_hz <= 0) {
		LOG_ERROR("policy: invalid [aov] parameters, thread stopped\n");
		return NULL;
	}

	pmu_fd = aov_pmu_open(&pmu_regs);
	if (pmu_fd < 0)
		LOG_ERROR("policy: PMU timeout wake is unavailable, suspend disabled\n");

	last_motion_ms = aov_now_ms();

	while (aov_thread_run) {
		sleep(1);
		now = aov_now_ms();

		if (aov_motion_seq != motion_seen) {
			motion_seen = aov_motion_seq;
			last_motion_ms = now;
			if (check_window) {
				LOG_INFO("policy: motion after resume, keep streaming\n");
				check_window = 0;
			}
		}

		if (check_window) {
			if (now < wake_deadline_ms)
				continue;
			check_window = 0;
			LOG_INFO("policy: no motion in wake window, suspend again\n");
		} else if (now - last_motion_ms < (int64_t)sleep_timeout_s * 1000) {
			continue;
		} else {
			LOG_INFO("policy: no motion for %lld s, suspend\n",
			         (long long)((now - last_motion_ms) / 1000));
		}

		if (pmu_fd < 0) {
			LOG_ERROR("policy: cannot suspend without a wake source\n");
			sleep(sleep_timeout_s > 60 ? 60 : sleep_timeout_s);
			last_motion_ms = aov_now_ms();
			continue;
		}

		aov_pmu_set_timeout(pmu_regs, wake_interval_s, pmu_hz);

		if (aov_system_suspend() < 0) {
			suspend_failures++;
			/* Do not hammer /sys/power/state if suspend is rejected. */
			LOG_ERROR("policy: suspend failed (%d), retry in 60 s\n",
			          suspend_failures);
			sleep(60);
			last_motion_ms = aov_now_ms();
			continue;
		}
		suspend_failures = 0;

		/* Resumed.  Give the pipeline wake_check_s to deliver IVS results,
		 * then decide whether to go back to sleep. */
		now = aov_now_ms();
		LOG_INFO("policy: resumed, %d s motion-detection window\n", wake_check_s);
		if (wake_check_s == 0) {
			last_motion_ms = now;
			continue;
		}
		/* Drop motion reports queued before the suspend entry: they belong
		 * to the frames that triggered the previous decision. */
		motion_seen = aov_motion_seq;
		last_motion_ms = now;
		check_window = 1;
		wake_deadline_ms = now + (int64_t)wake_check_s * 1000;
	}

	if (pmu_regs)
		munmap(pmu_regs, PMU_MAP_SIZE);
	if (pmu_fd >= 0)
		close(pmu_fd);

	return NULL;
}

int rkipc_aov_sleep_init(void) {
	if (aov_started)
		return 0;
	if (!rk_param_get_int("aov:enable", 0)) {
		LOG_INFO("sleep policy disabled\n");
		return 0;
	}

	aov_thread_run = 1;
	if (pthread_create(&aov_thread_id, NULL, aov_policy_thread, NULL)) {
		LOG_ERROR("create policy thread failed\n");
		aov_thread_run = 0;
		return -1;
	}
	aov_started = 1;
	return 0;
}

void rkipc_aov_sleep_deinit(void) {
	if (!aov_started)
		return;

	aov_thread_run = 0;
	pthread_join(aov_thread_id, NULL);
	aov_started = 0;
}