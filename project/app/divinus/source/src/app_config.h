#pragma once

#include <libgen.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hal/config.h"
#include "hal/support.h"
#include "region.h"

struct AppConfig {
    // [system]
    char sensor_config[128];
    unsigned short web_port;
    char web_whitelist[4][256];
    bool web_enable_auth;
    char web_auth_user[32];
    char web_auth_pass[32];
    bool web_auth_skiplocal;
    bool web_enable_static;
    unsigned int isp_thread_stack_size;
    unsigned int venc_stream_thread_stack_size;
    unsigned int web_server_thread_stack_size;
    unsigned int watchdog;
    /* OKO: NTP control (WebUI Network section).  An empty server means "use
     * the distribution configuration" (usually /etc/ntp.conf). */
    bool ntp_enable;
    char ntp_server[128];

    // [night_mode]
    bool night_mode_enable;
    unsigned int ir_cut_pin1;
    unsigned int ir_cut_pin2;
    unsigned int ir_led_pin;
    unsigned int ir_sensor_pin;
    unsigned int check_interval_s;
    unsigned int pin_switch_delay_us;
    char adc_device[128];
    hal_light night_lamp;
    int adc_threshold;

    // [isp]
    bool mirror;
    bool flip;
    int antiflicker;

    // [osd]
    bool osd_enable;

    // [mdns]
    bool mdns_enable;

    // [onvif]
    bool onvif_enable;
    bool onvif_enable_auth;
    char onvif_auth_user[32];
    char onvif_auth_pass[32];

    // [rtsp]
    bool rtsp_enable;
    bool rtsp_enable_auth;
    char rtsp_auth_user[32];
    char rtsp_auth_pass[32];
    int rtsp_port;
    char rtsp_audio_codec[8];   /* pcma (G.711A), pcmu (G.711U) or mp3 */

    // [record]
    bool record_enable;
    bool record_continuous;
    char record_filename[128];
    char record_path[128];
    int record_segment_duration;
    int record_segment_size;
    bool record_sub;                    /* source: main (false) or sub (true) */
    unsigned long long record_max_bytes; /* ring quota, 0 = unlimited */
    unsigned int record_min_free_mb;

    // [eptz]
    bool eptz_enable;
    unsigned char eptz_stream;          /* 0 main, 1 sub */
    unsigned short eptz_x, eptz_y, eptz_w, eptz_h;

    // [rotate]
    bool rotate_enable;
    unsigned char rotate_stream;
    unsigned short rotate_angle;        /* 0, 90, 180, 270 */

    // [venc]
    bool venc_smartp;
    unsigned char venc_vir_idr;
    bool venc_svc;
    bool venc_motion_deblur;
    unsigned char venc_min_qp, venc_max_qp, venc_min_iqp, venc_max_iqp;

    // [substream]
    bool substream_enable;
    bool substream_codecH265;
    unsigned int substream_mode;
    unsigned int substream_width;
    unsigned int substream_height;
    unsigned int substream_fps;
    unsigned int substream_gop;
    unsigned int substream_profile;
    unsigned int substream_bitrate;
    char substream_path[32];

    // [aiisp]
    int aiisp_enable;                   /* hal_aiisp_mode */
    char aiisp_model_dir[128];
    unsigned int aiisp_buf_cnt;
    unsigned int aiisp_fps_enabled;
    unsigned int aiisp_fps_disabled;

    // [ivs]
    bool ivs_enable;
    unsigned int ivs_mode;
    unsigned int ivs_width;
    unsigned int ivs_height;
    unsigned int ivs_sensitivity;
    unsigned int ivs_md_interval;
    unsigned int ivs_od_interval;
    unsigned int ivs_od_percent;
    bool ivs_act_http_post;
    bool ivs_act_record;
    bool ivs_act_osd;
    unsigned int ivs_record_seconds;
    bool ivs_record_sub;

    // [yolo]
    bool yolo_enable;
    unsigned int yolo_source;           /* 0 vi1, 1 vpss3, 2 vi0 */
    char yolo_model[192];
    unsigned int yolo_input_width;
    unsigned int yolo_input_height;
    unsigned int yolo_fps;
    double yolo_conf_threshold;
    double yolo_nms_threshold;
    unsigned int yolo_classes;
    unsigned int yolo_pre_seconds;
    unsigned int yolo_post_seconds;
    unsigned int yolo_max_frames;

    // [iq]
    char iq_main_scene[32];
    char iq_sub_scene[32];
    char iq_file[128];

    // [hdr]
    unsigned int hdr_mode;              /* 0 normal, 1 hdr2, 2 hdr3 */
    unsigned int hdr_apply;             /* 0 auto, 1 dyn, 2 restart */

    // [osd] extended
    bool osd_scope_all;

    // [stream]
    bool stream_enable;
    unsigned short stream_udp_srcport;
    char stream_dests[4][256];

    // [audio]
    bool audio_enable;
    unsigned int audio_bitrate;
    int audio_gain;
    unsigned int audio_srate;

    // [mp4]
    bool mp4_enable;
    bool mp4_codecH265;
    unsigned int mp4_mode;
    unsigned int mp4_fps;
    unsigned int mp4_gop;
    unsigned int mp4_width;
    unsigned int mp4_height;
    unsigned int mp4_profile;
    unsigned int mp4_bitrate;

    // [jpeg]
    bool jpeg_enable;
    unsigned int jpeg_width;
    unsigned int jpeg_height;
    unsigned int jpeg_qfactor;

    // [mjpeg]
    bool mjpeg_enable;
    unsigned int mjpeg_mode;
    unsigned int mjpeg_fps;
    unsigned int mjpeg_width;
    unsigned int mjpeg_height;
    unsigned int mjpeg_bitrate;
    unsigned int mjpeg_qfactor;

    // [http_post]
    bool http_post_enable;
    char http_post_host[128];
    char http_post_url[128];
    char http_post_login[128];
    char http_post_password[128];
    unsigned int http_post_width;
    unsigned int http_post_height;
    unsigned int http_post_qfactor;
    unsigned int http_post_interval;
};

extern struct AppConfig app_config;
enum ConfigError app_config_parse(void);
void app_config_restore(void);
int app_config_save(void);
/* OKO: path of the config file that app_config_open() would use right now. */
void app_config_path(char *out, size_t size);
/* OKO: write the in-memory configuration as YAML to an already open stream. */
void app_config_dump(FILE *file);
/* OKO: replace the configuration file with an uploaded copy.  The current
 * file is rotated to divinus.yaml.bak exactly like app_config_save() does,
 * so a file that no longer parses can be rolled back at startup. */
int app_config_replace(const char *data, size_t len);
