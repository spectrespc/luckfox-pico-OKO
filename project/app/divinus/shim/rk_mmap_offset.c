/*
 * Workaround for the RV1106 rockit build shipped with this SDK.
 *
 * rockit's encoder adapter maps the VENC output ring in two parts with
 * MAP_SHARED | MAP_FIXED. On this rockit build the mmap2() call for the
 * first part does not carry the page offset argument, so the syscall picks
 * up a stale stack value and fails ("mmap part 1 failed" in
 * MpiAdapterEncode::ma_encode_get_pack); the encoder then never delivers
 * frames.
 *
 * divinus only ever maps those ring buffers at offset 0, so force the
 * offset to 0 for large MAP_SHARED | MAP_FIXED mmap2() calls and pass
 * every other syscall through unchanged. Loaded with LD_PRELOAD for the
 * divinus process only (see RkLunch.sh); rkipc and other applications are
 * not affected.
 *
 * Remove this shim once rockit/divinus is updated upstream and the ring
 * buffers map without it.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#ifndef __NR_mmap2
#define __NR_mmap2 192
#endif

static long (*next_syscall)(long, ...);
static int reported;

long syscall(long number, ...) {
    va_list ap;
    unsigned long arg[6];

    if (!next_syscall)
        next_syscall = (long (*)(long, ...))dlsym(RTLD_NEXT, "syscall");

    va_start(ap, number);
    for (int i = 0; i < 6; i++)
        arg[i] = va_arg(ap, unsigned long);
    va_end(ap);

    if (number == __NR_mmap2 &&
        arg[3] == (unsigned long)(MAP_SHARED | MAP_FIXED) &&
        arg[1] >= 32768 && (long)arg[4] >= 0) {
        if (!reported) {
            reported = 1;
            fprintf(stderr, "[divinus] VENC ring mmap offset workaround active\n");
        }
        return next_syscall(number, arg[0], arg[1], arg[2], arg[3], arg[4], 0UL);
    }

    return next_syscall(number, arg[0], arg[1], arg[2], arg[3], arg[4], arg[5]);
}