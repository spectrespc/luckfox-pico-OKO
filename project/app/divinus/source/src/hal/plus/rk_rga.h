#pragma once

/* Optional RGA (im2d) converter.  librga is dlopen()ed on first use, so an
 * image without it simply keeps the CPU conversion path. */
int rk_rga_available(void);

/* NV12 (srcFd) -> packed RGB888 (dstFd) with scaling.
 * srcStride/dstStride are in pixels (not bytes); returns 0 on success. */
int rk_rga_convert_nv12_rgb(int srcFd, int srcWidth, int srcHeight,
    int dstFd, int dstWidth, int dstHeight, int dstStride);