// Copyright 2022 Rockchip Electronics Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef __AOV_SLEEP_H__
#define __AOV_SLEEP_H__

/* Report one accepted hardware motion detection to the sleep policy. */
void rkipc_aov_sleep_report_motion(void);

/* Start/stop the AP-side "sleep when idle" policy thread. */
int rkipc_aov_sleep_init(void);
void rkipc_aov_sleep_deinit(void);

#endif