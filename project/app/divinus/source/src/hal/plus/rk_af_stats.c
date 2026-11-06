/*
 * rk_af_stats.c - bridge from divinus to the rkaiq AF measurement engine.
 *
 * This is deliberately the only translation unit that includes the vendor
 * rkaiq headers; the rest of divinus sees the plain rk_af_map structure.  The
 * symbols are resolved with dlsym so a librkaiq build without AF still lets
 * the application start (the capability flag stays clear).
 *
 * The manual measurement configuration follows the RV1106 example in
 * media/isp/.../rkisp_demo/demo/af_algo_demo/third_party_af_algo.cpp.
 */
#if defined(__ARM_PCS_VFP)

#include "rk_af_stats.h"

#include <dlfcn.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "rk_aiq_user_api2_af.h"
#include "rk_aiq_user_api2_sysctl.h"

#include "../globals.h"

/* Defined in rk_hal.c; no vendor header declares it. */
extern void *_rk_aiq_ctx;

typedef int (*af_get_attrib_fn)(const rk_aiq_sys_ctx_t *,
                                rk_aiq_af_attrib_t *);
typedef int (*af_set_attrib_fn)(const rk_aiq_sys_ctx_t *,
                                rk_aiq_af_attrib_t *);
typedef XCamReturn (*get_stats_fn)(const rk_aiq_sys_ctx_t *,
                                   rk_aiq_isp_stats_t **, int);
typedef void (*release_stats_fn)(const rk_aiq_sys_ctx_t *,
                                 rk_aiq_isp_stats_t *);

static void *af_handle;
static af_get_attrib_fn af_get_attrib;
static af_set_attrib_fn af_set_attrib;
static get_stats_fn af_get_stats;
static release_stats_fn af_release_stats;

static const uint16_t gamma_y[RKAIQ_RAWAF_GAMMA_NUM] = {
    0, 45, 108, 179, 245, 344, 409, 459, 500, 567, 622, 676, 759, 833, 896,
    962, 1023
};

int rk_af_stats_init(void)
{
    if (af_handle)
        return 0;

    af_handle = dlopen("librkaiq.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!af_handle)
        return -1;

    af_get_attrib = (af_get_attrib_fn)dlsym(af_handle,
        "rk_aiq_user_api2_af_GetAttrib");
    af_set_attrib = (af_set_attrib_fn)dlsym(af_handle,
        "rk_aiq_user_api2_af_SetAttrib");
    af_get_stats = (get_stats_fn)dlsym(af_handle,
        "rk_aiq_uapi2_sysctl_get3AStatsBlk");
    af_release_stats = (release_stats_fn)dlsym(af_handle,
        "rk_aiq_uapi2_sysctl_release3AStatsRef");

    if (!af_get_attrib || !af_set_attrib || !af_get_stats || !af_release_stats)
    {
        dlclose(af_handle);
        af_handle = NULL;
        return -1;
    }
    return 0;
}

void rk_af_stats_deinit(void)
{
    if (af_handle)
        dlclose(af_handle);
    af_handle = NULL;
    af_get_attrib = NULL;
    af_set_attrib = NULL;
    af_get_stats = NULL;
    af_release_stats = NULL;
}

int rk_af_stats_available(void)
{
    return af_handle != NULL && _rk_aiq_ctx != NULL;
}

static void fill_manual_meascfg(rk_aiq_af_algo_meas_v31_t *cfg,
                                unsigned h_offs, unsigned v_offs,
                                unsigned h_size, unsigned v_size)
{
    static const int ver_flt_lowlit[6] = { -372, 851, 465, -77, 0, 77 };
    static const int ver_flt_normal[6] = { -265, 686, 512, -124, 0, 124 };
    static const int hor_flt_lowlit[2][6] = {
        { 203, 811, -375, 673, 0, -673 },
        { 31, 945, -448, 323, 0, -323 }
    };
    static const int hor_flt_normal[2][6] = {
        { 512, 557, -276, 460, 0, -460 },
        { 100, 870, -399, 191, 0, -191 }
    };
    int i;

    memset(cfg, 0, sizeof(*cfg));
    cfg->af_en = 1;
    cfg->rawaf_sel = 0;
    cfg->accu_8bit_mode = 1;
    cfg->ae_mode = 1;
    cfg->v_dnscl_mode = 1;

    /* The RV1106 example configures two identical windows (A and B).  Some
     * IQ tunings reject a single-window manual configuration, and the
     * statistics are read from window A only, so B just mirrors A. */
    cfg->window_num = 2;
    cfg->wina_h_offs = (unsigned short)h_offs;
    cfg->wina_v_offs = (unsigned short)v_offs;
    cfg->wina_h_size = (unsigned short)h_size;
    cfg->wina_v_size = (unsigned short)v_size;
    cfg->winb_h_offs = (unsigned short)h_offs;
    cfg->winb_v_offs = (unsigned short)v_offs;
    cfg->winb_h_size = (unsigned short)h_size;
    cfg->winb_v_size = (unsigned short)v_size;

    cfg->gamma_en = 1;
    memcpy(cfg->gamma_y, gamma_y, sizeof(gamma_y));

    cfg->thres = 4;
    cfg->shift_sum_a = 0;
    cfg->shift_y_a = 0;
    cfg->shift_sum_b = 1;
    cfg->shift_y_b = 1;

    cfg->gaus_en = 1;
    cfg->gaus_coe[1] = 64;
    cfg->gaus_coe[4] = 64;
    cfg->v1_fir_sel = 1;
    cfg->viir_en = 1;
    cfg->v1_fv_outmode = 0;
    cfg->v2_fv_outmode = 0;
    cfg->v1_fv_shift = 1;
    cfg->v2_fv_shift = 1;
    cfg->v_fv_thresh = 0;
    for (i = 0; i < 3; i++)
    {
        cfg->v1_iir_coe[i] = (short)ver_flt_lowlit[i];
        cfg->v1_fir_coe[i] = (short)ver_flt_lowlit[i + 3];
        cfg->v2_iir_coe[i] = (short)ver_flt_normal[i];
        cfg->v2_fir_coe[i] = (short)ver_flt_normal[i + 3];
    }

    cfg->hiir_en = 1;
    cfg->h1_fv_outmode = 0;
    cfg->h2_fv_outmode = 0;
    cfg->h1_fv_shift = 1;
    cfg->h2_fv_shift = 1;
    cfg->h_fv_thresh = 0;
    for (i = 0; i < 6; i++)
    {
        cfg->h1_iir1_coe[i] = (short)hor_flt_lowlit[0][i];
        cfg->h1_iir2_coe[i] = (short)hor_flt_lowlit[1][i];
        cfg->h2_iir1_coe[i] = (short)hor_flt_normal[0][i];
        cfg->h2_iir2_coe[i] = (short)hor_flt_normal[1][i];
    }

    cfg->ldg_en = 0;
    cfg->h_ldg_lumth[0] = 64;
    cfg->h_ldg_gain[0] = 28;
    cfg->h_ldg_gslp[0] = (255 - 28) * 255 / 45;
    cfg->h_ldg_lumth[1] = 185;
    cfg->h_ldg_gain[1] = 8;
    cfg->h_ldg_gslp[1] = (255 - 8) * 255 / 45;
    cfg->v_ldg_lumth[0] = 64;
    cfg->v_ldg_gain[0] = 28;
    cfg->v_ldg_gslp[0] = (255 - 28) * 255 / 45;
    cfg->v_ldg_lumth[1] = 185;
    cfg->v_ldg_gain[1] = 8;
    cfg->v_ldg_gslp[1] = (255 - 8) * 255 / 45;
    cfg->highlit_thresh = 912;
}

int rk_af_stats_set_window(unsigned h_offs, unsigned v_offs,
                           unsigned h_size, unsigned v_size)
{
    rk_aiq_af_attrib_t attr;
    int ret;

    if (!af_handle || !_rk_aiq_ctx)
        return -2;
    if (h_size == 0 || v_size == 0)
        return -1;

    memset(&attr, 0, sizeof(attr));
    ret = af_get_attrib((const rk_aiq_sys_ctx_t *)_rk_aiq_ctx, &attr);
    if (ret != 0)
        return -1;
    if (attr.AfHwVer != RKAIQ_AF_HW_V31)
        return -3;

    attr.AfMode = RKAIQ_AF_MODE_FIXED;
    fill_manual_meascfg(&attr.manual_meascfg_v31, h_offs, v_offs,
                        h_size, v_size);
    attr.sync.sync_mode = RK_AIQ_UAPI_MODE_SYNC;

    ret = af_set_attrib((const rk_aiq_sys_ctx_t *)_rk_aiq_ctx, &attr);
    return ret == 0 ? 0 : -1;
}

int rk_af_stats_force_contrast(void)
{
    rk_aiq_af_attrib_t attr;
    int ret;

    if (!af_handle || !_rk_aiq_ctx)
        return -2;

    memset(&attr, 0, sizeof(attr));
    ret = af_get_attrib((const rk_aiq_sys_ctx_t *)_rk_aiq_ctx, &attr);
    if (ret != 0)
        return -1;
    if (attr.AfHwVer != RKAIQ_AF_HW_V31)
        return -3;

    attr.contrast_af = true;
    attr.sync.sync_mode = RK_AIQ_UAPI_MODE_SYNC;
    ret = af_set_attrib((const rk_aiq_sys_ctx_t *)_rk_aiq_ctx, &attr);
    return ret == 0 ? 0 : -1;
}

int rk_af_stats_read(rk_af_map *map, int timeout_ms)
{
    rk_aiq_isp_stats_t *stats = NULL;
    XCamReturn ret;
    int i;

    if (!map)
        return -1;
    if (!af_handle || !_rk_aiq_ctx)
        return -2;

    ret = af_get_stats((const rk_aiq_sys_ctx_t *)_rk_aiq_ctx, &stats,
                       timeout_ms);
    if (ret == XCAM_RETURN_ERROR_TIMEOUT)
    {
        if (stats)
            af_release_stats((const rk_aiq_sys_ctx_t *)_rk_aiq_ctx, stats);
        return -4;
    }
    if (ret != XCAM_RETURN_NO_ERROR || !stats)
        return -1;
    if (stats->af_hw_ver != RKAIQ_AF_HW_V31)
    {
        af_release_stats((const rk_aiq_sys_ctx_t *)_rk_aiq_ctx, stats);
        return -3;
    }

    map->frame_id = stats->frame_id;
    map->af_hw_ver = stats->af_hw_ver;
    for (i = 0; i < RK_AF_CELLS; i++)
    {
        map->fv_h1[i] = stats->af_stats_v3x.wnda_fv_h1[i];
        map->fv_h2[i] = stats->af_stats_v3x.wnda_fv_h2[i];
        map->fv_v1[i] = stats->af_stats_v3x.wnda_fv_v1[i];
        map->fv_v2[i] = stats->af_stats_v3x.wnda_fv_v2[i];
        map->luma[i] = stats->af_stats_v3x.wnda_luma[i];
        map->highlit[i] = stats->af_stats_v3x.wina_highlit_cnt[i];
    }

    af_release_stats((const rk_aiq_sys_ctx_t *)_rk_aiq_ctx, stats);
    return 0;
}

#else /* !__ARM_PCS_VFP: keep the generic build linkable */

#include "rk_af_stats.h"

int rk_af_stats_init(void) { return -1; }
void rk_af_stats_deinit(void) { }
int rk_af_stats_available(void) { return 0; }
int rk_af_stats_set_window(unsigned h_offs, unsigned v_offs,
                           unsigned h_size, unsigned v_size)
{
    (void)h_offs; (void)v_offs; (void)h_size; (void)v_size;
    return -2;
}
int rk_af_stats_read(rk_af_map *map, int timeout_ms)
{
    (void)map; (void)timeout_ms;
    return -2;
}
int rk_af_stats_force_contrast(void) { return -2; }

#endif /* __ARM_PCS_VFP */