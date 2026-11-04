#pragma once

#include "rk_common.h"

/* RV1106 IVS (motion/object detection) MPI.  The structs mirror the vendor
 * rk_comm_ivs.h / rk_comm_video.h layouts because the pointers are passed
 * straight to librockit. */
#define RK_IVS_MAX_AREA_NUM 16
#define RK_IVS_MAX_AREA_POINT_NUM 6
#define RK_IVS_MAX_RECT_NUM 4096

typedef struct {
    int x, y;
} rk_ivs_point;

typedef struct {
    unsigned int pointNum;
    rk_ivs_point points[RK_IVS_MAX_AREA_POINT_NUM];
} rk_ivs_area;

typedef struct {
    unsigned int areaNum;
    rk_ivs_area areas[RK_IVS_MAX_AREA_NUM];
} rk_ivs_areas;

typedef struct {
    rk_ivs_areas area;
} rk_ivs_det_attr;

typedef struct {
    int mode;                   /* IVS_MODE_MD=1, OD=2, MD_OD=3 */
    unsigned int width, height;
    int pixFmt;
    int rotation;
    int mirror;
    int traceClose;
    int gop;
    int smearEnable;
    int weightpEnable;
    int mdEnable;
    int mdInterval;
    int mdNightMode;
    unsigned int mdSensitivity;
    int odEnable;
    int odInterval;
    int odPercent;
    rk_ivs_det_attr detAttr;
    unsigned int maxWidth, maxHeight;
} rk_ivs_chn_attr;

typedef struct {
    unsigned int frameId;
    unsigned int rectNum;
    rk_common_rect rect[RK_IVS_MAX_RECT_NUM];
    char *data;
    unsigned int size;
    int fd;
    unsigned int square;
    unsigned int detAreaSquare;
    unsigned int detOutputSquare;
} rk_ivs_md_info;

typedef struct {
    unsigned int frameId;
    unsigned int flag;
    unsigned int pixSum;
} rk_ivs_od_info;

typedef struct {
    rk_ivs_md_info md;
    rk_ivs_od_info od;
} rk_ivs_result;

typedef struct {
    int resultNum;
    rk_ivs_result *results;
} rk_ivs_result_info;

typedef struct {
    void *handle;

    int (*fnCreateChn)(int chn, rk_ivs_chn_attr *attr);
    int (*fnDestroyChn)(int chn);
    int (*fnSetChnAttr)(int chn, rk_ivs_chn_attr *attr);
    int (*fnGetChnAttr)(int chn, rk_ivs_chn_attr *attr);
    int (*fnGetResults)(int chn, rk_ivs_result_info *res, int timeout);
    int (*fnReleaseResults)(int chn, rk_ivs_result_info *res);
} rk_ivs_impl;

static int rk_ivs_load(rk_ivs_impl *ivs_lib) {
    if (!(ivs_lib->handle = dlopen("librockit.so", RTLD_LAZY | RTLD_GLOBAL))) {
        HAL_DANGER("rk_ivs", "Failed to load library!\nError: %s\n", dlerror());
        return EXIT_FAILURE;
    }

    ivs_lib->fnCreateChn = (int(*)(int, rk_ivs_chn_attr*))
        dlsym(ivs_lib->handle, "RK_MPI_IVS_CreateChn");
    ivs_lib->fnDestroyChn = (int(*)(int))
        dlsym(ivs_lib->handle, "RK_MPI_IVS_DestroyChn");
    ivs_lib->fnSetChnAttr = (int(*)(int, rk_ivs_chn_attr*))
        dlsym(ivs_lib->handle, "RK_MPI_IVS_SetChnAttr");
    ivs_lib->fnGetChnAttr = (int(*)(int, rk_ivs_chn_attr*))
        dlsym(ivs_lib->handle, "RK_MPI_IVS_GetChnAttr");
    ivs_lib->fnGetResults = (int(*)(int, rk_ivs_result_info*, int))
        dlsym(ivs_lib->handle, "RK_MPI_IVS_GetResults");
    ivs_lib->fnReleaseResults = (int(*)(int, rk_ivs_result_info*))
        dlsym(ivs_lib->handle, "RK_MPI_IVS_ReleaseResults");

    if (!ivs_lib->fnCreateChn || !ivs_lib->fnDestroyChn ||
        !ivs_lib->fnGetResults || !ivs_lib->fnReleaseResults) {
        HAL_DANGER("rk_ivs", "IVS MPI symbols are unavailable\n");
        dlclose(ivs_lib->handle);
        memset(ivs_lib, 0, sizeof(*ivs_lib));
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static void rk_ivs_unload(rk_ivs_impl *ivs_lib) {
    if (ivs_lib->handle) dlclose(ivs_lib->handle);
    ivs_lib->handle = NULL;
    memset(ivs_lib, 0, sizeof(*ivs_lib));
}