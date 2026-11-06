#include "gating.h"

#include <stdio.h>
#include <string.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include "app_config.h"
#include "hal/globals.h"
#include "media.h"

static const gating_result allowed = { true, NULL, NULL };

static gating_result blocked(const char *reason, const char *hint)
{
    gating_result result = { false, reason, hint };
    return result;
}

bool gating_sd_mounted(void)
{
    FILE *file = fopen("/proc/mounts", "r");
    char line[512];

    if (!file) return false;
    while (fgets(line, sizeof(line), file)) {
        char device[256], mount[256], type[64];
        if (sscanf(line, "%255s %255s %63s", device, mount, type) != 3) continue;
        if (!strcmp(mount, "/mnt/sdcard")) {
            fclose(file);
            return true;
        }
    }
    fclose(file);
    return false;
}

bool gating_sd_space(unsigned long long *total, unsigned long long *freeBytes)
{
    struct statvfs stat;

    if (!gating_sd_mounted()) return false;
    if (statvfs("/mnt/sdcard", &stat)) return false;
    if (!stat.f_blocks) return false;

    if (total) *total = (unsigned long long)stat.f_blocks * stat.f_frsize;
    if (freeBytes) *freeBytes = (unsigned long long)stat.f_bavail * stat.f_frsize;
    return true;
}

static int enabled_channels(void)
{
    int used = 0;

    for (int i = 0; i < chnCount; i++)
        if (chnState[i].enable) used++;
    return used;
}

gating_result gating_check(const char *feature)
{
    if (!feature) return blocked("unknown feature", NULL);

    if (!strcmp(feature, "eptz")) {
        if (!(halCapabilities & HAL_CAP_EPTZ))
            return blocked("VPSS crop is not available on this platform",
                "the librockit build has no RK_MPI_VPSS_SetChnCrop");
        return allowed;
    }

    if (!strcmp(feature, "rotate")) {
        if (!(halCapabilities & HAL_CAP_ROTATE))
            return blocked("VPSS rotation is not available on this platform",
                "the librockit build has no RK_MPI_VPSS_SetChnRotation");
        return allowed;
    }

    if (!strcmp(feature, "venc")) {
        if (!(halCapabilities & HAL_CAP_VENC_EXTRA))
            return blocked("encoder extensions are not available on this platform",
                "SmartP/SVC/deblur/QP symbols are missing from librockit");
        return allowed;
    }

    if (!strcmp(feature, "substream")) {
        int limit = chnCount - (app_config.ivs_enable ? 1 : 0);
        if (channel_by_role(HAL_CHNROLE_SUB) < 0 &&
            enabled_channels() >= limit)
            return blocked("no free VPSS channel for the sub stream",
                "disable MJPEG or analytics first");
        return allowed;
    }

    if (!strcmp(feature, "aiisp")) {
        char model[256];
        if (!(halCapabilities & HAL_CAP_AIISP))
            return blocked("AIISP is not available in librockit",
                "the vendor library has no RK_MPI_VPSS_SetGrpAIISPAttr");
        if (app_config.hdr_mode != 0)
            return blocked("AIISP cannot be combined with HDR",
                "switch hdr.mode to normal first");
        snprintf(model, sizeof(model), "%s/rkmodel_1080_1920.aiisp",
            app_config.aiisp_model_dir);
        if (access(model, R_OK))
            return blocked("AIISP model is missing from the image",
                "set RK_AIISP_MODEL in the board config and reflash oem");
        return allowed;
    }

    if (!strcmp(feature, "ivs")) {
        if (!(halCapabilities & HAL_CAP_IVS))
            return blocked("IVS is not available in librockit",
                "the vendor library has no IVS MPI symbols");
        if (chnState[HAL_CHN_IVS].enable)
            return blocked("no free VPSS channel for analytics",
                "disable MJPEG and restart the stream");
        if (app_config.yolo_enable && app_config.yolo_source == 1)
            return blocked("VPSS channel 3 is reserved by YOLO",
                "set yolo.source to vi0 or disable YOLO first");
        return allowed;
    }

    if (!strcmp(feature, "yolo")) {
        if (!(halCapabilities & HAL_CAP_RKNN))
            return blocked("the NPU runtime is not available",
                "librknnmrt.so is missing; reflash the oem partition");
        /* VI channel 1 (selfpath) is exposed by the ISP topology but enabling
         * it locks the RV1106 board until power-cycled or reflashed (M0). */
        if (app_config.yolo_source == 0)
            return blocked("VI selfpath (vi1) hangs the board on RV1106",
                "set yolo.source to vpss3");
        /* VPSS has four output channels.  Channel 3 is the last one, so any
         * fourth stream, IVS or another detector user owns it on RV1106. */
        if (app_config.yolo_source == 1 &&
            (app_config.ivs_enable || app_config.mjpeg_enable ||
             chnState[HAL_CHN_IVS].enable))
            return blocked("VPSS channel 3 is already used",
                "set yolo.source to vi0 or disable IVS/MJPEG");
        if (access(app_config.yolo_model, R_OK))
            return blocked("the configured model is missing",
                "upload a .rknn model in the Events section");
        return allowed;
    }

    if (!strcmp(feature, "focus_metric")) {
        if (!(halCapabilities & HAL_CAP_FOCUS_STATS))
            return blocked("AF statistics are not available in this librkaiq build",
                "check RKAIQ_ENABLE_AF and the af_* exports");
        return allowed;
    }

    if (!strcmp(feature, "focus_motor")) {
        if (!app_config.focus_motor)
            return blocked("the focus motor is disabled in the configuration",
                "set focus.motor to true first");
        return allowed;
    }

    if (!strcmp(feature, "iq")) {
        if (!(halCapabilities & HAL_CAP_IQ))
            return blocked("runtime IQ control is not available",
                "librkaiq has no switch_scene/updateIq export");
        return allowed;
    }

    if (!strcmp(feature, "hdr")) {
        if (app_config.aiisp_enable != HAL_AIISP_OFF)
            return blocked("HDR cannot be combined with AIISP",
                "switch aiisp.enable to off first");
        /* Gate G8: rk_aiq_uapi_sysctl_prepare(mode=HDR2/HDR3) fails on
         * SC230AI with this librkaiq build and the app falls back to normal.
         * Keep the feature off until a matching sensor/IQ combination is
         * verified. */
        return blocked("HDR is not supported by the SC230AI build",
            "stay at hdr.mode=normal; the camera would fall back to SDR");
    }

    if (!strcmp(feature, "osd_all")) {
        if (!(halCapabilities & HAL_CAP_OSD_MULTI))
            return blocked("multi-channel OSD has not been verified on this build",
                "keep osd.scope=main; the multi-attach PoC gate must pass first");
        return allowed;
    }

    if (!strcmp(feature, "record")) {
        if (!gating_sd_mounted())
            return blocked("recording requires a mounted microSD card",
                "insert a card formatted as vfat; local storage is not used");
        return allowed;
    }

    return blocked("unknown feature", NULL);
}