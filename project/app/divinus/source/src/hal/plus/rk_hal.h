#pragma once

#include "rk_common.h"
#include "rk_aiq.h"
#include "rk_aud.h"
#include "rk_ivs.h"
#include "rk_mb.h"
#include "rk_rgn.h"
#include "rk_rknn.h"
#include "rk_sys.h"
#include "rk_venc.h"
#include "rk_vi.h"
#include "rk_vpss.h"

#include "../globals.h"
#include "../../fmt/nal.h"

#include <fcntl.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <unistd.h>

extern hal_chnstate rk_state[RK_VENC_CHN_NUM];
extern int (*rk_aud_cb)(hal_audframe*);
extern int (*rk_vid_cb)(char, hal_vidstream*);
extern rk_mb_impl rk_mb;
extern rk_rknn_impl rk_rknn;

/* YOLO frame source: the capture thread pulls NV12 frames from the channel
 * selected by yolo.source.  VI chn1 (selfpath) is the default; VPSS chn3 is
 * the AIISP-processed alternative and VI chn0 is the emergency fallback. */
typedef enum {
    RK_YOLO_SRC_VI1 = 0,
    RK_YOLO_SRC_VPSS3 = 1,
    RK_YOLO_SRC_VI0 = 2
} rk_yolo_source;

int rk_yolo_source_open(rk_yolo_source source, short width, short height);
int rk_yolo_source_frame(rk_common_frame_info *frame, int timeoutMs);
void rk_yolo_source_release(rk_common_frame_info *frame);
void rk_yolo_source_close(void);

/* Dedicated JPEG encoder used for event frames.  It is not bound to a VPSS
 * channel: buffered NV12 frames are pushed with RK_MPI_VENC_SendFrame, which
 * keeps the OSD/region path of the streaming channels untouched. */
int rk_yolo_jpeg_open(short width, short height, int qfactor);
int rk_yolo_jpeg_encode(const void *nv12, hal_jpegdata *jpeg);
void rk_yolo_jpeg_close(void);

void rk_hal_deinit(void);
int rk_hal_init(void);

void rk_audio_deinit(void);
int rk_audio_init(int samplerate);
void *rk_audio_thread(void);

int rk_channel_bind(char index);
int rk_channel_create(char index, short width, short height, char mirror, char flip);
int rk_channel_grayscale(char enable);
int rk_channel_unbind(char index);

int rk_pipeline_create(short width, short height);
void rk_pipeline_destroy(void);

int rk_vpss_set_chn_crop(char channel, hal_crop *crop);
int rk_vpss_set_chn_rotation(char channel, hal_rotation rotation);
int rk_vpss_get_chn_rotation(char channel, hal_rotation *rotation);
int rk_video_set_rotation(char channel, hal_rotation rotation);
int rk_video_apply_extras(char channel, const hal_vidconfig *config);
int rk_group_set_aiisp(hal_aiisp_config *config);

int rk_aiq_switch_scene(const char *main, const char *sub);
int rk_aiq_update_iq(const char *file);
int rk_aiq_set_work_mode(int mode);

int rk_ivs_create(hal_ivs_config *config);
int rk_ivs_set_config(hal_ivs_config *config);
int rk_ivs_get_results(hal_ivs_result *result, int timeoutMs);
void rk_ivs_release_results(void);
void rk_ivs_destroy(void);

int rk_region_create(char handle, hal_rect rect, short opacity);
int rk_region_create2(char handle, hal_rect rect, short opacity,
    int module, int device, int channel);
void rk_region_destroy(char handle);
int rk_region_setbitmap(int handle, hal_bitmap *bitmap);

int rk_sensor_find_v4l2_endpoint(void);

int rk_video_create(char index, hal_vidconfig *config);
int rk_video_destroy(char index);
int rk_video_destroy_all(void);
void rk_video_request_idr(char index);
int rk_video_snapshot_grab(char index, hal_jpegdata *jpeg);
void *rk_video_thread(void);

int rk_system_calculate_block(short width, short height, rk_common_pixfmt pixFmt,
    unsigned int alignWidth);
void rk_system_deinit(void);
int rk_system_init(char *snrConfig);

int rk_system_identify(void);
