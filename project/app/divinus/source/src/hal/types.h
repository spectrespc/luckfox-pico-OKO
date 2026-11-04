#pragma once

#include "macros.h"

typedef enum {
    HAL_PLATFORM_UNK,
    HAL_PLATFORM_AK,
    HAL_PLATFORM_CVI,
    HAL_PLATFORM_F1,
    HAL_PLATFORM_F2,
    HAL_PLATFORM_GM,
    HAL_PLATFORM_I3,
    HAL_PLATFORM_I6,
    HAL_PLATFORM_I6C,
    HAL_PLATFORM_M6,
    HAL_PLATFORM_RK,
    HAL_PLATFORM_T31,
    HAL_PLATFORM_V1,
    HAL_PLATFORM_V2,
    HAL_PLATFORM_V3,
    HAL_PLATFORM_V4
} hal_platform;

typedef enum {
    OP_READ = 0b1,
    OP_WRITE = 0b10,
    OP_MODIFY = 0b11
} hal_register_op;

typedef enum {
    HAL_AUDCODEC_UNSPEC,
    HAL_AUDCODEC_MP3 = 0x69,
    HAL_AUDCODEC_NONE = 0xFF
} hal_audcodec;

typedef enum {
    HAL_VIDCODEC_UNSPEC,
    HAL_VIDCODEC_H264,
    HAL_VIDCODEC_H265,
    HAL_VIDCODEC_MJPG,
    HAL_VIDCODEC_JPG
} hal_vidcodec;

typedef enum {
    HAL_VIDMODE_CBR,
    HAL_VIDMODE_VBR,
    HAL_VIDMODE_QP,
    HAL_VIDMODE_ABR,
    HAL_VIDMODE_AVBR,
    HAL_VIDMODE_END
} hal_vidmode;

typedef enum {
    HAL_VIDPROFILE_BASELINE,
    HAL_VIDPROFILE_MAIN,
    HAL_VIDPROFILE_HIGH
} hal_vidprofile;

typedef enum {
    HAL_LIGHT_NONE  = 0,
    HAL_LIGHT_IR    = 1 << 0,
    HAL_LIGHT_WHITE = 1 << 1
} hal_light;

typedef struct {
    unsigned char *data[8];
    unsigned int length[8];
    unsigned int seq;
    unsigned int timestamp;
    char channelCnt;
    char interleavedOn;
} hal_audframe;

typedef enum {
    HAL_CHNROLE_NONE,
    HAL_CHNROLE_MAIN,
    HAL_CHNROLE_SUB,
    HAL_CHNROLE_JPEG,
    HAL_CHNROLE_MJPG,
    HAL_CHNROLE_IVS
} hal_chnrole;

/* RV1106 exposes four VPSS output channels (VPSS_MAX_CHN_NUM == 4).  IVS
 * takes the last one while it is enabled; the VENC pool then has 0..2, which
 * is enough for main, sub and JPEG, but not for MJPEG at the same time. */
#define HAL_CHN_IVS 3
#define HAL_CHN_COUNT 4

typedef struct {
    int fileDesc;
    char enable;
    char mainLoop;
    char role;
    hal_vidcodec payload;
} hal_chnstate;

typedef struct {
    unsigned short width, height;
} hal_dim;

typedef struct {
    hal_dim dim;
    void *data;
} hal_bitmap;

typedef struct {
    unsigned char *data;
    unsigned int length;
    unsigned int jpegSize;
} hal_jpegdata;

typedef struct {
    unsigned short x, y, width, height;
} hal_rect;

typedef struct {
    unsigned short width, height;
    hal_vidcodec codec;
    hal_vidmode mode;
    hal_vidprofile profile;
    unsigned char gop, framerate, minQual, maxQual;
    unsigned short bitrate, maxBitrate;
    /* Optional encoder extensions (RV1106).  Zero keeps the previous
     * behaviour: NormalP GOP, no SVC, no motion deblur, driver QP window. */
    unsigned char smartp;
    unsigned char virIdr;
    unsigned char svc;
    unsigned char motionDeblur;
    unsigned char minQp, maxQp, minIQp, maxIQp;
} hal_vidconfig;

typedef struct {
    unsigned int length;
    unsigned int offset;
    int type;
} hal_vidnalu;

typedef struct {
    unsigned char *data;
    unsigned int length;
    int naluCnt;
    hal_vidnalu nalu[8];
    unsigned int offset;
    unsigned long long timestamp;
} hal_vidpack;

typedef struct {
    hal_vidpack *pack;
    unsigned int count;
    unsigned int seq;
} hal_vidstream;

/* Digital zoom/crop and rotation of a VPSS output channel.  The crop
 * rectangle is in the coordinates of the VPSS group input (the full sensor
 * frame); the channel output keeps its configured size, so the cropped area
 * is scaled back up. */
typedef enum {
    HAL_ROTATE_NONE = 0,
    HAL_ROTATE_90,
    HAL_ROTATE_180,
    HAL_ROTATE_270
} hal_rotation;

typedef struct {
    unsigned char enable;
    hal_rect rect;
} hal_crop;

/* AIISP (AI noise reduction) is a per-VPSS-group attribute. */
typedef enum {
    HAL_AIISP_OFF,
    HAL_AIISP_AUTO,
    HAL_AIISP_ON
} hal_aiisp_mode;

typedef struct {
    hal_aiisp_mode mode;
    char model[192];
    unsigned char bufCnt;
    unsigned short fpsEnabled;
    unsigned short fpsDisabled;
} hal_aiisp_config;

/* Motion/object detection (IVS).  Only the fields the RV1106 IVS channel
 * exposes are mirrored here. */
typedef enum {
    HAL_IVS_MD = 1,
    HAL_IVS_OD = 2,
    HAL_IVS_MD_OD = 3
} hal_ivs_mode;

typedef struct {
    hal_ivs_mode mode;
    unsigned short width, height;
    unsigned char sensitivity;      /* 1 low, 2 mid, 3 high */
    unsigned short mdInterval;
    unsigned short odInterval;
    unsigned char odPercent;        /* [5, 8] */
} hal_ivs_config;

typedef struct {
    unsigned short x, y, width, height;
} hal_ivs_rect;

typedef struct {
    unsigned int frameId;
    unsigned int rectCount;
    hal_ivs_rect rect[8];
    unsigned int odPercent;
} hal_ivs_result;
