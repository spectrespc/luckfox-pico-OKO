#pragma once

/* AF measurement statistics for the focus assistant.
 *
 * This is the only public header of the rkaiq bridge: vendor types stay inside
 * rk_af_stats.c so the rest of divinus is not coupled to the vendor ABI. */
#include <stdint.h>

#define RK_AF_CELLS 225
#define RK_AF_GRID  15

typedef struct {
    uint32_t frame_id;
    int af_hw_ver;
    uint32_t fv_h1[RK_AF_CELLS];
    uint32_t fv_h2[RK_AF_CELLS];
    uint32_t fv_v1[RK_AF_CELLS];
    uint32_t fv_v2[RK_AF_CELLS];
    uint32_t luma[RK_AF_CELLS];
    uint32_t highlit[RK_AF_CELLS];
} rk_af_map;

int rk_af_stats_init(void);
void rk_af_stats_deinit(void);
int rk_af_stats_available(void);

/* Window in sensor pixels.  Returns 0 on success, -1 on a driver error,
 * -2 when the AIQ context is not ready and -3 for an unsupported AF HW. */
int rk_af_stats_set_window(unsigned h_offs, unsigned v_offs,
                           unsigned h_size, unsigned v_size);

/* Reads one statistics frame.  timeout_ms is passed to get3AStatsBlk.
 * Returns 0, -1 for a general failure, -4 on timeout and -3 on a HW mismatch. */
int rk_af_stats_read(rk_af_map *map, int timeout_ms);

/* Decision-gate fallback: enable the IQ contrast_af flag in the live attrib. */
int rk_af_stats_force_contrast(void);