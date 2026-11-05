#pragma once

#include "rk_common.h"

typedef struct {
    void *handle;

    char* (*fnGetData)(void *block);
    int (*fnGetFd)(void *block);
    unsigned long long (*fnGetSize)(void *block);

    /* Physically contiguous MMZ memory.  The NPU runs without an IOMMU and
     * VENC SendFrame takes an MB_BLK, so both paths need this allocator
     * instead of the C heap. */
    int (*fnAlloc)(void **block, const char *mmb, const char *zone,
        unsigned int size);
    int (*fnFree)(void *block);
} rk_mb_impl;

static int rk_mb_load(rk_mb_impl *mb_lib) {
    if (!(mb_lib->handle = dlopen("librockit.so", RTLD_LAZY | RTLD_GLOBAL)))
        HAL_ERROR("rk_mb", "Failed to load library!\nError: %s\n", dlerror());

    if (!(mb_lib->fnGetData = (char*(*)(void *block))
        hal_symbol_load("rk_mb", mb_lib->handle, "RK_MPI_MB_Handle2VirAddr")))
        return EXIT_FAILURE;

    if (!(mb_lib->fnGetFd = (int(*)(void *block))
        hal_symbol_load("rk_mb", mb_lib->handle, "RK_MPI_MB_Handle2Fd")))
        return EXIT_FAILURE;

    if (!(mb_lib->fnGetSize = (unsigned long long(*)(void *block))
        hal_symbol_load("rk_mb", mb_lib->handle, "RK_MPI_MB_GetSize")))
        return EXIT_FAILURE;

    if (!(mb_lib->fnAlloc = (int(*)(void **block, const char *mmb,
            const char *zone, unsigned int size))
        hal_symbol_load("rk_mb", mb_lib->handle, "RK_MPI_SYS_MmzAlloc")))
        return EXIT_FAILURE;

    if (!(mb_lib->fnFree = (int(*)(void *block))
        hal_symbol_load("rk_mb", mb_lib->handle, "RK_MPI_SYS_Free")))
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static void rk_mb_unload(rk_mb_impl *mb_lib) {
    if (mb_lib->handle) dlclose(mb_lib->handle);
    mb_lib->handle = NULL;
    memset(mb_lib, 0, sizeof(*mb_lib));
}