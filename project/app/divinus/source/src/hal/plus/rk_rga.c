#if defined(__ARM_PCS_VFP)

/*
 * Minimal im2d wrapper for NV12 -> RGB888 on the dma-buf fds used by the
 * YOLO ring (MMZ) and by rknn_create_mem() (dma-heap).  The vendor library
 * is resolved at runtime like every other optional HAL in divinus.
 */

#include <dlfcn.h>
#include <string.h>

#include "rk_rga.h"
#include "../macros.h"
#include <rga/im2d.h>

typedef struct {
    void *handle;
    rga_buffer_handle_t (*fnImportFd)(int fd, im_handle_param_t *param);
    rga_buffer_t (*fnWrap)(rga_buffer_handle_t handle, int width, int height,
        int wstride, int hstride, int format);
    IM_STATUS (*fnCvtColor)(rga_buffer_t src, rga_buffer_t dst, int sfmt,
        int dfmt, int mode, int sync);
    IM_STATUS (*fnRelease)(rga_buffer_handle_t handle);
} rk_rga_impl;

static rk_rga_impl rk_rga;
static int rk_rga_tried;

static int rk_rga_load(void)
{
    if (rk_rga_tried) return rk_rga.handle ? 0 : -1;
    rk_rga_tried = 1;

    rk_rga.handle = dlopen("librga.so", RTLD_LAZY | RTLD_LOCAL);
    if (!rk_rga.handle) {
        HAL_INFO("rk_rga", "librga.so is unavailable: %s\n", dlerror());
        return -1;
    }
    *(void **)&rk_rga.fnImportFd = dlsym(rk_rga.handle, "importbuffer_fd");
    *(void **)&rk_rga.fnWrap = dlsym(rk_rga.handle, "wrapbuffer_handle_t");
    *(void **)&rk_rga.fnCvtColor = dlsym(rk_rga.handle, "imcvtcolor_t");
    *(void **)&rk_rga.fnRelease = dlsym(rk_rga.handle, "releasebuffer_handle");
    if (!rk_rga.fnImportFd || !rk_rga.fnWrap || !rk_rga.fnCvtColor ||
        !rk_rga.fnRelease) {
        HAL_WARNING("rk_rga", "librga.so lacks the im2d fd API\n");
        dlclose(rk_rga.handle);
        memset(&rk_rga, 0, sizeof(rk_rga));
        return -1;
    }
    HAL_INFO("rk_rga", "im2d fd conversion is available\n");
    return 0;
}

int rk_rga_available(void)
{
    return rk_rga_load() == 0;
}

int rk_rga_convert_nv12_rgb(int srcFd, int srcWidth, int srcHeight,
    int dstFd, int dstWidth, int dstHeight, int dstStride)
{
    im_handle_param_t srcParam = {
        (unsigned int)srcWidth, (unsigned int)srcHeight,
        RK_FORMAT_YCbCr_420_SP
    };
    im_handle_param_t dstParam = {
        (unsigned int)dstWidth, (unsigned int)dstHeight, RK_FORMAT_RGB_888
    };
    rga_buffer_handle_t src, dst;
    rga_buffer_t srcBuf, dstBuf;
    IM_STATUS status;

    if (rk_rga_load()) return -1;
    if (srcFd < 0 || dstFd < 0 || srcWidth <= 0 || srcHeight <= 0 ||
        dstWidth <= 0 || dstHeight <= 0 || dstStride < dstWidth)
        return -2;

    src = rk_rga.fnImportFd(srcFd, &srcParam);
    if (!src) return -3;
    dst = rk_rga.fnImportFd(dstFd, &dstParam);
    if (!dst) {
        rk_rga.fnRelease(src);
        return -3;
    }

    srcBuf = rk_rga.fnWrap(src, srcWidth, srcHeight, srcWidth, srcHeight,
        RK_FORMAT_YCbCr_420_SP);
    dstBuf = rk_rga.fnWrap(dst, dstWidth, dstHeight, dstStride, dstHeight,
        RK_FORMAT_RGB_888);
    status = rk_rga.fnCvtColor(srcBuf, dstBuf, RK_FORMAT_YCbCr_420_SP,
        RK_FORMAT_RGB_888, IM_COLOR_SPACE_DEFAULT, 1);

    rk_rga.fnRelease(src);
    rk_rga.fnRelease(dst);
    return status == IM_STATUS_SUCCESS ? 0 : -4;
}

#endif