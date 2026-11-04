# API Endpoints Reference

This document describes the REST API endpoints available in Divinus' web server.

## General Response Format

All APIs return responses in JSON format with appropriate HTTP status codes.

## Authentication

If authentication is enabled, all requests must include a basic authentication header:

```
Authorization: Basic [ENCODED_CREDENTIALS]
```

where \[ENCODED_CREDENTIALS\] is a string representing a Base64-encoded string formatted this way: `username:password`. For example, if your username was "admin", and your password "secret", the header's value would be encoded from "admin:secret" to "YWRtaW46c2VjcmV0".

## Endpoints

### System Management

#### `/api/cmd`

Calls various integrated commands.

| Method | Parameters | Description                     |
|--------|------------|---------------------------------|
| GET    | `save`     | Saves the current configuration |
| GET    | `restart`  | Restarts the streaming service  |

**Response**
```json
{
  "code": 0  // 0 = success, !0 = error code
}
```

#### `/api/isp`

Configures ISP (image signal processor) parameters.

| Method | Parameters   | Description                |
|--------|--------------|----------------------------|
| GET    | `mirror`     | Enable/disable mirroring   |
| GET    | `flip`       | Enable/disable flipping    |
| GET    | `antiflicker`| Antiflicker in Hz (-1=off) |

**Response**
```json
{
  "mirror": false,
  "flip": false,
  "antiflicker": 60
}
```

#### `/api/status`

Gets live system status information.

| Method | Parameters | Description                |
|--------|------------|----------------------------|
| GET    | none       | Returns system information |

**Response**
```json
{
  "chip": "rv11xx",
  "loadavg": [0.05, 0.07, 0.06],
  "memory": "62/121MB",
  "sensor": "sc230ai",
  "temp": "45.0°C",
  "uptime": "7 days, 14:08:57",
  "features": {
    "eptz": true,
    "rotate": true,
    "substream": false,
    "aiisp": true,
    "ivs": true,
    "hdr": false,
    "sd": false
  }
}
```

#### `/api/time`

Configures or reads the real-time clock.

| Method | Parameters | Description                                |
|--------|------------|--------------------------------------------|
| GET    | `fmt`      | Time display format (used by dynamic OSDs) |
| GET    | `ts`       | Unix timestamp to set (assuming local TZ)  |

**Response**
```json
{
  "fmt": "%Y-%m-%d %H:%M:%S",
  "ts": 1716633815
}
```

### Video Configuration

#### `/api/jpeg`

Configures JPEG capture parameters.

| Method | Parameters | Description             |
|--------|------------|-------------------------|
| GET    | `width`    | Image width (px)        |
| GET    | `height`   | Image height (px)       |
| GET    | `qfactor`  | Quality factor (1-100%) |

**Response**
```json
{
  "enable": true,
  "width": 1920,
  "height": 1080,
  "qfactor": 80
}
```

#### `/api/mjpeg`

Configures the MJPEG stream.

| Method | Parameters | Description                     |
|--------|------------|---------------------------------|
| GET    | `enable`   | Enable/disable MJPEG stream     |
| GET    | `width`    | Video width (px)                |
| GET    | `height`   | Video height (px)               |
| GET    | `fps`      | Frames per second               |
| GET    | `mode`     | Compression mode (CBR, VBR, QP) |

**Response**
```json
{
  "enable": true,
  "width": 640,
  "height": 480,
  "fps": 15,
  "mode": "CBR",
  "bitrate": 1000000
}
```

#### `/api/mp4`

Configures the H.26x MP4 stream.

| Method | Parameters | Description                                |
|--------|------------|--------------------------------------------|
| GET    | `enable`   | Enable/disable MP4 stream                  |
| GET    | `width`    | Video width (px)                           |
| GET    | `height`   | Video height (px)                          |
| GET    | `fps`      | Frames per second                          |
| GET    | `bitrate`  | Bits per second                            |
| GET    | `h265`     | Use H.265 instead of H.264                 |
| GET    | `mode`     | Compression mode (CBR, VBR, QP, ABR, AVBR) |
| GET    | `profile`  | Profile (BP/BASELINE, MP/MAIN, HP/HIGH)    |

**Response**
```json
{
  "enable": true,
  "width": 1280,
  "height": 720,
  "fps": 30,
  "h265": false,
  "mode": "VBR",
  "profile": "MP",
  "bitrate": 2000000
}
```

### Audio Configuration

#### `/api/audio`

Configures the audio parameters.

| Method | Parameters | Description                   |
|--------|------------|-------------------------------|
| GET    | `enable`   | Enable/disable audio          |
| GET    | `bitrate`  | Bits per second               |
| GET    | `gain`     | Audio gain (dB amplification) |
| GET    | `srate`    | Sample rate in Hz             |

**Response**
```json
{
  "enable": true,
  "bitrate": 32000,
  "gain": 50,
  "srate": 16000
}
```

### Night Mode

#### `/api/night`

Configures the night mode parameters.

| Method | Parameters     | Description               |
|--------|----------------|---------------------------|
| GET    | `enable`       | Enable/disable night mode |
| GET    | `manual`       | Manual or automatic mode  |
| GET    | `grayscale`    | Switch to grayscale       |
| GET    | `ircut`        | Control IR-Cut filter     |
| GET    | `ircut_pin1`   | GPIO pin 1 for IR-Cut     |
| GET    | `ircut_pin2`   | GPIO pin 2 for IR-Cut     |
| GET    | `irled`        | Control IR LEDs           |
| GET    | `irled_pin`    | GPIO pin for IR LEDs      |
| GET    | `irsense_pin`  | GPIO pin for IR sensor    |
| GET    | `adc_device`   | ADC device                |
| GET    | `adc_threshold`| Activation threshold      |

**Response**
```json
{
  "active": true,
  "manual": false,
  "grayscale": true,
  "ircut": true,
  "ircut_pin1": 17,
  "ircut_pin2": 18,
  "irled": true,
  "irled_pin": 20,
  "irsense_pin": 21,
  "adc_device": "/dev/adc",
  "adc_threshold": 800
}
```

### On-Screen Display (OSD)

#### `/api/osd/{id}`

Configures text or image overlays by their ID (0-9 at the moment).

| Method | Parameters | Description                                        |
|--------|------------|----------------------------------------------------|
| GET    | `text`     | Text to display (special specifiers supported)     |
| GET    | `img`      | Local image path to display (`text` must be empty) |
| GET    | `font`     | Font name to be used                               |
| GET    | `size`     | Font size (decimal in pt)                          |
| GET    | `color`    | Font color (hex format, RGB555 format)             |
| GET    | `opal`     | Opacity level (0-255)                              |
| GET    | `pos`      | Position on main stream \[x,y\]                    |
| GET    | `posx`     | X coordinate (write-only)                          |
| GET    | `posy`     | Y coordinate (write-only)                          |
| GET    | `outl`     | Outline color (hex format, RGB555 format)          |
| GET    | `thick`    | Outline thickness (0 to disable)                   |
| POST   | `file`     | Bitmap or PNG image to upload (replaces text)      |

**Response**
```json
{
  "id": 0,
  "color": "#ffff",
  "opal": 255,
  "pos": [16, 64],
  "font": "UbuntuMono-Regular",
  "size": 15.0,
  "text": "Backyard (%T)",
  "outl": "#8000",
  "thick": 0.0
}
```

### Recordings

#### `/api/record`

Manages video recording operations.

| Method | Parameters         | Description                                        |
|--------|--------------------|----------------------------------------------------|
| GET    | `continuous`       | Adjusts the operation mode to be uninterruptible   |
| GET    | `path`             | Specifies the location of the resulting files      |
| GET    | `filename`         | Sets the base output name (extension needed)       |
| GET    | `segment_duration` | Sets the maximum segment duration (seconds)        |
| GET    | `segment_size`     | Sets the maximum segment size (bytes)              |
| GET    | `start`            | Starts a new recording session                     |
| GET    | `stop`             | Stops the current recording session                |

**Response**
```json
{
  "recording": true,
  "start_time": "2025-05-08T14:30:00Z",
  "continuous": true,
  "path": "/mnt/sdcard/recordings",
  "filename": "Entrance.mp4",
  "segment_duration": 0,
  "segment_size": 10485760,
  "source": "main",
  "max_bytes": 0,
  "min_free_mb": 2,
  "sd": true
}
```

`source` selects the VENC channel, `max_bytes` enables the ring quota and
`min_free_mb` reserves space. Recording is allowed only below the real
`/mnt/sdcard` mount point; paths under `/oem`, `/userdata`, `/tmp` or `/` are
refused, and `start=1` returns `blocked` with a reason when the card is missing.
If the card disappears while recording, the current segment is closed and no
further file is created on SPI NAND. Formatting or unmounting the card sets
`storageBusy`, so a concurrent `start=1` is refused instead of opening a file
that would be formatted.

#### `/api/eptz`

Digital zoom (VPSS crop).

| Method | Parameters | Description |
|--------|------------|-------------|
| GET | `enable` | `true`/`false` |
| GET | `stream` | `main`/`sub` |
| GET | `x`, `y`, `w`, `h` | Crop rectangle in sensor pixels |
| GET | `zoom` | Preset `1`, `2`, `4`, `8` (centered) |
| GET | `reset` | Reset to the full frame |

Blocked with `reason` when the HAL has no `RK_MPI_VPSS_SetChnCrop`.

#### `/api/rotate`

| Method | Parameters | Description |
|--------|------------|-------------|
| GET | `enable` | `true`/`false` |
| GET | `stream` | `main`/`sub` |
| GET | `angle` | `0`, `90`, `180`, `270` |

Rotation is applied with `RK_MPI_VENC_SetChnRotation`; the encoded SPS
dimensions change accordingly.

#### `/api/venc`

| Method | Parameters | Description |
|--------|------------|-------------|
| GET | `smartp` | SmartP GOP (next start) |
| GET | `vir_idr_len` | Virtual IDR length |
| GET | `svc` | SVC on/off |
| GET | `motion_deblur` | Motion deblur on/off |
| GET | `min_qp`, `max_qp`, `min_iqp`, `max_iqp` | QP window |

SVC/deblur/QP are applied to the running channels; SmartP is stored for the
next start. The response contains a `note` that says so.

#### `/api/substream`

Second H.26x stream served at `/live/1`.

| Method | Parameters | Description |
|--------|------------|-------------|
| GET | `enable` | `true`/`false` |
| GET | `path` | RTSP path |
| GET | `codec` | `H.264`/`H.265` |
| GET | `mode` | `CBR`/`VBR`/`QP`/`AVBR` |
| GET | `width`, `height`, `fps`, `gop`, `bitrate`, `profile` | Encoder parameters |

Disable keeps the encoder channel allocated (soft stop, `/live/1` returns 404)
so re-enabling is immediate.  If the channel does not exist yet, or when
encoder parameters change, the setting is saved and the application performs
one graceful restart to create the channel in the RGN-safe
`main -> sub -> JPEG` order.  With IVS running there are only three VENC slots.

#### `/api/streams`

```json
{
  "connections": 1,
  "max_connections": 16,
  "streams": [
    {"id": 0, "path": "/live/0", "channel": 0, "codec": "H.265", "enabled": true},
    {"id": 1, "path": "/live/1", "channel": 2, "codec": "H.265", "enabled": true}
  ]
}
```

#### `/api/aiisp`

| Method | Parameters | Description |
|--------|------------|-------------|
| GET | `enable` | `off`/`auto`/`on` |
| GET | `model_dir` | Directory with `rkmodel_1080_1920.aiisp` |
| GET | `buf_cnt` | AIISP frame buffers |

Blocked when the model is missing or the symbol is absent. The AINR callback
throttles the encoder to `fps_enabled`/`fps_disabled`.

#### `/api/ivs`

| Method | Parameters | Description |
|--------|------------|-------------|
| GET | `enable` | Start/stop analytics |
| GET | `mode` | `md`/`od`/`md_od` |
| GET | `width`, `height`, `sensitivity` | Analytics input |
| GET | `md_interval`, `od_interval`, `od_percent` | Detection tuning |
| GET | `act_http_post`, `act_record`, `act_osd` | Motion actions |
| GET | `record_seconds`, `record_source` | Clip settings |

The response also contains `events`, the last 16 detections:
```json
{"events":[{"time":"2026-11-04T09:15:00Z","frame":1234,"rects":1,"bbox":[100,80,200,300]}]}
```

#### `/api/iq`

| Method | Parameters | Description |
|--------|------------|-------------|
| GET | `main_scene`, `sub_scene` | `rk_aiq_uapi2_sysctl_switch_scene` |
| GET | `file` | `rk_aiq_uapi2_sysctl_updateIq` |

The response lists `scenes` (subdirectories) and `files` (`.json`/`.xml`) found
in `/oem/usr/share/iqfiles`.

#### `/api/hdr`

| Method | Parameters | Description |
|--------|------------|-------------|
| GET | `mode` | `normal`, `hdr2`, `hdr3` |
| GET | `apply` | `auto`, `dyn`, `restart` |

On this SC230AI build HDR prepare fails; the endpoint is blocked with a reason
and a hand-edited mode falls back to normal after a restart.

#### `/api/network`

Reads and writes the Ethernet configuration stored in the U-Boot env partition.
The WebUI calls it from **Network → Ethernet**. `mode` is `dhcp` or `static`;
`ipaddr`, `netmask`, `gateway` and `dns` are IPv4 dotted quads.

| Method | Parameters | Description |
|--------|------------|-------------|
| GET | none | Current env settings plus live IP/MAC and DHCP state |
| GET | `mode=dhcp` | Store DHCP mode; `oko-net apply` restarts `dhcpcd` |
| GET | `mode=static&ipaddr=...&netmask=...&gateway=...&dns=...` | Store static settings, stop the DHCP client and apply them |

**Response**

```json
{
  "mode": "dhcp",
  "ipaddr": "",
  "netmask": "",
  "gateway": "",
  "dns": "",
  "mac": "02:47:8d:05:67:56",
  "current_ip": "192.168.52.97",
  "current_mac": "02:47:8d:05:67:56",
  "dhcp_running": true
}
```

`mac` and the settings are persisted as `oko_*` variables in `/dev/mtd0`
through `fw_printenv`/`fw_setenv`, so they survive a `/userdata` or rootfs
reset. The MAC is derived once from `/proc/device-tree/serial-number`; DHCP
uses the MAC as client ID (no DUID), which keeps the lease stable across
firmware updates. Static mode returns after the response has been sent; the
browser may need to reconnect to the new address.

#### `/api/storage`

Reports the microSD state and performs mount/unmount/format operations.

| Method | Parameters | Description |
|--------|------------|-------------|
| GET | none | State plus a one-shot `format_token` |
| GET | `action=mount` | Mount the detected card on `/mnt/sdcard` |
| GET | `action=unmount&token=<format_token>` | Unmount the card; refused while recording |
| GET | `action=format&fs=fat32&confirm=FORMAT&token=<format_token>` | Wipe the whole removable card, create one MBR/FAT32 partition labelled `OKO`, then mount it |

**State response**

```json
{
  "formatted": false,
  "action": "",
  "sd_mounted": true,
  "total_mb": 30528,
  "free_mb": 12000,
  "path": "/mnt/sdcard/recordings",
  "max_bytes": 0,
  "min_free_mb": 2,
  "recording": false,
  "blocked": false,
  "reason": "",
  "device": "/dev/mmcblk0p1",
  "fs": "vfat",
  "format_supported": true,
  "format_busy": false,
  "format_token": "0123456789abcdef"
}
```

`format_supported` is true only when `/usr/bin/oko-sdcard`, `sfdisk` and
`mkfs.vfat` exist in the image; `wipefs` is used when present but is optional
(`sfdisk` overwrites the partition table). `format_busy` is true while recording or
while a format/unmount is running. `format_token` changes after every successful
format; `confirm=FORMAT` and the current token are both required, so a CSRF or
stale WebUI request cannot destroy the card.

Failures return HTTP 200 with
`{"error":"blocked"|"failed","reason":"...","hint":"..."}`; no partial state
JSON is returned. Only an SD whole `mmcblk` disk (`device/type=SD`, not eMMC
`MMC`) is accepted; `/dev/mtd*`,
`/dev/ubi*`, `/dev/root` and eMMC boot partitions are rejected. `format` and
`unmount` are refused with `reason":"stop recording first"` while a recording is
active. The helper is `project/cfg/BoardConfig_IPC/overlay/overlay-luckfox-buildroot-init/usr/bin/oko-sdcard`.


## Content Streaming

### `/image.jpg`

Captures an instant JPEG image.

| Method | Parameters    | Description          |
|--------|---------------|----------------------|
| GET    | `width`       | Snapshot width (px)  |
| GET    | `height`      | Snapshot height (px) |
| GET    | `qfactor`     | Quality factor       |
| GET    | `color2gray`  | Convert to grayscale |

**Response**: JPEG file

### `/mjpeg`

Continuous MJPEG stream. Requires `mjpeg.enable=true`; when disabled the
endpoint returns 404.

**Response**: multipart/x-mixed-replace stream

### `/video.mp4`

Continuous fragmented MP4 video stream. Requires `mp4.enable=true`. The muxer
builds `moov` from the VPS/SPS/PPS carried in the Annex B payload; the preview
works only if the browser can decode the selected codec (H.265 needs HEVC
support, otherwise use `/mjpeg`).

**Response**: Segmented MP4 video stream

### `/video.264` or `/video.265`

Raw H.264/H.265 stream.

**Response**: Binary data stream

### `/audio.mp3`

Continuous MP3 audio stream.

**Response**: MP3 audio stream

### `/audio.pcm`

Raw PCM audio stream.

**Response**: Uncompressed PCM audio stream

## Special Commands

### `/exit`

Gracefully stops the server.

| Method | Parameters | Description      |
|--------|------------|------------------|
| GET    | none       | Stops the server |

**Response**: Message "Closing..."

### `/`, `/index.htm` or `/index.html`

WebUI homepage.

**Response**: HTML SPA with embedded resources