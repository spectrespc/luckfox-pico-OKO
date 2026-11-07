#if defined(__ARM_PCS_VFP)

#include <stdint.h>

#include "rk_hal.h"
#include "rk_af_stats.h"
#include "rk_rgn_pack.h"
#include "../../app_config.h"
#include "../../analytics/nn_geom.h"

rk_aiq_impl     rk_aiq;
rk_aud_impl     rk_aud;
rk_ivs_impl     rk_ivs;
rk_mb_impl      rk_mb;
rk_rgn_impl     rk_rgn;
rk_rknn_impl    rk_rknn;
rk_rknn_impl    rk_rknn2;

rk_rknn_impl *rk_rknn_for_runtime(int runtime) {
    if (runtime == 2)
        return rk_rknn2.handle ? &rk_rknn2 : NULL;
    return rk_rknn.handle ? &rk_rknn : NULL;
}

int rk_rknn_runtime_available(int runtime) {
    return rk_rknn_for_runtime(runtime) != NULL;
}
rk_sys_impl     rk_sys;
rk_venc_impl    rk_venc;
rk_vi_impl      rk_vi;
rk_vpss_impl    rk_vpss;

hal_chnstate rk_state[RK_VENC_CHN_NUM] = {0};
int (*rk_aud_cb)(hal_audframe*);
int (*rk_vid_cb)(char, hal_vidstream*);

void *_rk_aiq_ctx = NULL;
char _rk_aud_chn = 0;
char _rk_aud_dev = 0;
char _rk_isp_chn = 0;
char _rk_isp_dev = 0;
char _rk_venc_dev = 0;
char _rk_vi_chn = 0;
char _rk_vi_dev = 0;
char _rk_vi_pipe = 0;
char _rk_vpss_chn = 0;
char _rk_vpss_grp = 0;

/* YOLO capture source and the dedicated event JPEG encoder (VENC chn 4, the
 * channel rkipc uses for snapshots).  These are independent of the VENC pool
 * in rk_state[] because they are never bound to a VPSS channel. */
#define RK_YOLO_JPEG_CHN 4
static int _rk_yolo_source = -1;
static int _rk_vi_depth = 0;
static int _rk_yolo_chn = -1;
static unsigned short _rk_yolo_width = 0, _rk_yolo_height = 0;
static void *_rk_yolo_jpeg_blk = NULL;
static int _rk_yolo_jpeg_ready = 0;
static unsigned short _rk_yolo_jpeg_width = 0, _rk_yolo_jpeg_height = 0;

void rk_hal_deinit(void)
{
    rk_af_stats_deinit();
    rk_ivs_unload(&rk_ivs);
    rk_vpss_unload(&rk_vpss);
    rk_vi_unload(&rk_vi);
    rk_venc_unload(&rk_venc);
    rk_rknn_unload(&rk_rknn);
    rk_rknn_unload(&rk_rknn2);
    rk_rgn_unload(&rk_rgn);
    rk_mb_unload(&rk_mb);
    rk_aud_unload(&rk_aud);
    rk_aiq_unload(&rk_aiq);
    rk_sys_unload(&rk_sys);
}

int rk_hal_init(void)
{
    int ret;

    if (ret = rk_sys_load(&rk_sys))
        return ret;
    if (ret = rk_aiq_load(&rk_aiq))
        return ret;
    /* Optional: a librkaiq build without AF exports must not fail startup. */
    if (rk_af_stats_init() == 0)
        halCapabilities |= HAL_CAP_FOCUS_STATS;
    if (ret = rk_aud_load(&rk_aud))
        return ret;
    if (ret = rk_mb_load(&rk_mb))
        return ret;
    if (ret = rk_rgn_load(&rk_rgn))
        return ret;
    if (ret = rk_venc_load(&rk_venc))
        return ret;
    if (ret = rk_vi_load(&rk_vi))
        return ret;
    if (ret = rk_vpss_load(&rk_vpss))
        return ret;

    /* IVS is optional: an older librockit simply disables analytics. */
    if (rk_ivs_load(&rk_ivs))
        HAL_DANGER("rk_hal", "IVS is unavailable, analytics disabled\n");

    /* NPU analytics is optional as well: the image may not carry the mini
     * runtime, and a missing librknnmrt must not stop the camera. */
    if (rk_rknn_load(&rk_rknn, RK_RKNN_RUNTIME1_PATH, 0))
        HAL_DANGER("rk_hal", "RKNN 1.x is unavailable, YOLO detection disabled\n");
    else
        halCapabilities |= HAL_CAP_RKNN;

    /* The 2.x runtime is an optional second copy under a distinct SONAME.
     * It is loaded RTLD_LOCAL so its rknn_* symbols cannot interpose the
     * 1.6.0 copy that AIISP/rockiva already use. */
    if (!rk_rknn_load(&rk_rknn2, RK_RKNN_RUNTIME2_PATH, 1)) {
        halCapabilities |= HAL_CAP_RKNN2;
        HAL_INFO("rk_hal", "RKNN 2.x runtime loaded from %s\n",
            RK_RKNN_RUNTIME2_PATH);
    } else {
        HAL_INFO("rk_hal", "RKNN 2.x runtime is not installed\n");
    }


    if (rk_vpss.fnSetChnCrop) halCapabilities |= HAL_CAP_EPTZ;
    if (rk_vpss.fnSetChnRotation) halCapabilities |= HAL_CAP_ROTATE;
    if (rk_venc.fnEnableSvc || rk_venc.fnSetRcParam)
        halCapabilities |= HAL_CAP_VENC_EXTRA;
    if (rk_vpss.fnSetGrpAIISPAttr) halCapabilities |= HAL_CAP_AIISP;
    if (rk_ivs.fnCreateChn) halCapabilities |= HAL_CAP_IVS;
    if (rk_aiq.fnSwitchScene || rk_aiq.fnUpdateIq)
        halCapabilities |= HAL_CAP_IQ;
    if (rk_aiq.fnSwWorkingModeDyn) halCapabilities |= HAL_CAP_HDR_DYN;

    return EXIT_SUCCESS;
}

void rk_audio_deinit(void)
{
    rk_aud.fnDisableChannel(_rk_aud_dev, _rk_aud_chn);

    rk_aud.fnDisableDevice(_rk_aud_dev);
}

int rk_audio_init(int samplerate)
{
    int ret;

    {
        rk_aud_cnf config;
        config.cardChnNum = 1;
        config.cardRate = samplerate;
        config.cardBit = RK_AUD_BIT_16;
        config.rate = samplerate;
        config.bit = RK_AUD_BIT_16;
        config.mode = RK_AUD_MODE_MONO;
        config.expandOn = 0;
        config.frmNum = 30;
        config.packNumPerFrm = 320;
        config.chnNum = 1;
        config.cardName[0] = '\0';
        if (ret = rk_aud.fnSetDeviceConfig(_rk_aud_dev, &config))
            return ret;
    }
    if (ret = rk_aud.fnEnableDevice(_rk_aud_dev))
        return ret;

    if (ret = rk_aud.fnEnableChannel(_rk_aud_dev, _rk_aud_chn))
        return ret;

    return EXIT_SUCCESS;
}

void *rk_audio_thread(void)
{
    int ret;

    rk_aud_frm frame;
    rk_aud_efrm echoFrame;
    memset(&frame, 0, sizeof(frame));
    memset(&echoFrame, 0, sizeof(echoFrame));

    while (keepRunning && audioOn) {
        while ((ret = rk_aud.fnGetFrame(_rk_aud_dev, _rk_aud_chn,
            &frame, &echoFrame, 0)) == 0) {
            if (rk_aud_cb) {
                hal_audframe outFrame;
                outFrame.channelCnt = 1;
                outFrame.data[0] = rk_mb.fnGetData(frame.mbBlk);
                outFrame.length[0] = frame.length;
                outFrame.seq = frame.sequence;
                outFrame.timestamp = frame.timestamp;
                (rk_aud_cb)(&outFrame);
            }

            if (ret = rk_aud.fnFreeFrame(_rk_aud_dev, _rk_aud_chn,
                &frame, &echoFrame)) {
                HAL_DANGER("rk_aud", "Releasing the frame failed"
                    " with %#x!\n", ret);
            }
        }

        usleep(5000);
    }
    HAL_INFO("rk_aud", "Shutting down encoding thread...\n");
}

/* The RV1106 honours RK_MPI_VPSS_SetChnCrop and the VENC rotation when they
 * are set before RK_MPI_VPSS_EnableChn; a call on an already enabled channel
 * returns success but leaves the picture unchanged.  Every channel (re)start
 * therefore re-applies the stored settings before the channel is enabled. */
static void rk_apply_stream_settings(char index)
{
    int role = rk_state[index].role;

    if (app_config.eptz_enable &&
        role == (app_config.eptz_stream ? HAL_CHNROLE_SUB : HAL_CHNROLE_MAIN)) {
        hal_crop crop;

        memset(&crop, 0, sizeof(crop));
        crop.enable = 1;
        crop.rect.x = app_config.eptz_x;
        crop.rect.y = app_config.eptz_y;
        crop.rect.width = app_config.eptz_w;
        crop.rect.height = app_config.eptz_h;
        rk_vpss_set_chn_crop(index, &crop);
    }

    if (app_config.rotate_enable &&
        role == (app_config.rotate_stream ? HAL_CHNROLE_SUB : HAL_CHNROLE_MAIN)) {
        hal_rotation rotation = HAL_ROTATE_NONE;

        switch (app_config.rotate_enable ? app_config.rotate_angle : 0) {
            case 90: rotation = HAL_ROTATE_90; break;
            case 180: rotation = HAL_ROTATE_180; break;
            case 270: rotation = HAL_ROTATE_270; break;
            default: rotation = HAL_ROTATE_NONE; break;
        }
        if (rk_video_set_rotation(index, rotation) != EXIT_SUCCESS)
            rk_vpss_set_chn_rotation(index, rotation);
    }
}

int rk_channel_bind(char index)
{
    int ret;

    rk_apply_stream_settings(index);

    if (ret = rk_vpss.fnEnableChannel(_rk_vpss_grp, index))
        return ret;

    {
        rk_sys_bind source = { .module = RK_SYS_MOD_VPSS,
            .device = _rk_vpss_grp, .channel = index };
        rk_sys_bind dest = { .module = RK_SYS_MOD_VENC,
            .device = _rk_venc_dev, .channel = index };
        if (ret = rk_sys.fnBind(&source, &dest))
            return ret;
    }

    return EXIT_SUCCESS;
}

int rk_channel_create(char index, short width, short height, char mirror, char flip)
{
    int ret;

    {
        rk_vpss_chn channel;
        memset(&channel, 0, sizeof(channel));
        channel.chnMode = RK_VPSS_CMODE_AUTO;
        channel.dest.width = width;
        channel.dest.height = height;
        channel.pixFmt = RK_PIXFMT_YUV420SP;
        channel.hdr = RK_HDR_SDR8;
        channel.srcFps = -1;
        channel.dstFps = -1;
        channel.mirror = mirror;
        channel.flip = flip;
        channel.depth = 0;
        channel.privFrmBufCnt = 1;
        if (ret = rk_vpss.fnSetChannelConfig(_rk_vpss_grp, index, &channel))
            return ret;
    }

    return EXIT_SUCCESS;
}

int rk_channel_grayscale(char enable)
{
    int ret;

    for (char i = 0; i < RK_VENC_CHN_NUM; i++) {
        rk_venc_para param;
        if (!rk_state[i].enable) continue;
        if (ret = rk_venc.fnGetChannelParam(i, &param))
            return ret;
        param.grayscaleOn = enable;
        if (ret = rk_venc.fnSetChannelParam(i, &param))
            return ret;
    }

    return EXIT_SUCCESS;
}

int rk_channel_unbind(char index)
{
    int ret;

    if (ret = rk_vpss.fnDisableChannel(_rk_vpss_grp, index))
        return ret;

    {
        rk_sys_bind source = { .module = RK_SYS_MOD_VPSS,
            .device = _rk_vpss_grp, .channel = index };
        rk_sys_bind dest = { .module = RK_SYS_MOD_VENC,
            .device = _rk_venc_dev, .channel = index };
        if (ret = rk_sys.fnUnbind(&source, &dest))
            return ret;
    }

    return EXIT_SUCCESS;
}

int rk_pipeline_create(short width, short height)
{
    int ret, v4l2dev = rk_sensor_find_v4l2_endpoint();
    char endpoint[32], *snrStart, *snrEnd;

    if (v4l2dev < 0)
        HAL_ERROR("rk_hal", "Failed to find sensor endpoint!\n");

    snprintf(endpoint, sizeof(endpoint), "/dev/video%d", v4l2dev);

    const char *snrEnt = rk_aiq.fnGetSensorFromV4l2(endpoint);
    if (!snrEnt)
        HAL_ERROR("rk_aiq", "Failed to get the sensor entity name!\n");
    else if ((snrStart = strrchr(snrEnt, '_')) &&
        (snrEnd = strchr(snrStart, ' ')) && snrEnd > ++snrStart)
        snprintf(sensor, snrEnd - snrStart + 1, "%s", snrStart);

    if (ret = rk_aiq.fnPreInitBuf(snrEnt, "rkraw_rx", 2))
        HAL_ERROR("rk_aiq", "Failed to pre-initialize buffer!\n");

    if (ret = rk_aiq.fnPreInitScene(snrEnt, "normal", "day"))
        HAL_ERROR("rk_aiq", "Failed to pre-initialize scene!\n");

    if (!(_rk_aiq_ctx = rk_aiq.fnInit(snrEnt, "/oem/usr/share/iqfiles", NULL, NULL)))
        HAL_ERROR("rk_aiq", "Failed to initialize!\n");

    {
        rk_aiq_work work = RK_AIQ_WORK_NORMAL;
        if (app_config.hdr_mode == 1) work = RK_AIQ_WORK_ISP_HDR2;
        else if (app_config.hdr_mode == 2) work = RK_AIQ_WORK_ISP_HDR3;
        ret = rk_aiq.fnPrepare(_rk_aiq_ctx, width, height, work);
        if (ret && work != RK_AIQ_WORK_NORMAL) {
            /* SC230AI HDR is experimental; never leave the camera dead. */
            HAL_WARNING("rk_aiq", "HDR prepare failed (%#x), falling back to "
                "normal mode\n", ret);
            app_config.hdr_mode = 0;
            /* Remember the fallback so the next boot does not retry an HDR
             * mode this sensor/library combination cannot provide. */
            app_config_save();
            ret = rk_aiq.fnPrepare(_rk_aiq_ctx, width, height,
                RK_AIQ_WORK_NORMAL);
        }
        if (ret)
            HAL_ERROR("rk_aiq", "Failed to prepare device!\n");
    }

    if (ret = rk_aiq.fnStart(_rk_aiq_ctx))
        HAL_ERROR("rk_aiq", "Failed to start device!\n");

    if (ret = rk_vi.fnEnableDevice(_rk_vi_dev))
        return ret;

    {
        rk_vi_bind bind;
        memset(&bind, 0, sizeof(bind));
        bind.num = 1;
        bind.pipeId[0] = _rk_vi_pipe;
        if (ret = rk_vi.fnBindPipe(_rk_vi_dev, &bind))
            return ret;
    }

    {
        rk_vi_chn channel;
        memset(&channel, 0, sizeof(channel));
        channel.size.width = width;
        channel.size.height = height;
        channel.pixFmt = RK_PIXFMT_YUV420SP;
        channel.compress = RK_COMPR_NONE;
        /* yolo.source=vi0 is the emergency fallback: it reads the same main
         * channel, so the user frame queue has to be non-zero. */
        channel.depth = (app_config.yolo_enable && app_config.yolo_source == 2)
            ? 2 : 0;
        _rk_vi_depth = (int)channel.depth;
        channel.srcFps = -1;
        channel.dstFps = -1;
        channel.ispOpts.bufCount = 2;
        channel.ispOpts.bufSize = width * height * 3 / 2;
        channel.ispOpts.vidCap = RK_VI_VCAP_VIDEO_MPLANE;
        channel.ispOpts.vidMem = RK_VI_VMEM_DMABUF;
        strcpy(channel.ispOpts.entityName, "rkisp_mainpath");
        if (ret = rk_vi.fnSetChannelConfig(_rk_vi_pipe, _rk_vi_chn, &channel))
            return ret;
    }
    if (ret = rk_vi.fnEnableChannel(_rk_vi_pipe, _rk_vi_chn))
        return ret;

    {
        rk_vpss_grp group;
        group.dest.width = width;
        group.dest.height = height;
        group.pixFmt = RK_PIXFMT_YUV420SP;
        group.hdr = RK_HDR_SDR8;
        group.srcFps = -1;
        group.dstFps = -1;
        group.compress = RK_COMPR_NONE;
        if (ret = rk_vpss.fnCreateGroup(_rk_vpss_grp, &group))
            return ret;
        if (ret = rk_vpss.fnSetGroupDevice(_rk_vpss_grp, 1))
            return ret;
    }
    if (ret = rk_vpss.fnStartGroup(_rk_vpss_grp))
        return ret;

    {
        rk_sys_bind source = { .module = RK_SYS_MOD_VI,
            .device = _rk_vi_dev, .channel = _rk_vi_chn };
        rk_sys_bind dest = { .module = RK_SYS_MOD_VPSS,
            .device = _rk_vpss_grp, .channel = 0 };
        if (ret = rk_sys.fnBind(&source, &dest))
            return ret;
    }

    return EXIT_SUCCESS;
}

void rk_pipeline_destroy(void)
{
    for (char grp = 0; grp < RK_VPSS_GRP_NUM; grp++)
    {
        for (char chn = 0; chn < RK_VPSS_CHN_NUM; chn++)
            rk_vpss.fnDisableChannel(grp, chn);

        {
            rk_sys_bind source = { .module = RK_SYS_MOD_VI,
                .device = _rk_vi_dev, .channel = _rk_vi_chn };
            rk_sys_bind dest = { .module = RK_SYS_MOD_VPSS,
                .device = grp, .channel = 0 };
            rk_sys.fnUnbind(&source, &dest);
        }

        rk_vpss.fnStopGroup(grp);
        rk_vpss.fnDestroyGroup(grp);
    }

    rk_vi.fnDisableChannel(_rk_vi_pipe, _rk_vi_chn);

    rk_vi.fnDisableDevice(_rk_vi_dev);

    rk_aiq.fnStop(_rk_aiq_ctx);

    rk_aiq.fnDeinit(_rk_aiq_ctx);
}

/* Collects the VENC channels an OSD region has to be attached to.  The main
 * channel is mandatory (it is the RTSP stream); with osd.scope=all the sub
 * and JPEG channels get their own attachment of the same region handle,
 * following the rkipc pattern. */
static int rk_region_targets(char *targets, int max)
{
    int main = 0, count = 0;

    for (int i = 0; i < RK_VENC_CHN_NUM; i++) {
        if (!rk_state[i].enable) continue;
        if (rk_state[i].role == HAL_CHNROLE_MAIN) { main = i; break; }
    }
    if (max < 1) return 0;
    targets[count++] = main;

    if (app_config.osd_scope_all)
        for (int i = 0; i < RK_VENC_CHN_NUM && count < max; i++) {
            if (!rk_state[i].enable) continue;
            if (rk_state[i].role != HAL_CHNROLE_SUB &&
                rk_state[i].role != HAL_CHNROLE_JPEG) continue;
            targets[count++] = i;
        }

    return count;
}

/* OSD handles are small region ids, but the once-per-handle logging below
 * uses a 32-bit mask: silently skip the log for out-of-range values instead
 * of shifting by an unbound amount. */
static int rk_region_log_once(unsigned int *mask, int handle)
{
    if (handle < 0 || handle >= 32) return 0;
    if (*mask & (1u << (unsigned int)handle)) return 0;
    *mask |= 1u << (unsigned int)handle;
    return 1;
}

/* Text and bitmap overlays are composited after the VENC rotation, so their
 * coordinates live in the encoded picture.  When the picture is rotated, the
 * region must keep the same corner and margin instead of following the scene;
 * nn_map_anchor() implements that rule.  The ePTZ crop does not move the
 * overlay, so only the rotation and the channel size matter here. */
static void rk_region_anchor(short *px, short *py, int channel,
    short w, short h)
{
    nn_geom g;
    int role = HAL_CHNROLE_MAIN;

    if (channel >= 0 && channel < RK_VENC_CHN_NUM)
        role = rk_state[channel].role;

    memset(&g, 0, sizeof(g));
    switch (role) {
        case HAL_CHNROLE_SUB:
            g.src_w = app_config.substream_width ? (int)app_config.substream_width : 640;
            g.src_h = app_config.substream_height ? (int)app_config.substream_height : 360;
            if (app_config.rotate_enable && app_config.rotate_stream)
                g.angle = app_config.rotate_angle;
            break;
        case HAL_CHNROLE_JPEG:
            g.src_w = app_config.jpeg_width ? (int)app_config.jpeg_width : 1920;
            g.src_h = app_config.jpeg_height ? (int)app_config.jpeg_height : 1080;
            break;
        default:
            g.src_w = app_config.mp4_width ? (int)app_config.mp4_width : 1920;
            g.src_h = app_config.mp4_height ? (int)app_config.mp4_height : 1080;
            if (app_config.rotate_enable && !app_config.rotate_stream)
                g.angle = app_config.rotate_angle;
            break;
    }

    if (g.src_w <= 0 || g.src_h <= 0) return;

    {
        int x = *px, y = *py;

        nn_map_anchor(&g, w, h, &x, &y);
        *px = (short)x;
        *py = (short)y;
    }
}

int rk_region_create2(char handle, hal_rect rect, short opacity,
    int module, int device, int channel)
{
    int ret;

    rk_sys_bind target = { .module = module,
        .device = device, .channel = channel };
    rk_rgn_chn attrib, attribCurr;
    // OKO: the RV1106 overlay requires 16-pixel aligned position and size and
    // accepts BGRA5551; text bitmaps are converted in rk_region_setbitmap.
    short px = rect.x, py = rect.y;

    rk_region_anchor(&px, &py, channel, rect.width, rect.height);
    px &= ~15;
    py &= ~15;

    if (rk_rgn.fnGetChannelConfig(handle, &target, &attribCurr)) {
        HAL_INFO("rk_rgn", "Attaching region %d to channel %d...\n",
            handle, channel);
    } else {
        if (attribCurr.overlay.point.x == px &&
            attribCurr.overlay.point.y == py &&
            attribCurr.overlay.fgAlpha == (unsigned int)opacity) {
            // OKO: librockit rejects a second attach of the same region to the
            // same channel; position, size and opacity are unchanged, so there
            // is nothing to do.
            return EXIT_SUCCESS;
        }

        /* OKO: change only the display attributes instead of detaching and
         * reattaching the region.  This is also what applies a new opacity
         * when the position stays the same (previously ignored).  Everything
         * else (layer, show, color LUT) is preserved from GetDisplayAttr. */
        attrib = attribCurr;
        attrib.overlay.point.x = px;
        attrib.overlay.point.y = py;
        attrib.overlay.fgAlpha = (unsigned int)opacity;
        attrib.overlay.bgAlpha = 0;

        ret = rk_rgn.fnSetChannelConfig(handle, &target, &attrib);
        if (!ret || ret == RK_NOTICE_RGN_BUFFER_CHANGE) {
            HAL_INFO("rk_rgn", "Updated display attributes of region %d on "
                "channel %d.\n", handle, channel);
            return EXIT_SUCCESS;
        }

        HAL_WARNING("rk_rgn", "SetDisplayAttr on region %d channel %d failed "
            "with %#x, reattaching\n", handle, channel, ret);
        rk_rgn.fnDetachChannel(handle, &target);
    }

    memset(&attrib, 0, sizeof(attrib));
    attrib.show = 1;
    attrib.type = RK_RGN_TYPE_OVERLAY;
    attrib.overlay.bgAlpha = 0;
    attrib.overlay.fgAlpha = opacity;
    attrib.overlay.point.x = px;
    attrib.overlay.point.y = py;
    attrib.overlay.layer = 7;

    ret = rk_rgn.fnAttachChannel(handle, &target, &attrib);
    if (ret)
        HAL_DANGER("rk_rgn", "Attaching region %d to channel %d failed with %#x!\n",
            handle, channel, ret);

    return ret;
}

int rk_region_create(char handle, hal_rect rect, short opacity)
{
    int ret, count;
    char targets[3] = {0};
    rk_rgn_cnf region, regionCurr;
    // OKO: the RV1106 overlay requires 16-pixel aligned position and size and
    // accepts BGRA5551; text bitmaps are converted in rk_region_setbitmap.
    short pw = (rect.width + 15) & ~15, ph = (rect.height + 15) & ~15;

    if (pw < 16) pw = 16;
    if (ph < 16) ph = 16;

    memset(&region, 0, sizeof(region));
    region.type = RK_RGN_TYPE_OVERLAY;
    region.overlay.pixFmt = RK_PIXFMT_BGRA5551;
    region.overlay.size.width = pw;
    region.overlay.size.height = ph;
    // OKO: use a single canvas; double buffering can keep showing the previous
    // second's OSD until the next update.
    region.overlay.canvas = 1;

    count = rk_region_targets(targets, sizeof(targets));
    if (count < 1) return EXIT_SUCCESS;

    if (rk_rgn.fnGetRegionConfig(handle, &regionCurr)) {
        HAL_INFO("rk_rgn", "Creating region %d...\n", handle);
        if (ret = rk_rgn.fnCreateRegion(handle, &region))
            return ret;
    } else if (regionCurr.overlay.size.height != region.overlay.size.height ||
        regionCurr.overlay.size.width != region.overlay.size.width) {
        HAL_INFO("rk_rgn", "Parameters are different, recreating "
            "region %d...\n", handle);
        /* The region may be attached to more than one channel; drop every
         * attachment before destroying it. */
        for (int i = 0; i < RK_VENC_CHN_NUM; i++) {
            rk_sys_bind target = { .module = RK_SYS_MOD_VENC,
                .device = _rk_venc_dev, .channel = i };
            rk_rgn.fnDetachChannel(handle, &target);
        }
        rk_rgn.fnDestroyRegion(handle);
        if (ret = rk_rgn.fnCreateRegion(handle, &region))
            return ret;
    }

    for (int i = 0; i < count; i++)
        rk_region_create2(handle, rect, opacity, RK_SYS_MOD_VENC,
            _rk_venc_dev, targets[i]);

    return EXIT_SUCCESS;
}

void rk_region_destroy(char handle)
{
    for (int i = 0; i < RK_VENC_CHN_NUM; i++) {
        rk_sys_bind target = { .module = RK_SYS_MOD_VENC,
            .device = _rk_venc_dev, .channel = i };
        rk_rgn.fnDetachChannel(handle, &target);
    }
    rk_rgn.fnDestroyRegion(handle);
}

/* -------------------------------------------------------------------------
 * NN overlay region (detector bounding boxes)
 *
 * The vendor rkipc draws RockIVA boxes into an overlay region in 2BPP and
 * repaints it with RK_MPI_RGN_GetCanvasInfo/UpdateCanvas: one byte holds four
 * pixels and only two palette entries are available, which keeps the whole
 * 1080p canvas at ~518 KiB instead of the ~4.1 MiB a BGRA5551 canvas needs.
 * The region is created by the YOLO capture thread and is not part of osds[].
 * ---------------------------------------------------------------------- */

static int rk_nn_ready;
static int rk_nn_width, rk_nn_height;
static int rk_nn_layer = -1;
static unsigned int rk_nn_lut0, rk_nn_lut1;

int rk_region_nn_supported(void)
{
    static int warned;
    int supported = rk_rgn.fnGetCanvasInfo && rk_rgn.fnUpdateCanvas;

    if (!supported && !warned) {
        HAL_WARNING("rk_rgn", "NN overlay is unavailable: "
            "RK_MPI_RGN_GetCanvasInfo/UpdateCanvas is missing\n");
        warned = 1;
    }
    return supported ? 1 : 0;
}

void rk_region_nn_destroy(void)
{
    if (!rk_nn_ready) return;

    for (int i = 0; i < RK_VENC_CHN_NUM; i++) {
        rk_sys_bind target = { .module = RK_SYS_MOD_VENC,
            .device = _rk_venc_dev, .channel = i };
        rk_rgn.fnDetachChannel(RK_RGN_NN_HANDLE, &target);
    }
    rk_rgn.fnDestroyRegion(RK_RGN_NN_HANDLE);

    rk_nn_ready = 0;
    rk_nn_layer = -1;
}

int rk_region_nn_create(int width, int height, int layer,
    unsigned int lut0, unsigned int lut1)
{
    char targets[3] = {0};
    rk_rgn_cnf region;
    rk_rgn_chn attrib;
    unsigned int pw, ph;
    int ret, count;

    if (!rk_region_nn_supported()) return EXIT_FAILURE;
    if (width < 16 || height < 16 || layer < 0 || layer > 7)
        return EXIT_FAILURE;

    pw = rk_rgn_pad16((unsigned int)width);
    ph = rk_rgn_pad16((unsigned int)height);
    if (pw > 8192 || ph > 8192) return EXIT_FAILURE;

    if (rk_nn_ready && rk_nn_width == (int)pw &&
        rk_nn_height == (int)ph && rk_nn_layer == layer &&
        rk_nn_lut0 == lut0 && rk_nn_lut1 == lut1)
        return EXIT_SUCCESS;

    rk_region_nn_destroy();

    count = rk_region_targets(targets, sizeof(targets));
    if (count < 1) return EXIT_FAILURE;

    memset(&region, 0, sizeof(region));
    region.type = RK_RGN_TYPE_OVERLAY;
    region.overlay.pixFmt = RK_PIXFMT_2BPP;
    region.overlay.size.width = pw;
    region.overlay.size.height = ph;
    /* A single canvas matches the vendor NN overlay; double buffering can
     * keep showing the previous frame until the next UpdateCanvas. */
    region.overlay.canvas = 1;

    if (ret = rk_rgn.fnCreateRegion(RK_RGN_NN_HANDLE, &region)) {
        HAL_DANGER("rk_rgn", "Creating the NN overlay region %d "
            "(%ux%u, 2BPP) failed with %#x!\n", RK_RGN_NN_HANDLE,
            pw, ph, ret);
        return ret;
    }
    rk_nn_ready = 1;

    memset(&attrib, 0, sizeof(attrib));
    attrib.show = 1;
    attrib.type = RK_RGN_TYPE_OVERLAY;
    attrib.overlay.bgAlpha = 0;
    attrib.overlay.fgAlpha = 255;
    attrib.overlay.point.x = 0;
    attrib.overlay.point.y = 0;
    attrib.overlay.layer = (unsigned int)layer;
    attrib.overlay.colorLut[0] = lut0;
    attrib.overlay.colorLut[1] = lut1;

    for (int i = 0; i < count; i++) {
        rk_sys_bind target = { .module = RK_SYS_MOD_VENC,
            .device = _rk_venc_dev, .channel = targets[i] };

        ret = rk_rgn.fnAttachChannel(RK_RGN_NN_HANDLE, &target, &attrib);
        if (ret) {
            HAL_DANGER("rk_rgn", "Attaching the NN overlay region %d to "
                "channel %d failed with %#x!\n", RK_RGN_NN_HANDLE,
                targets[i], ret);
            rk_region_nn_destroy();
            return ret;
        }
    }

    rk_nn_width = (int)pw;
    rk_nn_height = (int)ph;
    rk_nn_layer = layer;
    rk_nn_lut0 = lut0;
    rk_nn_lut1 = lut1;

    HAL_INFO("rk_rgn", "NN overlay region %d created: %ux%u 2BPP on %d "
        "channel(s), layer %d\n", RK_RGN_NN_HANDLE, pw, ph, count, layer);
    return EXIT_SUCCESS;
}

int rk_region_nn_begin(void **addr, unsigned int *stridePx,
    unsigned int *width, unsigned int *height)
{
    char targets[3] = {0};
    rk_rgn_canvas cv;
    unsigned int stride;
    int count, attached = 0;

    if (!rk_nn_ready || !addr || !stridePx || !width || !height)
        return EXIT_FAILURE;

    /* A VENC channel may have been recreated without the process restarting;
     * in that case the canvas still exists but the overlay is detached and
     * would never reach the stream.  Treat "no attachment" as a stale region
     * so the caller rebuilds it. */
    count = rk_region_targets(targets, sizeof(targets));
    for (int i = 0; i < count; i++) {
        rk_sys_bind target = { .module = RK_SYS_MOD_VENC,
            .device = _rk_venc_dev, .channel = targets[i] };
        rk_rgn_chn config;

        memset(&config, 0, sizeof(config));
        if (!rk_rgn.fnGetChannelConfig(RK_RGN_NN_HANDLE, &target, &config))
            attached++;
    }
    if (!attached) return EXIT_FAILURE;

    memset(&cv, 0, sizeof(cv));
    if (rk_rgn.fnGetCanvasInfo(RK_RGN_NN_HANDLE, &cv))
        return EXIT_FAILURE;

    /* Never cache the pointer: the region may have been recreated or the
     * canvas remapped.  The stride comes from the canvas, and a size change
     * (stream restart) is reported as a mismatch so the caller recreates. */
    stride = cv.virWidth ? cv.virWidth : cv.size.width;
    if (cv.pixFmt != RK_PIXFMT_2BPP || !cv.virAddr || !cv.virHeight ||
        cv.size.width < 16 || cv.size.height < 16 ||
        cv.size.width != (unsigned int)rk_nn_width ||
        cv.size.height != (unsigned int)rk_nn_height ||
        stride < cv.size.width || stride > 8192 || (stride & 3))
        return EXIT_FAILURE;

    *addr = (void *)(uintptr_t)cv.virAddr;
    *stridePx = stride;
    *width = cv.size.width;
    *height = cv.size.height;
    return EXIT_SUCCESS;
}

int rk_region_nn_commit(void)
{
    int tries = 0, ret;

    if (!rk_nn_ready) return EXIT_FAILURE;

    do {
        ret = rk_rgn.fnUpdateCanvas(RK_RGN_NN_HANDLE);
    } while (ret == RK_NOTICE_RGN_BUFFER_CHANGE && ++tries < 2);

    return ret ? EXIT_FAILURE : EXIT_SUCCESS;
}

int rk_region_setbitmap(int handle, hal_bitmap *bitmap)
{
    // OKO: RV1106 RGN accepts BGRA5551. The alpha bit position differs between
    // librockit builds (bit 0 vs bit 15), so set both; this keeps non-white
    // text visible.  The region canvas is updated in place through the canvas
    // interface when librockit provides it; SetBitmap remains the fallback.
    // Sizes are padded to the required 16-pixel alignment; padding stays
    // transparent so the visible text does not move.
    unsigned int w = bitmap->dim.width, h = bitmap->dim.height;
    unsigned int aw = rk_rgn_pad16(w), ah = rk_rgn_pad16(h);
    unsigned short *src = bitmap->data;
    uint16_t *dst;
    unsigned int stride = aw, painted = 0;
    static unsigned int logged, canvasFailed;
    rk_rgn_bmp nativeBmp;
    int ret;

    if (!src || !w || !h)
        return EXIT_FAILURE;

    if (rk_rgn.fnGetCanvasInfo && rk_rgn.fnUpdateCanvas) {
        rk_rgn_canvas cv;
        int tries = 0;
        ret = EXIT_FAILURE;

        do {
            memset(&cv, 0, sizeof(cv));
            ret = rk_rgn.fnGetCanvasInfo(handle, &cv);
            if (ret) break;

            /* Never trust the cached pointer: rk_region_create() may have
             * recreated the region, and a SetDisplayAttr can remap the
             * ping-pong buffer.  The stride comes from the canvas, not from
             * the requested size.  The pixel format and the virtual height
             * are checked as well: a mismatched canvas (stale region from a
             * previous process, wrong format) is smaller than the area the
             * packer writes.  The upper bound mirrors the vendor
             * RGN_OVERLAY_MAX_* limits and keeps a broken ABI from turning
             * into an unbounded memset. */
            stride = cv.virWidth ? cv.virWidth : cv.size.width;
            if (cv.pixFmt != RK_PIXFMT_BGRA5551 || !cv.virAddr ||
                !cv.virHeight || cv.virHeight < ah ||
                cv.size.width < aw || cv.size.height < ah || stride < aw ||
                cv.size.width > 8192 || cv.size.height > 8192 || stride > 8192) {
                ret = EXIT_FAILURE;
                break;
            }

            dst = (uint16_t *)(uintptr_t)cv.virAddr;
            memset(dst, 0, (size_t)stride * cv.virHeight * sizeof(*dst));
            painted = rk_rgn_pack_bgra5551(src, w, h, dst, stride);
            ret = rk_rgn.fnUpdateCanvas(handle);
        } while (ret == RK_NOTICE_RGN_BUFFER_CHANGE && ++tries < 2);

        if (!ret) {
            if (rk_region_log_once(&logged, handle))
                HAL_INFO("rk_rgn", "Region %d bitmap (canvas): %ux%u -> %ux%u, "
                    "stride %u, %u px\n", handle, w, h, aw, ah, stride, painted);
            return EXIT_SUCCESS;
        }

        if (rk_region_log_once(&canvasFailed, handle))
            HAL_WARNING("rk_rgn", "Canvas update on region %d failed with %#x, "
                "using the SetBitmap fallback\n", handle, ret);
    }

    stride = aw;
    dst = calloc((size_t)aw * ah, sizeof(*dst));
    if (!dst)
        return EXIT_FAILURE;

    painted = rk_rgn_pack_bgra5551(src, w, h, dst, stride);

    nativeBmp.data = dst;
    nativeBmp.pixFmt = RK_PIXFMT_BGRA5551;
    nativeBmp.size.width = aw;
    nativeBmp.size.height = ah;
    nativeBmp.stride = aw;

    if (rk_region_log_once(&logged, handle))
        HAL_INFO("rk_rgn", "Region %d bitmap (setbitmap): %ux%u -> %ux%u, "
            "%u px\n", handle, w, h, aw, ah, painted);

    ret = rk_rgn.fnSetBitmap(handle, &nativeBmp);
    if (ret)
        HAL_DANGER("rk_rgn", "SetBitmap on region %d (%ux%u, %u px) failed "
            "with %#x!\n", handle, aw, ah, painted, ret);
    free(dst);
    return ret;
}

int rk_sensor_find_v4l2_endpoint(void)
{
    int index = -1;

    for (int i = 0; i < 64; i++)
    {
        char path[256];
        sprintf(path, "/sys/class/video4linux/video%d/name", i);

        FILE* fp = fopen(path, "rb");
        if (!fp) continue;

        char line[32];
        fgets(line, 32, fp);
        fclose(fp);

        if (CONTAINS(line, "rkisp_mainpath"))
        {
            index = i;
            break;
        }
    }

    if (index == -1)
        HAL_ERROR("rk_hal", "Cannot find the corresponding V4L2 ISP device!\n");

    return index;
}

int rk_video_create(char index, hal_vidconfig *config)
{
    int ret;
    rk_venc_chn channel;
    memset(&channel, 0, sizeof(channel));

    channel.gop.mode = config->smartp ?
        RK_VENC_GOPMODE_SMARTP : RK_VENC_GOPMODE_NORMALP;
    channel.gop.virIdrLen = config->virIdr ? config->virIdr : config->gop;
    channel.gop.maxLtrCnt = config->smartp ? 0 : 1;
    channel.gop.tsvcPreload = config->smartp ? 1 : 0;
    if (config->codec == HAL_VIDCODEC_JPG || config->codec == HAL_VIDCODEC_MJPG) {
        channel.attrib.codec = RK_VENC_CODEC_MJPG;
        switch (config->mode) {
            case HAL_VIDMODE_CBR:
                channel.rate.mode = RK_VENC_RATEMODE_MJPGCBR;
                channel.rate.mjpgCbr = (rk_venc_rate_mjpgcbr){ .statTime = 1,
                    .srcFpsNum = config->framerate, .srcFpsDen = 1,
                    .dstFpsNum = config->framerate, .dstFpsDen = 1,
                    .bitrate = config->bitrate }; break;
            case HAL_VIDMODE_VBR:
                channel.rate.mode = RK_VENC_RATEMODE_MJPGVBR;
                channel.rate.mjpgVbr = (rk_venc_rate_mjpgvbr){ .statTime = 1,
                    .srcFpsNum = config->framerate, .srcFpsDen = 1,
                    .dstFpsNum = config->framerate, .dstFpsDen = 1, .bitrate = config->bitrate,
                    .maxBitrate = MAX(config->bitrate, config->maxBitrate),
                    .minBitrate = MIN(config->bitrate, config->maxBitrate) }; break;
            case HAL_VIDMODE_QP:
                channel.rate.mode = RK_VENC_RATEMODE_MJPGQP;
                channel.rate.mjpgQp = (rk_venc_rate_mjpgqp){ .srcFpsNum = config->framerate,
                    .srcFpsDen = 1, .dstFpsNum = config->framerate, .dstFpsDen = 1,
                    .quality = config->maxQual }; break;
            default:
                HAL_ERROR("rk_venc", "MJPEG encoder can only support CBR, VBR or fixed QP modes!");
        }
    } else if (config->codec == HAL_VIDCODEC_H265) {
        channel.attrib.codec = RK_VENC_CODEC_H265;
        channel.attrib.profile = MAX(config->profile, 1);
        switch (config->mode) {
            case HAL_VIDMODE_CBR:
                channel.rate.mode = RK_VENC_RATEMODE_H265CBR;
                channel.rate.h265Cbr = (rk_venc_rate_h26xcbr){ .gop = config->gop,
                    .statTime = 1, .srcFpsNum = config->framerate, .srcFpsDen = 1,
                    .dstFpsNum = config->framerate, .dstFpsDen = 1,
                    .bitrate = config->bitrate }; break;
            case HAL_VIDMODE_VBR:
                channel.rate.mode = RK_VENC_RATEMODE_H265VBR;
                channel.rate.h265Vbr = (rk_venc_rate_h26xvbr){ .gop = config->gop,
                    .statTime = 1, .srcFpsNum = config->framerate, .srcFpsDen = 1,
                    .dstFpsNum = config->framerate, .dstFpsDen = 1, .bitrate = config->bitrate,
                    .maxBitrate = MAX(config->bitrate, config->maxBitrate),
                    .minBitrate = MIN(config->bitrate, config->maxBitrate) }; break;
            case HAL_VIDMODE_QP:
                channel.rate.mode = RK_VENC_RATEMODE_H265QP;
                channel.rate.h265Qp = (rk_venc_rate_h26xqp){ .gop = config->gop,
                    .srcFpsNum = config->framerate, .srcFpsDen = 1,
                    .dstFpsNum = config->framerate, .dstFpsDen = 1, .interQual = config->maxQual,
                    .predQual = config->minQual, .bipredQual = config->minQual }; break;
            case HAL_VIDMODE_AVBR:
                channel.rate.mode = RK_VENC_RATEMODE_H265AVBR;
                channel.rate.h265Avbr = (rk_venc_rate_h26xvbr){ .gop = config->gop,
                    .statTime = 1, .srcFpsNum = config->framerate, .srcFpsDen = 1,
                    .dstFpsNum = config->framerate, .dstFpsDen = 1, .bitrate = config->bitrate,
                    .maxBitrate = MAX(config->bitrate, config->maxBitrate),
                    .minBitrate = MIN(config->bitrate, config->maxBitrate) }; break;
            default:
                HAL_ERROR("rk_venc", "H.265 encoder does not support this mode!");
        }
    } else if (config->codec == HAL_VIDCODEC_H264) {
        channel.attrib.codec = RK_VENC_CODEC_H264;
        switch (config->profile) {
            case 0: channel.attrib.profile = 66; break;
            case 1: channel.attrib.profile = 77; break;
            case 2: channel.attrib.profile = 100; break;
            default: HAL_ERROR("rk_venc", "H.264 encoder does not support this profile!");
        }
        channel.attrib.h264.level = 41;
        switch (config->mode) {
            case HAL_VIDMODE_CBR:
                channel.rate.mode = RK_VENC_RATEMODE_H264CBR;
                channel.rate.h264Cbr = (rk_venc_rate_h26xcbr){ .gop = config->gop,
                    .statTime = 1, .srcFpsNum = config->framerate, .srcFpsDen = 1,
                    .dstFpsNum = config->framerate, .dstFpsDen = 1,
                    .bitrate = config->bitrate }; break;
            case HAL_VIDMODE_VBR:
                channel.rate.mode = RK_VENC_RATEMODE_H264VBR;
                channel.rate.h264Vbr = (rk_venc_rate_h26xvbr){ .gop = config->gop,
                    .statTime = 1, .srcFpsNum = config->framerate, .srcFpsDen = 1,
                    .dstFpsNum = config->framerate, .dstFpsDen = 1, .bitrate = config->bitrate,
                    .maxBitrate = MAX(config->bitrate, config->maxBitrate),
                    .minBitrate = MIN(config->bitrate, config->maxBitrate) }; break;
            case HAL_VIDMODE_QP:
                channel.rate.mode = RK_VENC_RATEMODE_H264QP;
                channel.rate.h264Qp = (rk_venc_rate_h26xqp){ .gop = config->gop,
                    .srcFpsNum = config->framerate, .srcFpsDen = 1,
                    .dstFpsNum = config->framerate, .dstFpsDen = 1, .interQual = config->maxQual,
                    .predQual = config->minQual, .bipredQual = config->minQual }; break;
            case HAL_VIDMODE_AVBR:
                channel.rate.mode = RK_VENC_RATEMODE_H264AVBR;
                channel.rate.h264Avbr = (rk_venc_rate_h26xvbr){ .gop = config->gop,
                    .statTime = 1, .srcFpsNum = config->framerate, .srcFpsDen = 1,
                    .dstFpsNum = config->framerate, .dstFpsDen = 1, .bitrate = config->bitrate,
                    .maxBitrate = MAX(config->bitrate, config->maxBitrate),
                    .minBitrate = MIN(config->bitrate, config->maxBitrate) }; break;
            default:
                HAL_ERROR("rk_venc", "H.264 encoder does not support this mode!");
        }
    } else HAL_ERROR("rk_venc", "This codec is not supported by the hardware!");

    channel.attrib.pixFmt = RK_PIXFMT_YUV420SP;
    channel.attrib.bufSize = config->height * config->width * 3 / 2;
    channel.attrib.byFrame = 1;
    channel.attrib.pic.width = config->width;
    channel.attrib.pic.height = config->height;
    channel.attrib.vir.width = config->width;
    channel.attrib.vir.height = config->height;
    channel.attrib.strmBufCnt = 2;

    if (ret = rk_venc.fnCreateChannel(index, &channel))
        return ret;

    {
        int enable = 1;
        if (ret = rk_venc.fnSetChannelBufferShare(index, &enable))
        return ret;
    }

    {
        int count = -1;
        if (config->codec != HAL_VIDCODEC_JPG &&
            (ret = rk_venc.fnStartReceivingEx(index, &count)))
            return ret;
    }

    /* Optional RV1106 encoder extensions.  Missing symbols leave the
     * corresponding feature off; the API reports it as unavailable. */
    if (config->svc && rk_venc.fnEnableSvc) {
        if (ret = rk_venc.fnEnableSvc(index, 1))
            HAL_DANGER("rk_venc", "Enabling SVC on channel %d failed with %#x!\n",
                index, ret);
    }
    if (config->motionDeblur && rk_venc.fnEnableMotionDeblur) {
        if (ret = rk_venc.fnEnableMotionDeblur(index, 1))
            HAL_DANGER("rk_venc", "Enabling motion deblur on channel %d failed "
                "with %#x!\n", index, ret);
    }
    if ((config->minQp || config->maxQp || config->minIQp || config->maxIQp) &&
        rk_venc.fnSetRcParam) {
        rk_venc_rc_param param;
        memset(&param, 0, sizeof(param));
        if (rk_venc.fnGetRcParam) rk_venc.fnGetRcParam(index, &param);
        if (config->minQp) param.h264.minQp = config->minQp;
        if (config->maxQp) param.h264.maxQp = config->maxQp;
        if (config->minIQp) param.h264.minIQp = config->minIQp;
        if (config->maxIQp) param.h264.maxIQp = config->maxIQp;
        if (ret = rk_venc.fnSetRcParam(index, &param))
            HAL_DANGER("rk_venc", "Setting the QP window on channel %d failed "
                "with %#x!\n", index, ret);
    }

    rk_state[index].payload = config->codec;

    return EXIT_SUCCESS;
}

int rk_video_destroy(char index)
{
    int ret;

    rk_state[index].enable = 0;
    rk_state[index].role = HAL_CHNROLE_NONE;
    rk_state[index].payload = HAL_VIDCODEC_UNSPEC;

    rk_venc.fnStopReceiving(index);

    {
        rk_sys_bind source = { .module = RK_SYS_MOD_VPSS,
            .device = _rk_vpss_grp, .channel = index };
        rk_sys_bind dest = { .module = RK_SYS_MOD_VENC,
            .device = _rk_venc_dev, .channel = index };
        /* Callers may already have unbound the pair; a failure here must not
         * skip the channel teardown. */
        rk_sys.fnUnbind(&source, &dest);
    }

    if (ret = rk_venc.fnDestroyChannel(index))
        return ret;

    if (ret = rk_vpss.fnDisableChannel(_rk_vpss_grp, index))
        return ret;

    return EXIT_SUCCESS;
}

int rk_video_destroy_all(void)
{
    int ret;

    for (char i = 0; i < RK_VENC_CHN_NUM; i++)
        if (rk_state[i].enable)
            if (ret = rk_video_destroy(i))
                return ret;

    return EXIT_SUCCESS;
}

void rk_video_request_idr(char index)
{
    rk_venc.fnRequestIdr(index, 1);
}

int rk_video_snapshot_grab(char index, hal_jpegdata *jpeg)
{
    int ret;

    unsigned int count = 1;
    if (rk_venc.fnStartReceivingEx(index, &count)) {
        HAL_DANGER("rk_venc", "Requesting one frame "
            "%d failed with %#x!\n", index, ret);
        goto abort;
    }

    if (ret = rk_channel_bind(index)) {
        HAL_DANGER("rk_venc", "Binding the encoder channel "
            "%d failed with %#x!\n", index, ret);
        goto abort;
    }

    int fd = rk_venc.fnGetDescriptor(index);

    struct timeval timeout = { .tv_sec = 2, .tv_usec = 0 };
    fd_set readFds;
    FD_ZERO(&readFds);
    FD_SET(fd, &readFds);
    ret = select(fd + 1, &readFds, NULL, NULL, &timeout);
    if (ret < 0) {
        HAL_DANGER("rk_venc", "Select operation failed!\n");
        goto abort;
    } else if (ret == 0) {
        HAL_DANGER("rk_venc", "Capture stream timed out!\n");
        goto abort;
    }

    if (FD_ISSET(fd, &readFds)) {
        rk_venc_stat stat;
        /*if (rk_venc.fnQuery(index, &stat)) {
            HAL_DANGER("rk_venc", "Querying the encoder channel "
                "%d failed with %#x!\n", index, ret);
            goto abort;
        }

        if (!stat.curPacks) {
            HAL_DANGER("rk_venc", "Current frame is empty, skipping it!\n");
            goto abort;
        }*/stat.curPacks = 1;

        rk_venc_strm strm;
        memset(&strm, 0, sizeof(strm));
        rk_venc_pack packs[8];
        if (stat.curPacks > 8)
            strm.packet = (rk_venc_pack*)malloc(sizeof(rk_venc_pack) * stat.curPacks);
        else
            strm.packet = packs;

        if (!strm.packet) {
            HAL_DANGER("rk_venc", "Memory allocation on channel %d failed!\n", index);
            goto abort;
        }
        strm.count = stat.curPacks;

        if (ret = rk_venc.fnGetStream(index, &strm, 40)) {
            HAL_DANGER("rk_venc", "Getting the stream on "
                "channel %d failed with %#x!\n", index, ret);
            goto abort;
        }

        {
            jpeg->jpegSize = 0;
            for (unsigned int i = 0; i < strm.count; i++) {
                rk_venc_pack *pack = &strm.packet[i];
                unsigned int packLen = pack->length;
                unsigned char *packData = rk_mb.fnGetData(pack->mbBlk);

                unsigned int newLen = jpeg->jpegSize + packLen;
                if (newLen > jpeg->length) {
                    jpeg->data = realloc(jpeg->data, newLen);
                    jpeg->length = newLen;
                }
                memcpy(jpeg->data + jpeg->jpegSize, packData, packLen);
                jpeg->jpegSize += packLen;
            }
        }

        rk_venc.fnFreeStream(index, &strm);
    abort:
        if (stat.curPacks > 8 && strm.packet) {
            free(strm.packet);
            strm.packet = NULL;
        }
    }

    rk_venc.fnFreeDescriptor(index);

    rk_channel_unbind(index);

    rk_venc.fnStopReceiving(index);

    return ret;
}

int rk_yolo_source_open(rk_yolo_source source, short width, short height)
{
    int ret;

    if (_rk_yolo_source >= 0) return EXIT_SUCCESS;
    if (width <= 0 || height <= 0) return EXIT_FAILURE;

    _rk_yolo_width = (unsigned short)width;
    _rk_yolo_height = (unsigned short)height;

    switch (source) {
        case RK_YOLO_SRC_VI1: {
            rk_vi_chn channel;
            memset(&channel, 0, sizeof(channel));
            channel.size.width = width;
            channel.size.height = height;
            channel.pixFmt = RK_PIXFMT_YUV420SP;
            channel.compress = RK_COMPR_NONE;
            /* Match the rkipc reference: one queued frame, two ISP buffers
             * and an explicit maximum size.  stMaxSize = 0 on the selfpath
             * has not been exercised by any vendor sample. */
            channel.depth = 1;
            channel.srcFps = -1;
            channel.dstFps = -1;
            channel.ispOpts.bufCount = 2;
            channel.ispOpts.bufSize = width * height * 3 / 2;
            channel.ispOpts.maxSize.width = width;
            channel.ispOpts.maxSize.height = height;
            channel.ispOpts.vidCap = RK_VI_VCAP_VIDEO_MPLANE;
            channel.ispOpts.vidMem = RK_VI_VMEM_DMABUF;
            strcpy(channel.ispOpts.entityName, "rkisp_selfpath");
            if (ret = rk_vi.fnSetChannelConfig(_rk_vi_pipe, 1, &channel)) {
                HAL_DANGER("rk_yolo", "Configuring VI chn1 failed with %#x\n", ret);
                return EXIT_FAILURE;
            }
            if (ret = rk_vi.fnEnableChannel(_rk_vi_pipe, 1)) {
                HAL_DANGER("rk_yolo", "Enabling VI chn1 failed with %#x\n", ret);
                return EXIT_FAILURE;
            }
            _rk_yolo_chn = 1;
            break;
        }
        case RK_YOLO_SRC_VPSS3: {
            rk_vpss_chn channel;
            if (chnState[HAL_CHN_IVS].enable) {
                HAL_DANGER("rk_yolo", "VPSS channel 3 is already used by a stream\n");
                return EXIT_FAILURE;
            }
            memset(&channel, 0, sizeof(channel));
            channel.dest.width = width;
            channel.dest.height = height;
            channel.pixFmt = RK_PIXFMT_YUV420SP;
            channel.hdr = RK_HDR_SDR8;
            channel.compress = RK_COMPR_NONE;
            channel.srcFps = -1;
            channel.dstFps = -1;
            channel.depth = 2;
            if (ret = rk_vpss.fnSetChannelConfig(_rk_vpss_grp, 3, &channel)) {
                HAL_DANGER("rk_yolo", "Configuring VPSS chn3 failed with %#x\n", ret);
                return EXIT_FAILURE;
            }
            if (ret = rk_vpss.fnEnableChannel(_rk_vpss_grp, 3)) {
                HAL_DANGER("rk_yolo", "Enabling VPSS chn3 failed with %#x\n", ret);
                return EXIT_FAILURE;
            }
            _rk_yolo_chn = 3;
            break;
        }
        case RK_YOLO_SRC_VI0:
            /* The depth is applied in rk_pipeline_create() before the channel
             * is enabled; the frame is shared with the VPSS bind.  A detector
             * enabled at runtime after a start with yolo.enable=false cannot
             * use this source until the pipeline is recreated. */
            if (_rk_vi_depth < 1) {
                HAL_DANGER("rk_yolo", "vi0 needs a restart with yolo.enable=true\n");
                return EXIT_FAILURE;
            }
            _rk_yolo_chn = 0;
            break;
        default:
            return EXIT_FAILURE;
    }

    _rk_yolo_source = source;
    HAL_INFO("rk_yolo", "Frame source %s (%dx%d)\n",
        source == RK_YOLO_SRC_VI1 ? "vi1" :
        (source == RK_YOLO_SRC_VPSS3 ? "vpss3" : "vi0"), width, height);
    return EXIT_SUCCESS;
}

int rk_yolo_source_frame(rk_common_frame_info *frame, int timeoutMs)
{
    int ret;

    if (_rk_yolo_source < 0 || !frame) return EXIT_FAILURE;

    if (_rk_yolo_source == RK_YOLO_SRC_VPSS3)
        ret = rk_vpss.fnGetChnFrame(_rk_vpss_grp, _rk_yolo_chn, frame, timeoutMs);
    else
        ret = rk_vi.fnGetChnFrame(_rk_vi_pipe, _rk_yolo_chn, frame, timeoutMs);

    return ret ? EXIT_FAILURE : EXIT_SUCCESS;
}

void rk_yolo_source_release(rk_common_frame_info *frame)
{
    if (_rk_yolo_source < 0 || !frame) return;

    if (_rk_yolo_source == RK_YOLO_SRC_VPSS3)
        rk_vpss.fnReleaseChnFrame(_rk_vpss_grp, _rk_yolo_chn, frame);
    else
        rk_vi.fnReleaseChnFrame(_rk_vi_pipe, _rk_yolo_chn, frame);
}

void rk_yolo_source_close(void)
{
    if (_rk_yolo_source == RK_YOLO_SRC_VI1)
        rk_vi.fnDisableChannel(_rk_vi_pipe, 1);
    else if (_rk_yolo_source == RK_YOLO_SRC_VPSS3)
        rk_vpss.fnDisableChannel(_rk_vpss_grp, 3);

    /* vi0 belongs to the main pipeline and stays enabled. */
    _rk_yolo_source = -1;
    _rk_yolo_chn = -1;
}

int rk_yolo_jpeg_open(short width, short height, int qfactor)
{
    int ret;
    rk_venc_chn channel;
    rk_venc_jpg param;

    if (_rk_yolo_jpeg_ready) return EXIT_SUCCESS;
    if (width <= 0 || height <= 0) return EXIT_FAILURE;

    memset(&channel, 0, sizeof(channel));
    /* MJPG (not the still-JPEG codec): this is the combination the snapshot
     * channel already uses successfully on this image. */
    channel.attrib.codec = RK_VENC_CODEC_MJPG;
    channel.attrib.maxPic.width = width;
    channel.attrib.maxPic.height = height;
    channel.attrib.pixFmt = RK_PIXFMT_YUV420SP;
    channel.attrib.bufSize = 204800;
    channel.attrib.byFrame = 1;
    channel.attrib.pic.width = width;
    channel.attrib.pic.height = height;
    channel.attrib.vir.width = width;
    channel.attrib.vir.height = height;
    channel.attrib.strmBufCnt = 2;
    channel.rate.mode = RK_VENC_RATEMODE_MJPGQP;
    channel.rate.mjpgQp = (rk_venc_rate_mjpgqp){ .srcFpsNum = 10,
        .srcFpsDen = 1, .dstFpsNum = 10, .dstFpsDen = 1,
        .quality = qfactor ? (unsigned int)qfactor : 80 };
    channel.gop.mode = RK_VENC_GOPMODE_NORMALP;
    channel.gop.maxLtrCnt = 1;

    if (ret = rk_venc.fnCreateChannel(RK_YOLO_JPEG_CHN, &channel)) {
        HAL_DANGER("rk_yolo", "Creating the event JPEG channel failed with %#x\n", ret);
        return EXIT_FAILURE;
    }

    memset(&param, 0, sizeof(param));
    param.quality = qfactor ? (unsigned int)qfactor : 80;
    rk_venc.fnSetJpegParam(RK_YOLO_JPEG_CHN, &param);

    {
        int count = -1;
        if (ret = rk_venc.fnStartReceivingEx(RK_YOLO_JPEG_CHN, &count)) {
            HAL_DANGER("rk_yolo", "Starting the event JPEG channel failed with %#x\n", ret);
            rk_venc.fnDestroyChannel(RK_YOLO_JPEG_CHN);
            return EXIT_FAILURE;
        }
    }

    if (ret = rk_mb.fnAlloc(&_rk_yolo_jpeg_blk, NULL, NULL,
            (unsigned int)width * height * 3 / 2)) {
        HAL_DANGER("rk_yolo", "Allocating the JPEG frame buffer failed with %#x\n", ret);
        _rk_yolo_jpeg_blk = NULL;
        rk_venc.fnStopReceiving(RK_YOLO_JPEG_CHN);
        rk_venc.fnDestroyChannel(RK_YOLO_JPEG_CHN);
        return EXIT_FAILURE;
    }

    _rk_yolo_jpeg_width = (unsigned short)width;
    _rk_yolo_jpeg_height = (unsigned short)height;
    _rk_yolo_jpeg_ready = 1;
    return EXIT_SUCCESS;
}

int rk_yolo_jpeg_encode(const void *nv12, hal_jpegdata *jpeg)
{
    int ret;
    rk_common_frame_info info;
    rk_venc_strm strm;
    rk_venc_pack packs[32];

    if (!_rk_yolo_jpeg_ready || !nv12 || !jpeg) return EXIT_FAILURE;

    {
        unsigned int size = _rk_yolo_jpeg_width * _rk_yolo_jpeg_height * 3 / 2;
        memcpy(rk_mb.fnGetData(_rk_yolo_jpeg_blk), nv12, size);
    }

    memset(&info, 0, sizeof(info));
    info.frame.mbBlk = _rk_yolo_jpeg_blk;
    info.frame.width = _rk_yolo_jpeg_width;
    info.frame.height = _rk_yolo_jpeg_height;
    info.frame.virWidth = _rk_yolo_jpeg_width;
    info.frame.virHeight = _rk_yolo_jpeg_height;
    info.frame.pixelFormat = RK_PIXFMT_YUV420SP;
    info.frame.compressMode = RK_COMPR_NONE;

    if (ret = rk_venc.fnSendFrame(RK_YOLO_JPEG_CHN, &info, 1000)) {
        HAL_DANGER("rk_yolo", "Sending a frame to the event encoder failed with %#x\n", ret);
        return EXIT_FAILURE;
    }

    memset(&strm, 0, sizeof(strm));
    memset(packs, 0, sizeof(packs));
    strm.packet = packs;
    strm.count = sizeof(packs) / sizeof(packs[0]);

    if (ret = rk_venc.fnGetStream(RK_YOLO_JPEG_CHN, &strm, 1000)) {
        HAL_DANGER("rk_yolo", "Getting the event JPEG failed with %#x\n", ret);
        return EXIT_FAILURE;
    }

    jpeg->jpegSize = 0;
    for (unsigned int i = 0; i < strm.count; i++) {
        unsigned int len = strm.packet[i].length;
        unsigned char *data = (unsigned char *)rk_mb.fnGetData(
            strm.packet[i].mbBlk);
        unsigned int total = jpeg->jpegSize + len;

        if (total > jpeg->length) {
            unsigned char *grown = realloc(jpeg->data, total);
            if (!grown) {
                rk_venc.fnFreeStream(RK_YOLO_JPEG_CHN, &strm);
                return EXIT_FAILURE;
            }
            jpeg->data = grown;
            jpeg->length = total;
        }
        if (data) memcpy(jpeg->data + jpeg->jpegSize, data, len);
        jpeg->jpegSize += len;
    }

    rk_venc.fnFreeStream(RK_YOLO_JPEG_CHN, &strm);
    return EXIT_SUCCESS;
}

void rk_yolo_jpeg_close(void)
{
    if (!_rk_yolo_jpeg_ready) return;

    _rk_yolo_jpeg_ready = 0;
    rk_venc.fnStopReceiving(RK_YOLO_JPEG_CHN);
    rk_venc.fnDestroyChannel(RK_YOLO_JPEG_CHN);
    if (_rk_yolo_jpeg_blk) {
        rk_mb.fnFree(_rk_yolo_jpeg_blk);
        _rk_yolo_jpeg_blk = NULL;
    }
}

void *rk_video_thread(void)
{
    int ret;
    int maxFd = 0;

    for (int i = 0; i < RK_VENC_CHN_NUM; i++) {
        if (!rk_state[i].enable) continue;
        if (!rk_state[i].mainLoop) continue;

        ret = rk_venc.fnGetDescriptor(i);
        if (ret < 0) {
            HAL_DANGER("rk_venc", "Getting the encoder descriptor failed with %#x!\n", ret);
            return NULL;
        }
        rk_state[i].fileDesc = ret;

        if (maxFd <= rk_state[i].fileDesc)
            maxFd = rk_state[i].fileDesc;
    }

    rk_venc_stat stat;
    rk_venc_strm stream;
    struct timeval timeout;
    fd_set readFds;

    while (keepRunning) {
        FD_ZERO(&readFds);
        for(int i = 0; i < RK_VENC_CHN_NUM; i++) {
            if (!rk_state[i].enable) continue;
            if (!rk_state[i].mainLoop) continue;
            FD_SET(rk_state[i].fileDesc, &readFds);
        }

        timeout.tv_sec = 2;
        timeout.tv_usec = 0;
        ret = select(maxFd + 1, &readFds, NULL, NULL, &timeout);
        if (ret < 0) {
            HAL_DANGER("rk_venc", "Select operation failed!\n");
            break;
        } else if (ret == 0) {
            HAL_WARNING("rk_venc", "Main stream loop timed out!\n");
            continue;
        } else {
            for (int i = 0; i < RK_VENC_CHN_NUM; i++) {
                if (!rk_state[i].enable) continue;
                if (!rk_state[i].mainLoop) continue;
                if (FD_ISSET(rk_state[i].fileDesc, &readFds)) {
                    memset(&stream, 0, sizeof(stream));

                    /*if (ret = rk_venc.fnQuery(i, &stat)) {
                        HAL_DANGER("rk_venc", "Querying the encoder channel "
                            "%d failed with %#x!\n", i, ret);
                        break;
                    }

                    if (!stat.curPacks) {
                        HAL_INFO("rk_venc", "Current frame is empty, skipping it!\n");
                        continue;
                    }*/stat.curPacks = 1;

                    rk_venc_pack packs[8];
                    if (stat.curPacks > 8)
                        stream.packet = (rk_venc_pack*)malloc(sizeof(rk_venc_pack) * stat.curPacks);
                    else
                        stream.packet = packs;

                    if (!stream.packet) {
                        HAL_DANGER("rk_venc", "Memory allocation on channel %d failed!\n", i);
                        break;
                    }
                    stream.count = stat.curPacks;

                    if (ret = rk_venc.fnGetStream(i, &stream, 40)) {
                        HAL_DANGER("rk_venc", "Getting the stream on "
                            "channel %d failed with %#x!\n", i, ret);
                        if (stat.curPacks > 8) free(stream.packet);
                        break;
                    }

                    if (rk_vid_cb) {
                        hal_vidstream outStrm;
                        hal_vidpack outPack[stream.count];
                        outStrm.count = stream.count;
                        outStrm.seq = stream.sequence;
                        for (int j = 0; j < stream.count; j++) {
                            rk_venc_pack *pack = &stream.packet[j];
                            outPack[j].data = rk_mb.fnGetData(pack->mbBlk);
                            outPack[j].length = pack->length;
                            outPack[j].naluCnt = 0;
                            outPack[j].nalu[0].length = pack->length;
                            outPack[j].nalu[0].offset = 0;
                            switch (rk_state[i].payload) {
                                case HAL_VIDCODEC_H264:
                                    if (pack->naluType.h264Nalu != RK_VENC_NALU_H264_IDRSLICE) {
                                        unsigned int n = 0, sc, scanOff = 0;
                                        unsigned int pktLen = outPack[j].length;
                                        while ((sc = nal_find_startcode(outPack[j].data, scanOff, pktLen)) < pktLen) {
                                            unsigned int scLen = (outPack[j].data[sc + 2] == 1) ? 3 : 4;
                                            outPack[0].nalu[n].type = outPack[j].data[sc + scLen] & 0x1F;
                                            outPack[0].nalu[n].offset = sc;
                                            n++;
                                            scanOff = sc + scLen;
                                        }
                                        outPack[0].naluCnt = n;
                                        outPack[0].nalu[n].offset = pktLen;
                                        for (n = 0; n < outPack[0].naluCnt; n++)
                                            outPack[0].nalu[n].length =
                                                outPack[0].nalu[n + 1].offset -
                                                outPack[0].nalu[n].offset;
                                    }
                                    else outPack[j].nalu[outPack[j].naluCnt++].type = pack->naluType.h264Nalu;
                                    break;
                                case HAL_VIDCODEC_H265:
                                    outPack[j].nalu[outPack[j].naluCnt++].type = pack->naluType.h265Nalu;
                                    break;
                            }
                            outPack[j].offset = 0;
                            outPack[j].timestamp = pack->timestamp;
                        }
                        outStrm.pack = outPack;
                        (*rk_vid_cb)(i, &outStrm);
                    }

                    if (ret = rk_venc.fnFreeStream(i, &stream)) {
                        HAL_WARNING("rk_venc", "Releasing the stream on "
                            "channel %d failed with %#x!\n", i, ret);
                    }
                    if (stat.curPacks > 8) free(stream.packet);
                }
            }
        }
    }
    HAL_INFO("rk_venc", "Shutting down encoding thread...\n");
}

void rk_system_deinit(void)
{
    rk_sys.fnExit();
}

int rk_system_init(char *snrConfig)
{
    int ret;

    printf("App built with headers v%s\n", RK_SYS_API);

    rk_sys.fnExit();

    if (ret = rk_sys.fnInit())
        return ret;

    return EXIT_SUCCESS;
}

int rk_system_identify(void)
{
    if (access("/proc/rk_cma", F_OK))
        return 0;

    plat = HAL_PLATFORM_RK;
    strcpy(chip, "rv11xx");
    strcpy(family, "rockchip");
    /* One VENC channel is backed by the VPSS channel with the same index.
     * RV1106 accepts VPSS channels 0..4, and 4 is reserved for IVS, so four
     * VENC channels (main, sub, MJPEG, JPEG) are available. */
    chnCount = HAL_CHN_COUNT;
    chnState = (hal_chnstate*)rk_state;
    aud_thread = rk_audio_thread;
    vid_thread = rk_video_thread;
    return 1;
}

int rk_vpss_set_chn_crop(char channel, hal_crop *crop)
{
    rk_vpss_crop cfg;
    static int cropWarned;
    int ret;

    if (!crop || !rk_vpss.fnSetChnCrop) return -1;

    memset(&cfg, 0, sizeof(cfg));
    cfg.enable = crop->enable ? 1 : 0;
    cfg.coordinate = 1;         /* absolute pixels in the group input */
    cfg.rect.x = crop->rect.x;
    cfg.rect.y = crop->rect.y;
    cfg.rect.width = crop->rect.width;
    cfg.rect.height = crop->rect.height;

    ret = rk_vpss.fnSetChnCrop(_rk_vpss_grp, channel, &cfg);
    if (ret && !cropWarned) {
        HAL_DANGER("rk_vpss", "SetChnCrop on channel %d failed with %#x\n",
            (int)channel, ret);
        cropWarned = 1;
    }
    /* The numeric MPP code is returned to the caller (0 = applied) so that
     * /api/eptz can distinguish a refusal from a successful crop. */
    return ret;
}

int rk_vpss_set_chn_rotation(char channel, hal_rotation rotation)
{
    int value = 0;

    if (!rk_vpss.fnSetChnRotation) return EXIT_FAILURE;

    switch (rotation) {
        case HAL_ROTATE_90:  value = 1; break;
        case HAL_ROTATE_180: value = 2; break;
        case HAL_ROTATE_270: value = 3; break;
        default:             value = 0; break;
    }

    return rk_vpss.fnSetChnRotation(_rk_vpss_grp, channel, value)
        ? EXIT_FAILURE : EXIT_SUCCESS;
}

/* Applies the encoder extensions that support a running channel.  SmartP is
 * deliberately absent: the GOP mode is part of the channel attribute and is
 * picked up on the next start. */
int rk_video_apply_extras(char channel, const hal_vidconfig *config)
{
    int ret = EXIT_SUCCESS;

    if (rk_venc.fnEnableSvc) {
        if (rk_venc.fnEnableSvc(channel, config->svc ? 1 : 0))
            ret = EXIT_FAILURE;
    } else if (config->svc) {
        ret = EXIT_FAILURE;
    }

    if (rk_venc.fnEnableMotionDeblur) {
        if (rk_venc.fnEnableMotionDeblur(channel, config->motionDeblur ? 1 : 0))
            ret = EXIT_FAILURE;
    } else if (config->motionDeblur) {
        ret = EXIT_FAILURE;
    }

    if ((config->minQp || config->maxQp || config->minIQp || config->maxIQp) &&
        rk_venc.fnSetRcParam) {
        rk_venc_rc_param param;
        memset(&param, 0, sizeof(param));
        if (rk_venc.fnGetRcParam) rk_venc.fnGetRcParam(channel, &param);
        if (config->minQp) param.h264.minQp = config->minQp;
        if (config->maxQp) param.h264.maxQp = config->maxQp;
        if (config->minIQp) param.h264.minIQp = config->minIQp;
        if (config->maxIQp) param.h264.maxIQp = config->maxIQp;
        if (rk_venc.fnSetRcParam(channel, &param))
            ret = EXIT_FAILURE;
    }

    return ret;
}

int rk_video_set_rotation(char channel, hal_rotation rotation)
{
    int value = 0;

    if (!rk_venc.fnSetChnRotation) return EXIT_FAILURE;

    switch (rotation) {
        case HAL_ROTATE_90:  value = 1; break;
        case HAL_ROTATE_180: value = 2; break;
        case HAL_ROTATE_270: value = 3; break;
        default:             value = 0; break;
    }

    return rk_venc.fnSetChnRotation(channel, value)
        ? EXIT_FAILURE : EXIT_SUCCESS;
}

int rk_vpss_get_chn_rotation(char channel, hal_rotation *rotation)
{
    int value = 0;

    if (!rk_vpss.fnGetChnRotation) return EXIT_FAILURE;
    if (rk_vpss.fnGetChnRotation(_rk_vpss_grp, channel, &value))
        return EXIT_FAILURE;

    *rotation = (hal_rotation)value;
    return EXIT_SUCCESS;
}

/* AINR state change callback: the model is expensive, so while it is active
 * the main/sub encoders are throttled to aiisp.fps_enabled and restored to
 * aiisp.fps_disabled when the model is bypassed. */
static int rk_aiisp_update(void *param, void *priv)
{
    rk_aiq_ainr *ainr = param;
    int fps = (ainr && ainr->enable) ?
        app_config.aiisp_fps_enabled : app_config.aiisp_fps_disabled;

    (void)priv;
    if (fps < 1) fps = 1;

    for (int i = 0; i < RK_VENC_CHN_NUM; i++) {
        rk_venc_para para;
        if (!rk_state[i].enable) continue;
        if (rk_state[i].role != HAL_CHNROLE_MAIN &&
            rk_state[i].role != HAL_CHNROLE_SUB) continue;
        memset(&para, 0, sizeof(para));
        if (rk_venc.fnGetChannelParam(i, &para)) continue;
        para.frateOn = 1;
        para.srcFpsNum = fps;
        para.srcFpsDen = 1;
        para.dstFpsNum = fps;
        para.dstFpsDen = 1;
        rk_venc.fnSetChannelParam(i, &para);
    }

    return 0;
}

int rk_group_set_aiisp(hal_aiisp_config *config)
{
    rk_vpss_aiisp cfg;
    /* librockit keeps pModelFilePath for later use, so it must outlive this
     * call; the caller's config lives on the stack. */
    static char modelPath[192];

    if (!rk_vpss.fnSetGrpAIISPAttr) return EXIT_FAILURE;

    snprintf(modelPath, sizeof(modelPath), "%s", config->model);

    memset(&cfg, 0, sizeof(cfg));
    cfg.enable = config->mode != HAL_AIISP_OFF;
    cfg.model = cfg.enable ? modelPath : NULL;
    cfg.bufCnt = config->bufCnt ? config->bufCnt : 1;
    cfg.func.callback = (void *)rk_aiisp_update;

    {
        int ret = rk_vpss.fnSetGrpAIISPAttr(_rk_vpss_grp, &cfg);
        if (ret) {
            HAL_DANGER("rk_aiisp", "SetGrpAIISPAttr failed with %#x!\n", ret);
            return EXIT_FAILURE;
        }
    }
    if (!cfg.enable) rk_aiisp_update(NULL, NULL);
    return EXIT_SUCCESS;
}

int rk_aiq_switch_scene(const char *main, const char *sub)
{
    if (!_rk_aiq_ctx || !rk_aiq.fnSwitchScene) return EXIT_FAILURE;
    return rk_aiq.fnSwitchScene(_rk_aiq_ctx, main, sub)
        ? EXIT_FAILURE : EXIT_SUCCESS;
}

int rk_aiq_update_iq(const char *file)
{
    if (!_rk_aiq_ctx || !rk_aiq.fnUpdateIq || !file) return EXIT_FAILURE;
    return rk_aiq.fnUpdateIq(_rk_aiq_ctx, (char *)file)
        ? EXIT_FAILURE : EXIT_SUCCESS;
}

int rk_aiq_set_work_mode(int mode)
{
    rk_aiq_work_mode work;

    if (!_rk_aiq_ctx || !rk_aiq.fnSwWorkingModeDyn) return EXIT_FAILURE;
    work.mode = mode;
    return rk_aiq.fnSwWorkingModeDyn(_rk_aiq_ctx, &work)
        ? EXIT_FAILURE : EXIT_SUCCESS;
}

static char ivsCreated;
static rk_ivs_result_info ivsResultInfo;

int rk_ivs_create(hal_ivs_config *config)
{
    int ret;

    if (!rk_ivs.fnCreateChn) return EXIT_FAILURE;
    if (ivsCreated) return EXIT_SUCCESS;

    {
        rk_vpss_chn channel;
        memset(&channel, 0, sizeof(channel));
        channel.chnMode = RK_VPSS_CMODE_AUTO;
        channel.dest.width = config->width;
        channel.dest.height = config->height;
        channel.pixFmt = RK_PIXFMT_YUV420SP;
        channel.hdr = RK_HDR_SDR8;
        channel.srcFps = -1;
        channel.dstFps = -1;
        channel.depth = 1;
        channel.privFrmBufCnt = 1;
        if (ret = rk_vpss.fnSetChannelConfig(_rk_vpss_grp, HAL_CHN_IVS, &channel)) {
            HAL_DANGER("rk_ivs", "Configuring VPSS channel %d failed with %#x!\n",
                HAL_CHN_IVS, ret);
            return ret;
        }
        if (ret = rk_vpss.fnEnableChannel(_rk_vpss_grp, HAL_CHN_IVS)) {
            HAL_DANGER("rk_ivs", "Enabling VPSS channel %d failed with %#x!\n",
                HAL_CHN_IVS, ret);
            return ret;
        }
    }

    {
        rk_ivs_chn_attr attr;
        memset(&attr, 0, sizeof(attr));
        attr.mode = (int)config->mode;
        attr.width = config->width;
        attr.height = config->height;
        attr.pixFmt = 0;        /* RK_FMT_YUV420SP */
        attr.gop = 30;
        attr.mdEnable = (config->mode & HAL_IVS_MD) ? 1 : 0;
        attr.mdInterval = config->mdInterval ? config->mdInterval : 5;
        attr.mdNightMode = 1;
        attr.mdSensitivity = config->sensitivity ? config->sensitivity : 2;
        attr.odEnable = (config->mode & HAL_IVS_OD) ? 1 : 0;
        attr.odInterval = config->odInterval ? config->odInterval : 1;
        attr.odPercent = config->odPercent ? config->odPercent : 7;
        attr.maxWidth = config->width;
        attr.maxHeight = config->height;

        if (ret = rk_ivs.fnCreateChn(0, &attr)) {
            HAL_DANGER("rk_ivs", "Creating IVS channel failed with %#x!\n", ret);
            rk_vpss.fnDisableChannel(_rk_vpss_grp, HAL_CHN_IVS);
            return ret;
        }
    }

    {
        rk_sys_bind source = { .module = RK_SYS_MOD_VPSS,
            .device = _rk_vpss_grp, .channel = HAL_CHN_IVS };
        rk_sys_bind dest = { .module = RK_SYS_MOD_IVS,
            .device = 0, .channel = 0 };
        if (ret = rk_sys.fnBind(&source, &dest)) {
            HAL_DANGER("rk_ivs", "Binding VPSS to IVS failed with %#x!\n", ret);
            rk_ivs.fnDestroyChn(0);
            rk_vpss.fnDisableChannel(_rk_vpss_grp, HAL_CHN_IVS);
            return ret;
        }
    }

    ivsCreated = 1;
    HAL_INFO("rk_ivs", "IVS enabled (%s) on VPSS channel %d\n",
        config->mode == HAL_IVS_MD ? "MD" :
        config->mode == HAL_IVS_OD ? "OD" : "MD+OD", HAL_CHN_IVS);
    return EXIT_SUCCESS;
}

int rk_ivs_set_config(hal_ivs_config *config)
{
    rk_ivs_chn_attr attr;

    if (!ivsCreated || !rk_ivs.fnSetChnAttr) return EXIT_FAILURE;

    memset(&attr, 0, sizeof(attr));
    if (rk_ivs.fnGetChnAttr && !rk_ivs.fnGetChnAttr(0, &attr)) {
        /* get succeeded, keep the rest */
    }
    attr.mode = (int)config->mode;
    attr.mdEnable = (config->mode & HAL_IVS_MD) ? 1 : 0;
    attr.mdInterval = config->mdInterval ? config->mdInterval : 5;
    attr.mdSensitivity = config->sensitivity ? config->sensitivity : 2;
    attr.odEnable = (config->mode & HAL_IVS_OD) ? 1 : 0;
    attr.odInterval = config->odInterval ? config->odInterval : 1;
    attr.odPercent = config->odPercent ? config->odPercent : 7;

    return rk_ivs.fnSetChnAttr(0, &attr) ? EXIT_FAILURE : EXIT_SUCCESS;
}

int rk_ivs_get_results(hal_ivs_result *result, int timeoutMs)
{
    int count;

    memset(result, 0, sizeof(*result));
    if (!ivsCreated || !rk_ivs.fnGetResults) return EXIT_FAILURE;

    memset(&ivsResultInfo, 0, sizeof(ivsResultInfo));
    if (rk_ivs.fnGetResults(0, &ivsResultInfo, timeoutMs) != 0)
        return EXIT_FAILURE;
    if (!ivsResultInfo.results || ivsResultInfo.resultNum < 1) {
        if (ivsResultInfo.results) rk_ivs_release_results();
        return EXIT_FAILURE;
    }

    result->frameId = ivsResultInfo.results[0].md.frameId;
    result->rectCount = ivsResultInfo.results[0].md.rectNum;
    if (result->rectCount > sizeof(result->rect) / sizeof(result->rect[0]))
        result->rectCount = sizeof(result->rect) / sizeof(result->rect[0]);
    for (unsigned int i = 0; i < result->rectCount; i++) {
        rk_common_rect *r = &ivsResultInfo.results[0].md.rect[i];
        result->rect[i].x = r->x;
        result->rect[i].y = r->y;
        result->rect[i].width = r->width;
        result->rect[i].height = r->height;
    }
    result->odPercent = ivsResultInfo.results[0].od.pixSum;
    count = result->rectCount + (ivsResultInfo.results[0].od.flag ? 1 : 0);
    return count > 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

void rk_ivs_release_results(void)
{
    if (ivsCreated && rk_ivs.fnReleaseResults)
        rk_ivs.fnReleaseResults(0, &ivsResultInfo);
    memset(&ivsResultInfo, 0, sizeof(ivsResultInfo));
}

void rk_ivs_destroy(void)
{
    if (!ivsCreated) return;

    {
        rk_sys_bind source = { .module = RK_SYS_MOD_VPSS,
            .device = _rk_vpss_grp, .channel = HAL_CHN_IVS };
        rk_sys_bind dest = { .module = RK_SYS_MOD_IVS,
            .device = 0, .channel = 0 };
        rk_sys.fnUnbind(&source, &dest);
    }
    rk_ivs.fnDestroyChn(0);
    rk_vpss.fnDisableChannel(_rk_vpss_grp, HAL_CHN_IVS);
    if (rk_sys.fnWaitFreeMB) rk_sys.fnWaitFreeMB();

    ivsCreated = 0;
    HAL_INFO("rk_ivs", "IVS disabled\n");
}

#endif
