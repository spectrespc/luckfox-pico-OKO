#pragma once

#include "types.h"

#ifndef GIT_REV
#define GIT_REV "unknown"
#endif

extern volatile char graceful, keepRunning;
extern char audioOn;

extern void *aud_thread;
extern void *isp_thread;
extern void *vid_thread;

extern char chnCount;
extern hal_chnstate *chnState;

extern char chip[16];
extern char family[32];
extern hal_platform plat;
extern char sensor[16];
extern int series;

/* Runtime capabilities discovered while loading the vendor libraries. */
#define HAL_CAP_EPTZ        (1u << 0)
#define HAL_CAP_ROTATE      (1u << 1)
#define HAL_CAP_VENC_EXTRA  (1u << 2)
#define HAL_CAP_AIISP       (1u << 3)
#define HAL_CAP_IVS         (1u << 4)
#define HAL_CAP_IQ          (1u << 5)
#define HAL_CAP_HDR_DYN     (1u << 6)
#define HAL_CAP_OSD_MULTI   (1u << 7)
#define HAL_CAP_RKNN        (1u << 8)
#define HAL_CAP_FOCUS_STATS (1u << 9)
extern unsigned int halCapabilities;
