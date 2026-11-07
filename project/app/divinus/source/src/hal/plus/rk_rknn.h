#pragma once

/*
 * Optional RKNN (mini runtime) loader.
 *
 * The vendor library is not linked into divinus: `project/app/divinus/Makefile`
 * only passes -ldl, exactly like librockit and librkaiq.  Every entry point is
 * resolved at runtime, so an image without librknnmrt.so still boots and the
 * YOLO feature degrades to a gating error.
 *
 * Zero-copy API: RV1106 models are driven through rknn_create_mem() /
 * rknn_set_io_mem(); the classic rknn_inputs_set() path copies through the CPU
 * and is not used by the vendor examples on this platform.
 */

#include "rk_common.h"
#include "rknn_api.h"

/* Two runtimes may live in one process: AIISP/rockiva are prebuilt against
 * librknnmrt.so 1.6.0, while a model converted by toolkit 2.x needs the 2.x
 * runtime.  Both vendor files carry SONAME librknnmrt.so, so the 2.x copy is
 * re-stamped to librknnmrt2.so and loaded by an absolute path. */
#define RK_RKNN_RUNTIME1_PATH "/oem/usr/lib/librknnmrt.so"
#define RK_RKNN_RUNTIME2_PATH "/oem/usr/lib/librknnmrt2.so"

typedef struct {
    void *handle;

    int (*fnInit)(rknn_context *context, void *model, unsigned int size,
        unsigned int flag, rknn_init_extend *extend);
    int (*fnDestroy)(rknn_context context);
    int (*fnQuery)(rknn_context context, rknn_query_cmd cmd, void *info,
        unsigned int size);
    int (*fnRun)(rknn_context context, rknn_run_extend *extend);

    rknn_tensor_mem *(*fnCreateMem)(rknn_context context, unsigned int size);
    int (*fnDestroyMem)(rknn_context context, rknn_tensor_mem *mem);
    int (*fnSetIoMem)(rknn_context context, rknn_tensor_mem *mem,
        rknn_tensor_attr *attr);
    int (*fnMemSync)(rknn_context context, rknn_tensor_mem *mem,
        rknn_mem_sync_mode mode);
} rk_rknn_impl;

extern rk_rknn_impl rk_rknn;
extern rk_rknn_impl rk_rknn2;

/* Selected runtime implementation, or NULL when that library is missing. */
rk_rknn_impl *rk_rknn_for_runtime(int runtime);
int rk_rknn_runtime_available(int runtime);

static int rk_rknn_load(rk_rknn_impl *rknn_lib, const char *path, int local) {
    int flags = RTLD_LAZY | (local ? RTLD_LOCAL : RTLD_GLOBAL);

    if (!(rknn_lib->handle = dlopen(path, flags)))
        HAL_ERROR("rk_rknn", "Failed to load %s!\nError: %s\n", path,
            dlerror());

    if (!(rknn_lib->fnInit = (int(*)(rknn_context *context, void *model,
            unsigned int size, unsigned int flag, rknn_init_extend *extend))
        hal_symbol_load("rk_rknn", rknn_lib->handle, "rknn_init")))
        return EXIT_FAILURE;

    if (!(rknn_lib->fnDestroy = (int(*)(rknn_context context))
        hal_symbol_load("rk_rknn", rknn_lib->handle, "rknn_destroy")))
        return EXIT_FAILURE;

    if (!(rknn_lib->fnQuery = (int(*)(rknn_context context, rknn_query_cmd cmd,
            void *info, unsigned int size))
        hal_symbol_load("rk_rknn", rknn_lib->handle, "rknn_query")))
        return EXIT_FAILURE;

    if (!(rknn_lib->fnRun = (int(*)(rknn_context context, rknn_run_extend *extend))
        hal_symbol_load("rk_rknn", rknn_lib->handle, "rknn_run")))
        return EXIT_FAILURE;

    if (!(rknn_lib->fnCreateMem = (rknn_tensor_mem*(*)(rknn_context context,
            unsigned int size))
        hal_symbol_load("rk_rknn", rknn_lib->handle, "rknn_create_mem")))
        return EXIT_FAILURE;

    if (!(rknn_lib->fnDestroyMem = (int(*)(rknn_context context,
            rknn_tensor_mem *mem))
        hal_symbol_load("rk_rknn", rknn_lib->handle, "rknn_destroy_mem")))
        return EXIT_FAILURE;

    if (!(rknn_lib->fnSetIoMem = (int(*)(rknn_context context,
            rknn_tensor_mem *mem, rknn_tensor_attr *attr))
        hal_symbol_load("rk_rknn", rknn_lib->handle, "rknn_set_io_mem")))
        return EXIT_FAILURE;

    /* Cache maintenance is not needed for the dma-heap buffers returned by
     * rknn_create_mem(), but keep it optional for older runtimes. */
    rknn_lib->fnMemSync = (int(*)(rknn_context context, rknn_tensor_mem *mem,
        rknn_mem_sync_mode mode))
        dlsym(rknn_lib->handle, "rknn_mem_sync");

    return EXIT_SUCCESS;
}

static void rk_rknn_unload(rk_rknn_impl *rknn_lib) {
    if (rknn_lib->handle) dlclose(rknn_lib->handle);
    rknn_lib->handle = NULL;
    memset(rknn_lib, 0, sizeof(*rknn_lib));
}