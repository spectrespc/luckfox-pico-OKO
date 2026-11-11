# Configuration Reference

This document describes the fields that can be found within a configuration file for Divinus.

## System section

- **sensor_config**: Path to the sensor calibration or configuration file, if applicable (e.g., `/etc/sensors/imx415.bin`).
- **web_port**: Port number for the web server (default: `80`).
- **web_whitelist**: Array of up to 4 IP addresses or domains allowed to access the web server.
- **web_enable_auth**: Boolean to enable authentication on the API, live stream and WebUI endpoints (default: `false`).
- **web_auth_user**: Username for basic authentication (default: `admin`).
- **web_auth_pass**: Password for basic authentication (default: `12345`).
- **web_auth_skiplocal**: Boolean to skip authentication for local addresses (default: `true`).
- **web_enable_static**: Boolean to enable serving static web content (default: `false`).
- **isp_thread_stack_size**: Stack size for ISP thread, if applicable (default: `16384`).
- **venc_stream_thread_stack_size**: Stack size for video encoding stream thread (default: `16384`).
- **web_server_thread_stack_size**: Stack size for web server thread (default: `65536`).
- **time_format**: Format for displaying time, refer to strftime() modifiers for exact parameters (e.g., `"%Y-%m-%d %H:%M:%S"`).
- **watchdog**: Watchdog timer in seconds, where 0 means disabled (default: `0`).

## Night mode section

- **enable**: Boolean to activate night mode support.
- **ir_cut_pin1**: GPIO number for IR cut filter control (normal state pin).
- **ir_cut_pin2**: GPIO number for IR cut filter control (inverted state pin).
- **ir_led_pin**: GPIO number for IR LED control.
- **ir_sensor_pin**: GPIO number for PIR motion sensor or similar digital toggle; `999` means no sensor (default).
- **check_interval_s**: Interval in seconds to check night mode conditions.
- **pin_switch_delay_us**: Delay in microseconds before switching GPIO pins, must be used to protect cut filter coils from burning.
- **adc_device**: Path to the ADC device used for night mode.
- **lamp**: Illuminator mode on Fullhan V100, combinable: `ir` (default), `white` (colour night vision, IR-cut filter in) or `none`. Combine with commas, e.g. `ir,white`.
- **adc_threshold**: Threshold raw value to trigger night mode, depends on the bitness of the given ADC device.

## ISP section

- **mirror**: Boolean to turn on image mirroring (default: `false`).
- **flip**: Boolean to turn on image flipping (default: `false`).
- **antiflicker**: Antiflicker setting in Hz (default: `60`).

## mDNS section

- **enable**: Boolean to turn on mDNS announcer, lets LAN users access the device by a domain name, its configured hostname followed by .local (default: `false`).

## ONVIF section

- **enable**: Boolean to activate ONVIF services (default: `false`).
- **enable_auth**: Boolean to turn on ONVIF authentication (default: `false`).
- **auth_user**: Username for ONVIF authentication (default: `admin`).
- **auth_pass**: Password for ONVIF authentication (default: `12345`).

## RTSP section

- **enable**: Boolean to activate the integrated RTSP server (default: `true`).
- **enable_auth**: Boolean to turn on RTSP authentication (default: `false`).
- **auth_user**: Username for RTSP authentication (default: `admin`).
- **auth_pass**: Password for RTSP authentication (default: `12345`).
- **port**: Port number for RTSP server (default: `554`).

## Record section

- **enable**: Boolean to allow or block recording operations (default: `false`).
- **continuous**: Boolean to turn on continuous recording at launch (default: `false`).
- **path**: Path to save recordings. Only `/mnt/sdcard` and its subdirectories are accepted (e.g., `/mnt/sdcard/recordings`); missing directories are created automatically. `/oem`, `/userdata`, `/tmp` and `/` are always refused.
- **filename**: Base name for the resulting files (e.g., `Entrance.mp4`). A `_<timestamp>` suffix is appended before the extension so segments never overwrite each other; leave empty to use `recording`.
- **segment_duration**: Target duration for a recording in seconds
- **segment_size**: Target file size for a recording in bytes
- **source**: VENC channel that feeds the recorder: `main` (default) or `sub`.
- **max_bytes**: Ring quota for the recording directory in bytes; `0` keeps everything (default: `0`). The oldest `.mp4` segments are deleted first.
- **min_free_mb**: Minimum free space on the card in MiB; older segments are removed before writing when the card gets full (default: `2`). Recording is refused unless `/mnt/sdcard` is a real mount point. If the card is unmounted or removed while recording, the current segment is closed and writing stops; the recorder never falls back to SPI NAND. `/api/storage?action=unmount` and `action=format` are refused while a recording is active.

Both H.264 and H.265 are recorded as fragmented MP4.  The quota is checked when a
segment is opened, so the current segment may temporarily exceed `max_bytes`
until the next rotation; the oldest files are removed first.

## ePTZ section

Digital zoom by cropping the VPSS input and scaling it back to the configured channel size.

- **enable**: Boolean to turn the crop on (default: `false`).
- **stream**: `main` or `sub` (default: `main`).
- **x**, **y**, **w**, **h**: Crop rectangle in sensor pixels. Clamped to the main stream size; `w`/`h` are at least 16.

## Rotation section

- **enable**: Boolean to turn rotation on (default: `false`).
- **stream**: `main` or `sub` (default: `main`).
- **angle**: `0`, `90`, `180` or `270` degrees (default: `0`). On RV1106 the VENC rotation is used because the VPSS rotation is a no-op for the encoded picture.

## VENC section

- **smartp**: SmartP GOP mode (default: `false`); applied at the next start.
- **vir_idr_len**: Virtual IDR interval for SmartP; `0` uses the GOP length.
- **svc**: Enable SVC (default: `false`).
- **motion_deblur**: Enable motion deblur (default: `false`).
- **min_qp**, **max_qp**, **min_iqp**, **max_iqp**: QP window (0 keeps the driver value). SVC, deblur and QP are applied live.

## Sub stream section

- **enable**: Boolean to create the second H.26x channel (default: `false`).
- **path**: RTSP path (default: `/live/1`).
- **codec**: `H.264` or `H.265` (default: `H.265`).
- **mode**: Rate-control mode, same values as the MP4 section.
- **width**, **height**, **fps**, **gop**, **bitrate**, **profile**: Encoder parameters (defaults `640x360`, `10`, `20`, `512`, `1`).

The channel is created at boot when `enable` is true, before JPEG, because that
order keeps the main OSD working on RV1106.  Enabling a channel that does not
exist, or changing encoder parameters at runtime, saves the configuration and
restarts the application.  Disabling keeps the channel allocated until the next
restart so re-enabling is instant.

## AIISP section

- **enable**: `off`, `auto` or `on` (default: `off`).
- **model_dir**: Directory that contains `rkmodel_1080_1920.aiisp` (default: `/oem/usr/lib`). The board config exports `RK_AIISP_MODEL` so only the matching model is installed.
- **buf_cnt**: AIISP frame buffers (default: `1` on RV1106).
- **fps_enabled**: Encoder rate while the AINR model is active (default: `30`).
- **fps_disabled**: Encoder rate while the model is bypassed (default: `20`).

## IVS section

- **enable**: Boolean to start motion/object analytics (default: `false`).
- **mode**: `md`, `od` or `md_od` (default: `md`).
- **width**, **height**: Analytics input size (default `640x360`).
- **sensitivity**: MD sensitivity 1-3 (default: `2`).
- **md_interval**, **od_interval**: Detection intervals (defaults `5`, `1`).
- **od_percent**: OD confidence threshold 5-8 (default: `7`).
- **act_http_post**: Upload a JPEG snapshot on motion (default: `false`).
- **act_record**: Write a short clip on motion (default: `false`).
- **act_osd**: Show `MOTION HH:MM:SS` in the OSD for 10 s (default: `false`).
- **record_seconds**: Clip length (default: `15`).
- **record_source**: `main` or `sub` (default: `main`).

IVS uses the last VPSS channel (index 3). It cannot run together with MJPEG.

## IQ section

- **main_scene**, **sub_scene**: Scene names passed to `rk_aiq_uapi2_sysctl_switch_scene`.
- **file**: IQ file passed to `rk_aiq_uapi2_sysctl_updateIq` (empty to skip).

## HDR section

- **mode**: `normal`, `hdr2` or `hdr3` (default: `normal`). HDR is currently refused by the API on SC230AI because the AIQ HDR prepare fails; a hand-edited value falls back to normal at start.
- **apply**: `auto`, `dyn` or `restart` (default: `auto`). `dyn` is unavailable in the RV1106 `librkaiq`.

## Stream section

Pushes the main channel as raw RTP over UDP, independently of `rtsp.enable` and
of the built-in RTSP server. Each NAL unit is sent in its own RTP packet
(payload type 96); NAL units larger than 1400 bytes are fragmented as RFC 6184
FU-A (H.264) or RFC 7798 FU (H.265). H.265 fragmentation is only correct as of
the OKO fix, older builds sent a two-byte FU header and no receiver could
depacketize them.

- **enable**: Boolean to turn on special streaming methods (default: `false`).
  The whole `dest` list is only parsed when this is `true`.
- **udp_srcport**: Source port for UDP streaming (default: `5600`). The socket
  binds this port, packets are sent from it, and it is also the destination
  port when a `udp://host` entry has no explicit port.
- **dest**: List of destination URLs for streaming (e.g., `- udp://239.255.255.0:5600 or - rtmp://myserver/path/mykey`).
  Up to four entries. The multicast flag for `udp://` comes from the first
  octet of the host part: `224..239` is IPv4 multicast, everything else is
  unicast. An automatically registered sender (for example MediaMTX playing the
  built-in RTSP stream, which creates no RTP traffic in this direction) expires
  after 60 s of silence; destinations from this list never expire.
  This section is independent of `rtsp.enable`: the built-in RTSP server
  publishes the same main channel on its own.

## Audio section

- **enable**: Boolean to activate or deactivate audio functionality.
- **bitrate**: Audio bitrate in kbps (e.g., `128`).
- **gain**: Audio gain in decibels (e.g., `0` for no gain).
- **srate**: Audio sampling rate in Hz (e.g., `44100`).

## MP4 section

- **enable**: Boolean to activate MP4 output (required by live streams and RTSP).
- **codec**: Codec used for encoding (H.264 or H.265).
- **mode**: Encoding mode.
- **width**: Video width in pixels.
- **height**: Video height in pixels.
- **fps**: Frames per second.
- **gop**: Interval between keyframes.
- **profile**: Encoding profile.
- **bitrate**: Bitrate in kbps.

## OSD section

- **enable**: Boolean to turn on On-Screen Display regions globally, used to reduce resource usage or let another app manage the functionality (default: `true`).
- **scope**: `main` (default) attaches regions to the main VENC channel only; `all` would also attach the same region handle to the sub and JPEG channels. `all` is refused on this build because the RV1106 RGN multi-attach gate has not passed.
- **regX_img**: Path to the image for OSD region X.
- **regX_text**: Text displayed in OSD region X.
- **regX_font**: Font used for text in OSD region X.
- **regX_opal**: Opacity of OSD region X.
- **regX_posx**: X position of OSD region X.
- **regX_posy**: Y position of OSD region X.
- **regX_size**: Size of the text or image in OSD region X.
- **regX_color**: Color of the text or image in OSD region X.
- **regX_outl**: Outline color of the text in OSD region X.
- **regX_thick**: Thickness of the text outline in OSD region X.

## JPEG section

- **enable**: Boolean to activate JPEG output.
- **width**: Image width in pixels.
- **height**: Image height in pixels.
- **qfactor**: JPEG compression quality factor.

## MJPEG section

- **enable**: Boolean to activate MJPEG output.
- **mode**: Encoding mode.
- **width**: Video width in pixels.
- **height**: Video height in pixels.
- **fps**: Frames per second.
- **bitrate**: Bitrate in kbps.
- **qfactor**: JPEG compression quality factor.

## HTTP POST section

- **enable**: Boolean to activate HTTP POST snapshots.
- **host**: Hostname for HTTP POST requests.
- **url**: URL for HTTP POST requests.
- **login**: Login for HTTP POST authentication.
- **password**: Password for HTTP POST authentication.
- **width**: Image width sent in pixels.
- **height**: Image height sent in pixels.
- **interval**: Interval between requests in seconds.
- **qfactor**: JPEG compression quality factor.
