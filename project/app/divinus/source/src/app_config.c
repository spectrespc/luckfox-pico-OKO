#include "app_config.h"

const char *appconf_paths[] = {"./divinus.yaml", "/etc/divinus.yaml", NULL};

struct AppConfig app_config;

static inline void app_config_open(FILE **file, const char *flags) {
    const char **path = appconf_paths;
    char conf_path[PATH_MAX], exe_path[PATH_MAX];
    *file = NULL;

    ssize_t exe_len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (exe_len != -1) {
        char *dir = dirname(exe_path);
        exe_path[exe_len] = '\0';
        snprintf(conf_path, sizeof(conf_path), "%s/divinus.yaml", dir);
        if (!access(conf_path, F_OK)) {
            if (*flags == 'w') {
                char bak_path[PATH_MAX];
                sprintf(bak_path, "%s.bak", conf_path);
                remove(bak_path);
                rename(conf_path, bak_path);
            }
            *file = fopen(conf_path, flags);
            return;
        }
    }

    while (*path) {
        if (access(*path++, F_OK)) continue;
        if (*flags == 'w') {
            char bak_path[PATH_MAX];
            sprintf(bak_path, "%s.bak", *(path - 1));
            remove(bak_path);
            rename(*(path - 1), bak_path);
        }
        *file = fopen(*(path - 1), flags);
        break;
    }
}

void app_config_restore(void) {
    char conf_path[PATH_MAX], exe_path[PATH_MAX];

    ssize_t exe_len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (exe_len != -1) {
        char bak_path[PATH_MAX], *dir = dirname(exe_path);
        exe_path[exe_len] = '\0';
        snprintf(conf_path, sizeof(conf_path), "%s/divinus.yaml", dir);
        sprintf(bak_path, "%s.bak", conf_path);
        if (!access(bak_path, F_OK)) {
            remove(conf_path);
            rename(bak_path, conf_path);
            return;
        }
    }

    const char **path = appconf_paths;
    while (*path) {
        char bak_path[PATH_MAX];
        sprintf(bak_path, "%s.bak", *path);
        if (!access(bak_path, F_OK)) {
            remove(*path);
            rename(bak_path, *path);
        }
        path++;
    }
}

/* OKO: report the file that app_config_open() resolves to, without opening
 * it.  Mirrors the search order above: exe dir, ./divinus.yaml, /etc. */
void app_config_path(char *out, size_t size) {
    char exe_path[PATH_MAX], dir[PATH_MAX];

    if (!out || size == 0) return;
    out[0] = '\0';

    ssize_t exe_len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (exe_len != -1) {
        exe_path[exe_len] = '\0';
        snprintf(dir, sizeof(dir), "%s", exe_path);
        snprintf(out, size, "%s/divinus.yaml", dirname(dir));
        if (!access(out, F_OK)) return;
    }

    for (const char **path = appconf_paths; *path; path++) {
        if (access(*path, F_OK)) continue;
        snprintf(out, size, "%s", *path);
        return;
    }

    snprintf(out, size, "%s", appconf_paths[0]);
}

int app_config_save(void) {
    FILE *file;

    app_config_open(&file, "w");
    if (!file)
        HAL_ERROR("app_config", "Can't open config file for writing\n");

    app_config_dump(file);
    fclose(file);
    return EXIT_SUCCESS;
}

int app_config_replace(const char *data, size_t len) {
    FILE *file;

    if (!data || !len) return EXIT_FAILURE;

    app_config_open(&file, "w");
    if (!file)
        return EXIT_FAILURE;

    if (fwrite(data, 1, len, file) != len) {
        fclose(file);
        return EXIT_FAILURE;
    }

    return fclose(file) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

void app_config_dump(FILE *file) {
    fprintf(file, "system:\n");
    fprintf(file, "  sensor_config: %s\n", app_config.sensor_config);
    fprintf(file, "  web_port: %d\n", app_config.web_port);
    if (!EMPTY(*app_config.web_whitelist)) {
        fprintf(file, "  web_whitelist:\n");
        for (int i = 0; app_config.web_whitelist[i] && *app_config.web_whitelist[i]; i++) {
            fprintf(file, "    - %s\n", app_config.web_whitelist[i]);
        }
    }
    fprintf(file, "  web_enable_auth: %s\n", app_config.web_enable_auth ? "true" : "false");
    fprintf(file, "  web_auth_user: %s\n", app_config.web_auth_user);
    fprintf(file, "  web_auth_pass: %s\n", app_config.web_auth_pass);
    fprintf(file, "  web_auth_skiplocal: %s\n", app_config.web_auth_skiplocal ? "true" : "false");
    fprintf(file, "  web_enable_static: %s\n", app_config.web_enable_static ? "true" : "false");
    fprintf(file, "  isp_thread_stack_size: %d\n", app_config.isp_thread_stack_size);
    fprintf(file, "  venc_stream_thread_stack_size: %d\n", app_config.venc_stream_thread_stack_size);
    fprintf(file, "  web_server_thread_stack_size: %d\n", app_config.web_server_thread_stack_size);
    fprintf(file, "  time_format: %s\n", timefmt);
    fprintf(file, "  watchdog: %d\n", app_config.watchdog);
    fprintf(file, "  ntp_enable: %s\n", app_config.ntp_enable ? "true" : "false");
    fprintf(file, "  ntp_server: %s\n", app_config.ntp_server);

    fprintf(file, "night_mode:\n");
    fprintf(file, "  enable: %s\n", app_config.night_mode_enable ? "true" : "false");
    fprintf(file, "  ir_sensor_pin: %d\n", app_config.ir_sensor_pin);
    fprintf(file, "  check_interval_s: %d\n", app_config.check_interval_s);
    fprintf(file, "  ir_cut_pin1: %d\n", app_config.ir_cut_pin1);
    fprintf(file, "  ir_cut_pin2: %d\n", app_config.ir_cut_pin2);
    fprintf(file, "  ir_led_pin: %d\n", app_config.ir_led_pin);
    fprintf(file, "  pin_switch_delay_us: %d\n", app_config.pin_switch_delay_us);
    fprintf(file, "  adc_device: %s\n", app_config.adc_device);
    {
        char lamp[16] = "";
        if (app_config.night_lamp & HAL_LIGHT_IR) strcat(lamp, "ir");
        if (app_config.night_lamp & HAL_LIGHT_WHITE) {
            if (lamp[0]) strcat(lamp, ",");
            strcat(lamp, "white");
        }
        if (!lamp[0]) strcpy(lamp, "none");
        fprintf(file, "  lamp: %s\n", lamp);
    }
    fprintf(file, "  adc_threshold: %d\n", app_config.adc_threshold);

    fprintf(file, "isp:\n");
    fprintf(file, "  mirror: %s\n", app_config.mirror ? "true" : "false");
    fprintf(file, "  flip: %s\n", app_config.flip ? "true" : "false");
    fprintf(file, "  antiflicker: %d\n", app_config.antiflicker);

    fprintf(file, "mdns:\n");
    fprintf(file, "  enable: %s\n", app_config.mdns_enable ? "true" : "false");

    fprintf(file, "onvif:\n");
    fprintf(file, "  enable: %s\n", app_config.onvif_enable ? "true" : "false");
    fprintf(file, "  enable_auth: %s\n", app_config.onvif_enable_auth ? "true" : "false");
    fprintf(file, "  auth_user: %s\n", app_config.onvif_auth_user);
    fprintf(file, "  auth_pass: %s\n", app_config.onvif_auth_pass);

    fprintf(file, "rtsp:\n");
    fprintf(file, "  enable: %s\n", app_config.rtsp_enable ? "true" : "false");
    fprintf(file, "  port: %d\n", app_config.rtsp_port);
    fprintf(file, "  audio_codec: %s\n", app_config.rtsp_audio_codec);
    fprintf(file, "  enable_auth: %s\n", app_config.rtsp_enable_auth ? "true" : "false");
    fprintf(file, "  auth_user: %s\n", app_config.rtsp_auth_user);
    fprintf(file, "  auth_pass: %s\n", app_config.rtsp_auth_pass);

    fprintf(file, "record:\n");
    fprintf(file, "  enable: %s\n", app_config.record_enable ? "true" : "false");
    fprintf(file, "  continuous: %s\n", app_config.record_continuous ? "true" : "false");
    fprintf(file, "  path: %s\n", app_config.record_path);
    fprintf(file, "  filename: %s\n", app_config.record_filename);
    fprintf(file, "  segment_duration: %d\n", app_config.record_segment_duration);
    fprintf(file, "  segment_size: %d\n", app_config.record_segment_size);
    fprintf(file, "  source: %s\n", app_config.record_sub ? "sub" : "main");
    fprintf(file, "  max_bytes: %llu\n", app_config.record_max_bytes);
    fprintf(file, "  min_free_mb: %d\n", app_config.record_min_free_mb);

    fprintf(file, "eptz:\n");
    fprintf(file, "  enable: %s\n", app_config.eptz_enable ? "true" : "false");
    fprintf(file, "  stream: %s\n", app_config.eptz_stream ? "sub" : "main");
    fprintf(file, "  x: %d\n  y: %d\n  w: %d\n  h: %d\n",
        app_config.eptz_x, app_config.eptz_y, app_config.eptz_w, app_config.eptz_h);

    fprintf(file, "rotate:\n");
    fprintf(file, "  enable: %s\n", app_config.rotate_enable ? "true" : "false");
    fprintf(file, "  stream: %s\n", app_config.rotate_stream ? "sub" : "main");
    fprintf(file, "  angle: %d\n", app_config.rotate_angle);

    fprintf(file, "venc:\n");
    fprintf(file, "  smartp: %s\n", app_config.venc_smartp ? "true" : "false");
    fprintf(file, "  vir_idr_len: %d\n", app_config.venc_vir_idr);
    fprintf(file, "  svc: %s\n", app_config.venc_svc ? "true" : "false");
    fprintf(file, "  motion_deblur: %s\n", app_config.venc_motion_deblur ? "true" : "false");
    fprintf(file, "  min_qp: %d\n  max_qp: %d\n  min_iqp: %d\n  max_iqp: %d\n",
        app_config.venc_min_qp, app_config.venc_max_qp,
        app_config.venc_min_iqp, app_config.venc_max_iqp);

    fprintf(file, "substream:\n");
    fprintf(file, "  enable: %s\n", app_config.substream_enable ? "true" : "false");
    fprintf(file, "  path: %s\n", app_config.substream_path);
    fprintf(file, "  codec: %s\n", app_config.substream_codecH265 ? "H.265" : "H.264");
    fprintf(file, "  mode: %d\n", app_config.substream_mode);
    fprintf(file, "  width: %d\n  height: %d\n  fps: %d\n  gop: %d\n",
        app_config.substream_width, app_config.substream_height,
        app_config.substream_fps, app_config.substream_gop);
    fprintf(file, "  bitrate: %d\n  profile: %d\n",
        app_config.substream_bitrate, app_config.substream_profile);

    fprintf(file, "aiisp:\n");
    fprintf(file, "  enable: %s\n", app_config.aiisp_enable == HAL_AIISP_OFF ? "off" :
        (app_config.aiisp_enable == HAL_AIISP_AUTO ? "auto" : "on"));
    fprintf(file, "  model_dir: %s\n", app_config.aiisp_model_dir);
    fprintf(file, "  buf_cnt: %d\n", app_config.aiisp_buf_cnt);
    fprintf(file, "  fps_enabled: %d\n  fps_disabled: %d\n",
        app_config.aiisp_fps_enabled, app_config.aiisp_fps_disabled);

    fprintf(file, "ivs:\n");
    fprintf(file, "  enable: %s\n", app_config.ivs_enable ? "true" : "false");
    fprintf(file, "  mode: %d\n", app_config.ivs_mode);
    fprintf(file, "  width: %d\n  height: %d\n", app_config.ivs_width, app_config.ivs_height);
    fprintf(file, "  sensitivity: %d\n  md_interval: %d\n  od_interval: %d\n  od_percent: %d\n",
        app_config.ivs_sensitivity, app_config.ivs_md_interval,
        app_config.ivs_od_interval, app_config.ivs_od_percent);
    fprintf(file, "  act_http_post: %s\n", app_config.ivs_act_http_post ? "true" : "false");
    fprintf(file, "  act_record: %s\n", app_config.ivs_act_record ? "true" : "false");
    fprintf(file, "  act_osd: %s\n", app_config.ivs_act_osd ? "true" : "false");
    fprintf(file, "  record_seconds: %d\n", app_config.ivs_record_seconds);
    fprintf(file, "  record_source: %s\n", app_config.ivs_record_sub ? "sub" : "main");

    fprintf(file, "yolo:\n");
    fprintf(file, "  enable: %s\n", app_config.yolo_enable ? "true" : "false");
    fprintf(file, "  source: %s\n", app_config.yolo_source == 2 ? "vi0" :
        (app_config.yolo_source == 1 ? "vpss3" : "vi1"));
    fprintf(file, "  model: %s\n", app_config.yolo_model);
    fprintf(file, "  input_width: %d\n  input_height: %d\n",
        app_config.yolo_input_width, app_config.yolo_input_height);
    fprintf(file, "  fps: %d\n", app_config.yolo_fps);
    fprintf(file, "  conf_threshold: %.2f\n", app_config.yolo_conf_threshold);
    fprintf(file, "  nms_threshold: %.2f\n", app_config.yolo_nms_threshold);
    fprintf(file, "  classes: %d\n", app_config.yolo_classes);
    fprintf(file, "  pre_seconds: %d\n  post_seconds: %d\n  max_frames: %d\n",
        app_config.yolo_pre_seconds, app_config.yolo_post_seconds,
        app_config.yolo_max_frames);

    fprintf(file, "iq:\n");
    fprintf(file, "  main_scene: %s\n", app_config.iq_main_scene);
    fprintf(file, "  sub_scene: %s\n", app_config.iq_sub_scene);
    fprintf(file, "  file: %s\n", app_config.iq_file);

    fprintf(file, "hdr:\n");
    fprintf(file, "  mode: %s\n", app_config.hdr_mode == 1 ? "hdr2" :
        (app_config.hdr_mode == 2 ? "hdr3" : "normal"));
    fprintf(file, "  apply: %s\n", app_config.hdr_apply == 2 ? "restart" :
        (app_config.hdr_apply == 1 ? "dyn" : "auto"));

    fprintf(file, "stream:\n");
    fprintf(file, "  enable: %s\n", app_config.stream_enable ? "true" : "false");
    fprintf(file, "  udp_srcport: %d\n", app_config.stream_udp_srcport);
    if (!EMPTY(*app_config.stream_dests)) {
        fprintf(file, "  dest:\n");
        for (int i = 0; app_config.stream_dests[i] && *app_config.stream_dests[i]; i++) {
            fprintf(file, "    - %s\n", app_config.stream_dests[i]);
        }
    }

    fprintf(file, "audio:\n");
    fprintf(file, "  enable: %s\n", app_config.audio_enable ? "true" : "false");
    fprintf(file, "  bitrate: %d\n", app_config.audio_bitrate);
    fprintf(file, "  gain: %d\n", app_config.audio_gain);
    fprintf(file, "  srate: %d\n", app_config.audio_srate);

    fprintf(file, "mp4:\n");
    fprintf(file, "  enable: %s\n", app_config.mp4_enable ? "true" : "false");
    fprintf(file, "  codec: %s\n", app_config.mp4_codecH265 ? "H.265" : "H.264");
    fprintf(file, "  mode: %d\n", app_config.mp4_mode);
    fprintf(file, "  width: %d\n", app_config.mp4_width);
    fprintf(file, "  height: %d\n", app_config.mp4_height);
    fprintf(file, "  fps: %d\n", app_config.mp4_fps);
    fprintf(file, "  gop: %d\n", app_config.mp4_gop);
    fprintf(file, "  profile: %d\n", app_config.mp4_profile);
    fprintf(file, "  bitrate: %d\n", app_config.mp4_bitrate);

    fprintf(file, "osd:\n");
    fprintf(file, "  enable: %s\n", app_config.osd_enable ? "true" : "false");
    fprintf(file, "  scope: %s\n", app_config.osd_scope_all ? "all" : "main");
    for (char i = 0; i < MAX_OSD; i++) {
        char imgEmpty = EMPTY(osds[i].img);
        char textEmpty = EMPTY(osds[i].text);
        if (imgEmpty && textEmpty) continue;

        if (!imgEmpty)
            fprintf(file, "  reg%d_img: %s\n", i, osds[i].img);
        if (!textEmpty)
            fprintf(file, "  reg%d_text: %s\n", i, osds[i].text);
        fprintf(file, "  reg%d_font: %s\n", i, osds[i].font);
        fprintf(file, "  reg%d_opal: %d\n", i, osds[i].opal);
        fprintf(file, "  reg%d_posx: %d\n", i, osds[i].posx);
        fprintf(file, "  reg%d_posy: %d\n", i, osds[i].posy);
        fprintf(file, "  reg%d_size: %.1f\n", i, osds[i].size);
        fprintf(file, "  reg%d_color: %#04x\n", i, osds[i].color);
        fprintf(file, "  reg%d_outl: %#04x\n", i, osds[i].outl);
        fprintf(file, "  reg%d_thick: %.1f\n", i, osds[i].thick);
    }

    fprintf(file, "jpeg:\n");
    fprintf(file, "  enable: %s\n", app_config.jpeg_enable ? "true" : "false");
    fprintf(file, "  width: %d\n", app_config.jpeg_width);
    fprintf(file, "  height: %d\n", app_config.jpeg_height);
    fprintf(file, "  qfactor: %d\n", app_config.jpeg_qfactor);

    fprintf(file, "mjpeg:\n");
    fprintf(file, "  enable: %s\n", app_config.mjpeg_enable ? "true" : "false");
    fprintf(file, "  mode: %d\n", app_config.mjpeg_mode);
    fprintf(file, "  width: %d\n", app_config.mjpeg_width);
    fprintf(file, "  height: %d\n", app_config.mjpeg_height);
    fprintf(file, "  fps: %d\n", app_config.mjpeg_fps);
    fprintf(file, "  bitrate: %d\n", app_config.mjpeg_bitrate);
    fprintf(file, "  qfactor: %d\n", app_config.mjpeg_qfactor);

    fprintf(file, "http_post:\n");
    fprintf(file, "  enable: %s\n", app_config.http_post_enable ? "true" : "false");
    fprintf(file, "  host: %s\n", app_config.http_post_host);
    fprintf(file, "  url: %s\n", app_config.http_post_url);
    fprintf(file, "  login: %s\n", app_config.http_post_login);
    fprintf(file, "  password: %s\n", app_config.http_post_password);
    fprintf(file, "  width: %d\n", app_config.http_post_width);
    fprintf(file, "  height: %d\n", app_config.http_post_height);
    fprintf(file, "  interval: %d\n", app_config.http_post_interval);
    fprintf(file, "  qfactor: %d\n", app_config.http_post_qfactor);
}

enum ConfigError app_config_parse(void) {
    memset(&app_config, 0, sizeof(struct AppConfig));

    app_config.web_port = 8080;
    *app_config.web_whitelist[0] = '\0';
    app_config.web_enable_auth = false;
    app_config.web_auth_skiplocal = false;
    app_config.web_enable_static = false;
    app_config.isp_thread_stack_size = 16 * 1024;
    app_config.venc_stream_thread_stack_size = 16 * 1024;
    app_config.web_server_thread_stack_size = 32 * 1024;
    app_config.watchdog = 0;
    app_config.ntp_enable = true;
    app_config.ntp_server[0] = '\0';

    app_config.mdns_enable = false;

    app_config.osd_enable = false;

    app_config.onvif_enable = false;
    app_config.onvif_enable_auth = false;
    app_config.onvif_auth_user[0] = '\0';
    app_config.onvif_auth_pass[0] = '\0';

    app_config.rtsp_enable = false;
    app_config.rtsp_port = 554;
    strcpy(app_config.rtsp_audio_codec, "mp3");   /* upstream default; the Fullhan image ships pcma in its yaml */
    app_config.rtsp_enable_auth = false;
    app_config.rtsp_auth_user[0] = '\0';
    app_config.rtsp_auth_pass[0] = '\0';

    app_config.record_enable = false;
    app_config.record_continuous = false;
    app_config.record_filename[0] = '\0';
    strcpy(app_config.record_path, "/mnt/sdcard/recordings");
    app_config.record_segment_duration = 0;
    app_config.record_segment_size = 0;
    app_config.record_sub = false;
    app_config.record_max_bytes = 0;
    app_config.record_min_free_mb = 2;

    app_config.eptz_enable = false;
    app_config.eptz_stream = 0;
    app_config.eptz_x = 0;
    app_config.eptz_y = 0;
    app_config.eptz_w = 1920;
    app_config.eptz_h = 1080;

    app_config.rotate_enable = false;
    app_config.rotate_stream = 0;
    app_config.rotate_angle = 0;

    app_config.venc_smartp = false;
    app_config.venc_vir_idr = 0;
    app_config.venc_svc = false;
    app_config.venc_motion_deblur = false;
    app_config.venc_min_qp = 0;
    app_config.venc_max_qp = 0;
    app_config.venc_min_iqp = 0;
    app_config.venc_max_iqp = 0;

    app_config.substream_enable = false;
    app_config.substream_codecH265 = true;
    app_config.substream_mode = HAL_VIDMODE_CBR;
    app_config.substream_width = 640;
    app_config.substream_height = 360;
    app_config.substream_fps = 10;
    app_config.substream_gop = 20;
    app_config.substream_profile = HAL_VIDPROFILE_MAIN;
    app_config.substream_bitrate = 512;
    strcpy(app_config.substream_path, "/live/1");

    app_config.aiisp_enable = HAL_AIISP_OFF;
    strcpy(app_config.aiisp_model_dir, "/oem/usr/lib");
    app_config.aiisp_buf_cnt = 1;
    app_config.aiisp_fps_enabled = 30;
    app_config.aiisp_fps_disabled = 20;

    app_config.ivs_enable = false;
    app_config.ivs_mode = HAL_IVS_MD;
    app_config.ivs_width = 640;
    app_config.ivs_height = 360;
    app_config.ivs_sensitivity = 2;
    app_config.ivs_md_interval = 5;
    app_config.ivs_od_interval = 1;
    app_config.ivs_od_percent = 7;
    app_config.ivs_act_http_post = false;
    app_config.ivs_act_record = false;
    app_config.ivs_act_osd = false;
    app_config.ivs_record_seconds = 15;
    app_config.ivs_record_sub = false;

    app_config.yolo_enable = false;
    app_config.yolo_source = 1;   /* vpss3: VI selfpath hung the board in M0 */
    strcpy(app_config.yolo_model, "/userdata/models/yolov5n.rknn");
    app_config.yolo_input_width = 640;
    app_config.yolo_input_height = 384;
    app_config.yolo_fps = 5;
    app_config.yolo_conf_threshold = 0.35;
    app_config.yolo_nms_threshold = 0.45;
    app_config.yolo_classes = 80;
    app_config.yolo_pre_seconds = 3;
    app_config.yolo_post_seconds = 2;
    app_config.yolo_max_frames = 20;

    app_config.iq_main_scene[0] = '\0';
    app_config.iq_sub_scene[0] = '\0';
    app_config.iq_file[0] = '\0';

    app_config.hdr_mode = 0;
    app_config.hdr_apply = 0;

    app_config.osd_scope_all = false;

    app_config.stream_enable = false;
    app_config.stream_udp_srcport = 0;
    *app_config.stream_dests[0] = '\0';

    app_config.sensor_config[0] = 0;
    app_config.audio_enable = false;
    app_config.audio_bitrate = 128;
    app_config.audio_gain = 0;
    app_config.jpeg_enable = false;
    app_config.mp4_enable = false;

    app_config.mjpeg_enable = false;
    app_config.mjpeg_fps = 15;
    app_config.mjpeg_width = 640;
    app_config.mjpeg_height = 360;
    app_config.mjpeg_bitrate = 1024;
    app_config.mjpeg_qfactor = 50;

    app_config.mirror = false;
    app_config.flip = false;
    app_config.antiflicker = 0;

    app_config.night_mode_enable = false;
    app_config.ir_sensor_pin = 999;
    app_config.ir_cut_pin1 = 999;
    app_config.ir_cut_pin2 = 999;
    app_config.ir_led_pin = 999;
    app_config.pin_switch_delay_us = 250;
    app_config.check_interval_s = 10;
    app_config.adc_device[0] = 0;
    app_config.night_lamp = HAL_LIGHT_IR;
    app_config.adc_threshold = 128;

    struct IniConfig ini;
    memset(&ini, 0, sizeof(struct IniConfig));

    FILE *file;
    app_config_open(&file, "r");
    if (!open_config(&ini, &file))  {
        printf("Can't find config divinus.yaml in:\n"
            "    ./divinus.yaml\n    /etc/divinus.yaml\n");
        return -1;
    }

    enum ConfigError err;
    find_sections(&ini);

    if (plat != HAL_PLATFORM_GM && plat != HAL_PLATFORM_RK) {
        err = parse_param_value(&ini, "system", "sensor_config", app_config.sensor_config);
        if (err != CONFIG_OK && (plat == HAL_PLATFORM_AK ||
             plat == HAL_PLATFORM_V1 || plat == HAL_PLATFORM_V2 ||
             plat == HAL_PLATFORM_V3 || plat == HAL_PLATFORM_V4))
            goto RET_ERR;
    }
    int port, count;
    err = parse_int(&ini, "system", "web_port", 0, USHRT_MAX, &port);
    if (err != CONFIG_OK)
        goto RET_ERR;
    app_config.web_port = (unsigned short)port;
    parse_list(&ini, "system", "web_whitelist",
        sizeof(app_config.web_whitelist) / sizeof(*app_config.web_whitelist),
        &count, app_config.web_whitelist);
    *app_config.web_whitelist[count] = '\0';
    parse_bool(&ini, "system", "web_enable_auth", &app_config.web_enable_auth);
    parse_param_value(
        &ini, "system", "web_auth_user", app_config.web_auth_user);
    parse_param_value(
        &ini, "system", "web_auth_pass", app_config.web_auth_pass);
    parse_bool(&ini, "system", "web_auth_skiplocal", &app_config.web_auth_skiplocal);
    err = parse_bool(
        &ini, "system", "web_enable_static", &app_config.web_enable_static);
    if (err != CONFIG_OK)
        goto RET_ERR;
    err = parse_int(
        &ini, "system", "isp_thread_stack_size", 16 * 1024, INT_MAX,
        &app_config.isp_thread_stack_size);
    if (err != CONFIG_OK)
        goto RET_ERR;
    err = parse_int(
        &ini, "system", "venc_stream_thread_stack_size", 16 * 1024, INT_MAX,
        &app_config.venc_stream_thread_stack_size);
    if (err != CONFIG_OK)
        goto RET_ERR;
    err = parse_int(
        &ini, "system", "web_server_thread_stack_size", 16 * 1024, INT_MAX,
        &app_config.web_server_thread_stack_size);
    if (err != CONFIG_OK)
        goto RET_ERR;
    parse_param_value(&ini, "system", "time_format", timefmt);
    if (EMPTY(timefmt))
        strncpy(timefmt, DEF_TIMEFMT, sizeof(timefmt) - 1);
    parse_int(&ini, "system", "watchdog", 0, INT_MAX, &app_config.watchdog);
    parse_bool(&ini, "system", "ntp_enable", &app_config.ntp_enable);
    parse_param_value_n(&ini, "system", "ntp_server",
        app_config.ntp_server, sizeof(app_config.ntp_server) - 1);

    err =
        parse_bool(&ini, "night_mode", "enable", &app_config.night_mode_enable);
    #define PIN_MAX 95
    if (app_config.night_mode_enable) {
        {
            int pin;
            if (parse_int(&ini, "night_mode", "ir_sensor_pin", 0, 999, &pin) == CONFIG_OK &&
                (pin <= PIN_MAX || pin == 999))
                app_config.ir_sensor_pin = pin;
        }
        parse_int(
            &ini, "night_mode", "check_interval_s", 0, 600,
            &app_config.check_interval_s);
        parse_int(
            &ini, "night_mode", "ir_cut_pin1", 0, PIN_MAX,
            &app_config.ir_cut_pin1);
        parse_int(
            &ini, "night_mode", "ir_cut_pin2", 0, PIN_MAX,
            &app_config.ir_cut_pin2);
        parse_int(
            &ini, "night_mode", "ir_led_pin", 0, PIN_MAX,
            &app_config.ir_led_pin);
        parse_int(
            &ini, "night_mode", "pin_switch_delay_us", 0, 1000,
            &app_config.pin_switch_delay_us);
        parse_param_value(
            &ini, "night_mode", "adc_device", app_config.adc_device);
        {
            char lamp[128] = {0};
            if (parse_param_value_n(&ini, "night_mode", "lamp", lamp, sizeof(lamp)) == CONFIG_OK) {
                int flags = 0, sawNone = 0, valid = 1;
                for (char *p = lamp; *p; ) {
                    p += strspn(p, ",| ");
                    if (!*p) break;
                    size_t len = strcspn(p, ",| ");
                    if (len == 2 && !strncmp(p, "ir", 2))
                        flags |= HAL_LIGHT_IR;
                    else if (len == 5 && !strncmp(p, "white", 5))
                        flags |= HAL_LIGHT_WHITE;
                    else if (len == 4 && !strncmp(p, "none", 4))
                        sawNone = 1;
                    else { valid = 0; break; }
                    p += len;
                }
                if (valid && (flags || sawNone) && !(sawNone && flags))
                    app_config.night_lamp = (hal_light)flags;
            }
        }
        parse_int(
            &ini, "night_mode", "adc_threshold", INT_MIN, INT_MAX,
            &app_config.adc_threshold);
    }

    err = parse_bool(&ini, "isp", "mirror", &app_config.mirror);
    if (err != CONFIG_OK)
        goto RET_ERR;
    err = parse_bool(&ini, "isp", "flip", &app_config.flip);
    if (err != CONFIG_OK)
        goto RET_ERR;
    parse_int(&ini, "isp", "antiflicker", -1, 60, &app_config.antiflicker);

    parse_bool(&ini, "mdns", "enable", &app_config.mdns_enable);

    parse_bool(&ini, "osd", "enable", &app_config.osd_enable);
    {
        char scope[16] = {0};
        if (parse_param_value_n(&ini, "osd", "scope", scope, sizeof(scope)) == CONFIG_OK)
            app_config.osd_scope_all = EQUALS(scope, "all");
    }
    if (app_config.osd_enable) {
        for (char i = 0; i < MAX_OSD; i++) {
            char param[16];
            int val;
            region_defaults(&osds[i], i);
            sprintf(param, "reg%d_img", i);
            parse_param_value(&ini, "osd", param, osds[i].img);
            sprintf(param, "reg%d_text", i);
            parse_param_value(&ini, "osd", param, osds[i].text);
            sprintf(param, "reg%d_font", i);
            parse_param_value(&ini, "osd", param, osds[i].font);
            sprintf(param, "reg%d_opal", i);
            err = parse_int(&ini, "osd", param, 0, UCHAR_MAX, &val);
            if (err == CONFIG_OK) osds[i].opal = (unsigned char)val;
            sprintf(param, "reg%d_posx", i);
            err = parse_int(&ini, "osd", param, 0, SHRT_MAX, &val);
            if (err == CONFIG_OK) osds[i].posx = (short)val;
            sprintf(param, "reg%d_posy", i);
            err = parse_int(&ini, "osd", param, 0, SHRT_MAX, &val);
            if (err == CONFIG_OK) osds[i].posy = (short)val;
            sprintf(param, "reg%d_size", i);
            parse_double(&ini, "osd", param, 0, INT_MAX, &osds[i].size);
            sprintf(param, "reg%d_color", i);
            parse_int(&ini, "osd", param, 0, USHRT_MAX, &osds[i].color);
            sprintf(param, "reg%d_outl", i);
            parse_int(&ini, "osd", param, 0, USHRT_MAX, &osds[i].outl);
            sprintf(param, "reg%d_thick", i);
            parse_double(&ini, "osd", param, 0, UCHAR_MAX, &osds[i].thick);
            osds[i].updt = 1;
        }
    }

    parse_bool(&ini, "onvif", "enable", &app_config.onvif_enable);
    if (app_config.onvif_enable) {
        parse_bool(&ini, "onvif", "enable_auth", &app_config.onvif_enable_auth);
        parse_param_value(
            &ini, "onvif", "auth_user", app_config.onvif_auth_user);
        parse_param_value(
            &ini, "onvif", "auth_pass", app_config.onvif_auth_pass);
    }

    parse_bool(&ini, "record", "enable", &app_config.record_enable);
    parse_bool(&ini, "record", "continuous", &app_config.record_continuous);
    parse_param_value(
        &ini, "record", "path", app_config.record_path);
    parse_param_value(
        &ini, "record", "filename", app_config.record_filename);
    parse_int(&ini, "record", "segment_duration", 0, INT_MAX,
        &app_config.record_segment_duration);
    parse_int(&ini, "record", "segment_size", 0, INT_MAX,
        &app_config.record_segment_size);
    {
        char val[16] = {0};
        if (parse_param_value_n(&ini, "record", "source", val, sizeof(val)) == CONFIG_OK)
            app_config.record_sub = EQUALS(val, "sub");
        {
            uint64_t bytes;
            if (parse_uint64(&ini, "record", "max_bytes", 0, UINT64_MAX, &bytes) == CONFIG_OK)
                app_config.record_max_bytes = (unsigned long long)bytes;
        }
        {
            int free_mb = app_config.record_min_free_mb;
            parse_int(&ini, "record", "min_free_mb", 0, INT_MAX, &free_mb);
            app_config.record_min_free_mb = (unsigned int)free_mb;
        }
    }

    {
        int val;
        parse_bool(&ini, "eptz", "enable", &app_config.eptz_enable);
        {
            char val[16] = {0};
            if (parse_param_value_n(&ini, "eptz", "stream", val, sizeof(val)) == CONFIG_OK)
                app_config.eptz_stream = EQUALS(val, "sub") ? 1 : 0;
        }
        val = app_config.eptz_x;
        parse_int(&ini, "eptz", "x", 0, 65535, &val);
        app_config.eptz_x = (unsigned short)val;
        val = app_config.eptz_y;
        parse_int(&ini, "eptz", "y", 0, 65535, &val);
        app_config.eptz_y = (unsigned short)val;
        val = app_config.eptz_w;
        parse_int(&ini, "eptz", "w", 16, 65535, &val);
        app_config.eptz_w = (unsigned short)val;
        val = app_config.eptz_h;
        parse_int(&ini, "eptz", "h", 16, 65535, &val);
        app_config.eptz_h = (unsigned short)val;

        parse_bool(&ini, "rotate", "enable", &app_config.rotate_enable);
        {
            char val[16] = {0};
            if (parse_param_value_n(&ini, "rotate", "stream", val, sizeof(val)) == CONFIG_OK)
                app_config.rotate_stream = EQUALS(val, "sub") ? 1 : 0;
        }
        {
            int angle = app_config.rotate_angle;
            if (parse_int(&ini, "rotate", "angle", 0, 270, &angle) == CONFIG_OK)
                app_config.rotate_angle = (angle == 90 || angle == 180 ||
                    angle == 270) ? angle : 0;
        }
    }

    parse_bool(&ini, "venc", "smartp", &app_config.venc_smartp);
    {
        int val;
        val = app_config.venc_vir_idr;
        parse_int(&ini, "venc", "vir_idr_len", 0, 255, &val);
        app_config.venc_vir_idr = (unsigned char)val;
        val = app_config.venc_min_qp;
        parse_int(&ini, "venc", "min_qp", 0, 51, &val);
        app_config.venc_min_qp = (unsigned char)val;
        val = app_config.venc_max_qp;
        parse_int(&ini, "venc", "max_qp", 0, 51, &val);
        app_config.venc_max_qp = (unsigned char)val;
        val = app_config.venc_min_iqp;
        parse_int(&ini, "venc", "min_iqp", 0, 51, &val);
        app_config.venc_min_iqp = (unsigned char)val;
        val = app_config.venc_max_iqp;
        parse_int(&ini, "venc", "max_iqp", 0, 51, &val);
        app_config.venc_max_iqp = (unsigned char)val;
    }
    parse_bool(&ini, "venc", "svc", &app_config.venc_svc);
    parse_bool(&ini, "venc", "motion_deblur", &app_config.venc_motion_deblur);

    parse_bool(&ini, "substream", "enable", &app_config.substream_enable);
    if (app_config.substream_enable) {
        char val[32] = {0};
        parse_param_value_n(&ini, "substream", "path", val, sizeof(val));
        if (val[0] == '/') {
            strncpy(app_config.substream_path, val,
                sizeof(app_config.substream_path) - 1);
            app_config.substream_path[sizeof(app_config.substream_path) - 1] = 0;
        }
        if (parse_param_value_n(&ini, "substream", "codec", val, sizeof(val)) == CONFIG_OK)
            app_config.substream_codecH265 = !(EQUALS(val, "H.264") ||
                EQUALS(val, "H264") || EQUALS(val, "AVC"));
        {
            const char *modes[] = {"CBR", "VBR", "QP", "ABR", "AVBR"};
            int v = (int)app_config.substream_mode;
            parse_enum(&ini, "substream", "mode", (void *)&v, modes, 5, 0);
            if (v >= 0 && v < HAL_VIDMODE_END)
                app_config.substream_mode = (unsigned int)v;
        }
        {
            int w = app_config.substream_width, h = app_config.substream_height;
            int fps = app_config.substream_fps, gop = app_config.substream_gop;
            int bitrate = app_config.substream_bitrate, profile = app_config.substream_profile;
            parse_int(&ini, "substream", "width", 160, INT_MAX, &w);
            parse_int(&ini, "substream", "height", 120, INT_MAX, &h);
            parse_int(&ini, "substream", "fps", 1, 60, &fps);
            parse_int(&ini, "substream", "gop", 1, INT_MAX, &gop);
            parse_int(&ini, "substream", "bitrate", 32, INT_MAX, &bitrate);
            parse_int(&ini, "substream", "profile", 0, 2, &profile);
            app_config.substream_width = w;
            app_config.substream_height = h;
            app_config.substream_fps = fps;
            app_config.substream_gop = gop;
            app_config.substream_bitrate = bitrate;
            app_config.substream_profile = profile;
        }
    }

    {
        char val[16] = {0};
        if (parse_param_value_n(&ini, "aiisp", "enable", val, sizeof(val)) == CONFIG_OK) {
            if (EQUALS_CASE(val, "on") || EQUALS(val, "true") || EQUALS(val, "1"))
                app_config.aiisp_enable = HAL_AIISP_ON;
            else if (EQUALS_CASE(val, "auto"))
                app_config.aiisp_enable = HAL_AIISP_AUTO;
            else
                app_config.aiisp_enable = HAL_AIISP_OFF;
        }
        if (parse_param_value_n(&ini, "aiisp", "model_dir", val, sizeof(val)) == CONFIG_OK)
            strncpy(app_config.aiisp_model_dir, val, sizeof(app_config.aiisp_model_dir) - 1);
        {
            int v = (int)app_config.aiisp_buf_cnt;
            parse_int(&ini, "aiisp", "buf_cnt", 0, 16, &v);
            app_config.aiisp_buf_cnt = (unsigned int)v;
            v = (int)app_config.aiisp_fps_enabled;
            parse_int(&ini, "aiisp", "fps_enabled", 1, 60, &v);
            app_config.aiisp_fps_enabled = (unsigned int)v;
            v = (int)app_config.aiisp_fps_disabled;
            parse_int(&ini, "aiisp", "fps_disabled", 1, 60, &v);
            app_config.aiisp_fps_disabled = (unsigned int)v;
        }
    }

    parse_bool(&ini, "ivs", "enable", &app_config.ivs_enable);
    if (app_config.ivs_enable) {
        char val[16] = {0};
        int mode = app_config.ivs_mode;
        parse_int(&ini, "ivs", "mode", 1, 3, &mode);
        if (parse_param_value_n(&ini, "ivs", "mode", val, sizeof(val)) == CONFIG_OK) {
            if (EQUALS_CASE(val, "md")) mode = 1;
            else if (EQUALS_CASE(val, "od")) mode = 2;
            else if (EQUALS_CASE(val, "md_od")) mode = 3;
        }
        app_config.ivs_mode = (unsigned int)mode;
        {
            int w = (int)app_config.ivs_width, h = (int)app_config.ivs_height;
            int sens = (int)app_config.ivs_sensitivity;
            int md = (int)app_config.ivs_md_interval;
            int od = (int)app_config.ivs_od_interval;
            int pct = (int)app_config.ivs_od_percent;
            int sec = (int)app_config.ivs_record_seconds;
            parse_int(&ini, "ivs", "width", 160, INT_MAX, &w);
            parse_int(&ini, "ivs", "height", 120, INT_MAX, &h);
            parse_int(&ini, "ivs", "sensitivity", 1, 3, &sens);
            parse_int(&ini, "ivs", "md_interval", 1, 100, &md);
            parse_int(&ini, "ivs", "od_interval", 1, 100, &od);
            parse_int(&ini, "ivs", "od_percent", 5, 8, &pct);
            parse_int(&ini, "ivs", "record_seconds", 1, 600, &sec);
            app_config.ivs_width = (unsigned int)w;
            app_config.ivs_height = (unsigned int)h;
            app_config.ivs_sensitivity = (unsigned int)sens;
            app_config.ivs_md_interval = (unsigned int)md;
            app_config.ivs_od_interval = (unsigned int)od;
            app_config.ivs_od_percent = (unsigned int)pct;
            app_config.ivs_record_seconds = (unsigned int)sec;
        }
        parse_bool(&ini, "ivs", "act_http_post", &app_config.ivs_act_http_post);
        parse_bool(&ini, "ivs", "act_record", &app_config.ivs_act_record);
        parse_bool(&ini, "ivs", "act_osd", &app_config.ivs_act_osd);
        if (parse_param_value_n(&ini, "ivs", "record_source", val, sizeof(val)) == CONFIG_OK)
            app_config.ivs_record_sub = EQUALS(val, "sub");
    }

    /* [yolo] is parsed even while disabled: the WebUI has to be able to edit
     * and Save the settings before the model is installed. */
    parse_bool(&ini, "yolo", "enable", &app_config.yolo_enable);
    {
        char val[32] = {0};
        if (parse_param_value_n(&ini, "yolo", "source", val, sizeof(val)) == CONFIG_OK) {
            if (EQUALS(val, "vpss3")) app_config.yolo_source = 1;
            else if (EQUALS(val, "vi0")) app_config.yolo_source = 2;
            else app_config.yolo_source = 0;
        }
        parse_param_value_n(&ini, "yolo", "model", app_config.yolo_model,
            sizeof(app_config.yolo_model));
        {
            int v;
            v = (int)app_config.yolo_input_width;
            parse_int(&ini, "yolo", "input_width", 64, 4096, &v);
            app_config.yolo_input_width = (unsigned int)v;
            v = (int)app_config.yolo_input_height;
            parse_int(&ini, "yolo", "input_height", 64, 4096, &v);
            app_config.yolo_input_height = (unsigned int)v;
            v = (int)app_config.yolo_fps;
            parse_int(&ini, "yolo", "fps", 1, 30, &v);
            app_config.yolo_fps = (unsigned int)v;
            v = (int)app_config.yolo_classes;
            parse_int(&ini, "yolo", "classes", 1, 255, &v);
            app_config.yolo_classes = (unsigned int)v;
            v = (int)app_config.yolo_pre_seconds;
            parse_int(&ini, "yolo", "pre_seconds", 0, 30, &v);
            app_config.yolo_pre_seconds = (unsigned int)v;
            v = (int)app_config.yolo_post_seconds;
            parse_int(&ini, "yolo", "post_seconds", 0, 30, &v);
            app_config.yolo_post_seconds = (unsigned int)v;
            v = (int)app_config.yolo_max_frames;
            parse_int(&ini, "yolo", "max_frames", 1, 60, &v);
            app_config.yolo_max_frames = (unsigned int)v;
        }
        parse_double(&ini, "yolo", "conf_threshold", 0.01, 0.99,
            &app_config.yolo_conf_threshold);
        parse_double(&ini, "yolo", "nms_threshold", 0.01, 0.99,
            &app_config.yolo_nms_threshold);
    }

    parse_param_value_n(&ini, "iq", "main_scene",
        app_config.iq_main_scene, sizeof(app_config.iq_main_scene));
    parse_param_value_n(&ini, "iq", "sub_scene",
        app_config.iq_sub_scene, sizeof(app_config.iq_sub_scene));
    parse_param_value_n(&ini, "iq", "file", app_config.iq_file, sizeof(app_config.iq_file));

    {
        char val[16] = {0};
        if (parse_param_value_n(&ini, "hdr", "mode", val, sizeof(val)) == CONFIG_OK) {
            if (EQUALS_CASE(val, "hdr2")) app_config.hdr_mode = 1;
            else if (EQUALS_CASE(val, "hdr3")) app_config.hdr_mode = 2;
            else app_config.hdr_mode = 0;
        }
        if (parse_param_value_n(&ini, "hdr", "apply", val, sizeof(val)) == CONFIG_OK) {
            if (EQUALS_CASE(val, "dyn")) app_config.hdr_apply = 1;
            else if (EQUALS_CASE(val, "restart")) app_config.hdr_apply = 2;
            else app_config.hdr_apply = 0;
        }
    }

    parse_bool(&ini, "rtsp", "enable", &app_config.rtsp_enable);
    parse_int(&ini, "rtsp", "port", 0, USHRT_MAX, &app_config.rtsp_port);
    {
        /* Bounded, and only the codecs the RTP path implements: anything
         * else would advertise a payload type nothing produces. */
        char codec[16] = {0};
        if (parse_param_value_n(&ini, "rtsp", "audio_codec", codec, sizeof(codec)) == CONFIG_OK) {
            if (EQUALS(codec, "pcma") || EQUALS(codec, "pcmu") || EQUALS(codec, "mp3")) {
                strncpy(app_config.rtsp_audio_codec, codec,
                    sizeof(app_config.rtsp_audio_codec) - 1);
                app_config.rtsp_audio_codec[sizeof(app_config.rtsp_audio_codec) - 1] = 0;
            }
        }
    }
    if (app_config.rtsp_enable) {
        parse_bool(&ini, "rtsp", "enable_auth", &app_config.rtsp_enable_auth);
        parse_param_value(
            &ini, "rtsp", "auth_user", app_config.rtsp_auth_user);
        parse_param_value(
            &ini, "rtsp", "auth_pass", app_config.rtsp_auth_pass);
    }

    parse_bool(&ini, "stream", "enable", &app_config.stream_enable);
    if (app_config.stream_enable) {
        int count, val;
        err = parse_int(&ini, "stream", "udp_srcport", 0, USHRT_MAX, &val);
        if (err == CONFIG_OK) app_config.stream_udp_srcport = (unsigned short)val;
        parse_list(&ini, "stream", "dest",
            sizeof(app_config.stream_dests) / sizeof(*app_config.stream_dests),
            &count, app_config.stream_dests);
        *app_config.stream_dests[count] = '\0';
    }

    parse_bool(&ini, "audio", "enable", &app_config.audio_enable);
    if (app_config.audio_enable) {
        parse_int(&ini, "audio", "bitrate", 32, 320, &app_config.audio_bitrate);
        parse_int(&ini, "audio", "gain", -60, 30, &app_config.audio_gain);
        parse_int(&ini, "audio", "srate", 8000, 96000,
            &app_config.audio_srate);
    }

    parse_bool(&ini, "mp4", "enable", &app_config.mp4_enable);
    if (app_config.mp4_enable) {
        {
            const char *possible_values[] = {"H.264", "H.265", "H264", "H265", "AVC", "HEVC"};
            const int count = sizeof(possible_values) / sizeof(const char *);
            int val = 0;
            parse_enum(&ini, "mp4", "codec", (void *)&val,
                possible_values, count, 0);
            if (val % 2)
                app_config.mp4_codecH265 = true;
            else
                app_config.mp4_codecH265 = false;
        }
        {
            const char *possible_values[] = {"CBR", "VBR", "QP", "ABR", "AVBR"};
            const int count = sizeof(possible_values) / sizeof(const char *);
            int val = 0;
            parse_enum(&ini, "mp4", "mode", (void *)&val,
                possible_values, count, 0);
            app_config.mp4_mode = val;
        }
        err = parse_int(
            &ini, "mp4", "width", 160, INT_MAX, &app_config.mp4_width);
        if (err != CONFIG_OK)
            goto RET_ERR;
        err = parse_int(
            &ini, "mp4", "height", 120, INT_MAX, &app_config.mp4_height);
        if (err != CONFIG_OK)
            goto RET_ERR;
        err = parse_int(&ini, "mp4", "fps", 1, INT_MAX, &app_config.mp4_fps);
        if (err != CONFIG_OK)
            goto RET_ERR;
        app_config.mp4_gop = app_config.mp4_fps;
        parse_int(&ini, "mp4", "gop", 1, INT_MAX, &app_config.mp4_gop);
        {
            const char *possible_values[] = {"BP", "MP", "HP"};
            const int count = sizeof(possible_values) / sizeof(const char *);
            const char *possible_values2[] = {"BASELINE", "MAIN", "HIGH"};
            const int count2 = sizeof(possible_values2) / sizeof(const char *);
            int val = 0;
            if (parse_enum(&ini, "mp4", "profile", (void *)&val,
                    possible_values, count, 0) != CONFIG_OK)
                parse_enum( &ini, "mp4", "profile", (void *)&val,
                    possible_values2, count2, 0);
            app_config.mp4_profile = val;
        }
        parse_int(&ini, "mp4", "profile", 0, 2, &app_config.mp4_profile);

        err = parse_int(
            &ini, "mp4", "bitrate", 32, INT_MAX, &app_config.mp4_bitrate);
        if (err != CONFIG_OK)
            goto RET_ERR;
    }

    err = parse_bool(&ini, "jpeg", "enable", &app_config.jpeg_enable);
    if (err != CONFIG_OK)
        goto RET_ERR;
    if (app_config.jpeg_enable) {
        err = parse_int(
            &ini, "jpeg", "width", 160, INT_MAX, &app_config.jpeg_width);
        if (err != CONFIG_OK)
            goto RET_ERR;
        err = parse_int(
            &ini, "jpeg", "height", 120, INT_MAX, &app_config.jpeg_height);
        if (err != CONFIG_OK)
            goto RET_ERR;
        err =
            parse_int(&ini, "jpeg", "qfactor", 1, 99, &app_config.jpeg_qfactor);
        if (err != CONFIG_OK)
            goto RET_ERR;
    }

    err = parse_bool(&ini, "mjpeg", "enable", &app_config.mjpeg_enable);
    if (err != CONFIG_OK)
        goto RET_ERR;
    if (app_config.mjpeg_enable) {
        {
            const char *possible_values[] = {"CBR", "VBR", "QP"};
            const int count = sizeof(possible_values) / sizeof(const char *);
            int val = 0;
            parse_enum(&ini, "mjpeg", "mode", (void *)&val,
                possible_values, count, 0);
            app_config.mjpeg_mode = val;
        }
        err = parse_int(
            &ini, "mjpeg", "width", 160, INT_MAX, &app_config.mjpeg_width);
        if (err != CONFIG_OK)
            goto RET_ERR;
        err = parse_int(
            &ini, "mjpeg", "height", 120, INT_MAX, &app_config.mjpeg_height);
        if (err != CONFIG_OK)
            goto RET_ERR;
        err =
            parse_int(&ini, "mjpeg", "fps", 1, INT_MAX, &app_config.mjpeg_fps);
        if (err != CONFIG_OK)
            goto RET_ERR;
        err = parse_int(
            &ini, "mjpeg", "bitrate", 32, INT_MAX, &app_config.mjpeg_bitrate);
        if (err != CONFIG_OK)
            goto RET_ERR;
        parse_int(&ini, "mjpeg", "qfactor", 1, 99, &app_config.mjpeg_qfactor);
    }

    parse_bool(&ini, "http_post", "enable", &app_config.http_post_enable);
    if (app_config.http_post_enable) {
        err = parse_param_value(
            &ini, "http_post", "host", app_config.http_post_host);
        if (err != CONFIG_OK)
            goto RET_ERR;
        err = parse_param_value(
            &ini, "http_post", "url", app_config.http_post_url);
        if (err != CONFIG_OK)
            goto RET_ERR;

        err = parse_param_value(
            &ini, "http_post", "login", app_config.http_post_login);
        if (err != CONFIG_OK)
            goto RET_ERR;
        err = parse_param_value(
            &ini, "http_post", "password", app_config.http_post_password);
        if (err != CONFIG_OK)
            goto RET_ERR;

        err = parse_int(
            &ini, "http_post", "width", 160, INT_MAX,
            &app_config.http_post_width);
        if (err != CONFIG_OK)
            goto RET_ERR;
        err = parse_int(
            &ini, "http_post", "height", 120, INT_MAX,
            &app_config.http_post_height);
        if (err != CONFIG_OK)
            goto RET_ERR;
        err = parse_int(
            &ini, "http_post", "interval", 1, INT_MAX,
            &app_config.http_post_interval);
        if (err != CONFIG_OK)
            goto RET_ERR;
        err = parse_int(
            &ini, "http_post", "qfactor", 1, 99, &app_config.http_post_qfactor);
        if (err != CONFIG_OK)
            goto RET_ERR;
    }

    free(ini.str);
    return CONFIG_OK;
RET_ERR:
    free(ini.str);
    return err;
}
