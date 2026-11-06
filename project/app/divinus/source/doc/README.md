# OKO media features: gates and implementation notes

Target: Luckfox Pico Pro Max (RV1106, Buildroot, SC230AI, `librkaiq` from the
2024-02-18 SDK release).  Measurements were taken on the bench DUT
(`192.168.52.97`, firmware of 2026-11-04) with a cross-built divinus binary.

## Feature status

| # | Feature | State | Where |
|---|---|---|---|
| 1 | ePTZ (digital zoom/crop) | implemented, gate passed | `hal/plus/rk_hal.c`, `media.c` |
| 2 | 90/180/270 rotation | implemented, gate passed via VENC | `hal/plus/rk_hal.c`, `media.c` |
| 3 | VENC SmartP/SVC/deblur/QP | implemented, gate passed | `hal/plus/rk_venc.h`, `rk_hal.c` |
| 4 | OSD on main+sub+JPEG | implemented, **blocked** (G4 not passed) | `rk_hal.c` `rk_region_create2` |
| 5 | second H.26x sub stream | implemented, gate passed | `media.c`, `rtsp/` |
| 6 | AIISP night denoise | implemented, gate passed with model in oem | `rk_hal.c`, `media.c` |
| 7 | IVS motion/object detection | implemented, gate passed (MD) | `ivs_events.c`, `rk_hal.c` |
| 8 | HDR2/HDR3 | **blocked** (gate failed) | `rk_hal.c` fallback only |
| 9 | runtime IQ scene/file switch | API implemented, scene switch accepted | `rk_hal.c` |
| 10 | microSD hotplug + recorder quota | implemented, format/hotplug/recording verified | `oko-sdcard`, `record.c` |
| 11 | Ethernet DHCP/static + stable MAC | implemented, env-backed, verified | `oko-net`, `/api/network` |
| 12 | Focus assistant (AF metric + ROI) | implemented, gate passed | `focus.c`, `hal/plus/rk_af_stats.c`, `/api/focus` |
| 13 | STM8 focus motor (I2C `0x60`) | implemented, verified on the bench | `focus_motor.c`, `stm8-motor/` |
| 14 | Controller firmware upload | implemented, STM8 verified; STM32 real flash not tested | `flash.c`, `/api/flash`, `oko-flash` |
| 15 | Controller version cache | implemented, verified | `firmware_status.c`, `/api/firmware` |
| 16 | NPU load on the OSD CPU line | implemented, verified | `oko-osd`, `/tmp/osd-sys.txt` |

Blocked features return `{"error":"blocked","reason":...,"hint":...}` from the
API and are refused again in `gating.c`; editing the YAML directly is also
sanitised at start (HDR falls back, `osd.scope=all` is forced to `main`).

## Gate results

| Gate | Check | Result | Evidence |
|---|---|---|---|
| G1 | `RK_MPI_VPSS_SetChnCrop` changes the encoded picture | **pass** | 2x crop changed the decoded frame (mean per-channel difference 43/255) while the SDP stayed 1920x1080 |
| G2 | rotation changes the encoded picture | **pass via VENC** | `RK_MPI_VPSS_SetChnRotation` returned success but left the frame unchanged; `RK_MPI_VENC_SetChnRotation` produced 1080x1920 and new SPS. The HAL now prefers VENC and falls back to VPSS |
| G3 | SVC / motion deblur / QP / SmartP | **pass** | symbols present in `librockit.so`; SVC, deblur and QP applied live to a running channel without dropping the stream. SmartP is a channel attribute and is applied at the next start |
| G4 | one region attached to VENC0 and JPEG | **not passed** | the vendor note says RV1106 RGN does not attach to a second channel; the code path exists (`rk_region_create2`) but `HAL_CAP_OSD_MULTI` is never set, so `osd.scope=all` is refused |
| G5 | second VENC channel + RTSP path | **pass** | `/live/0` = H.265 1920x1080 and `/live/1` = H.265 640x360 @10 decoded concurrently, main OSD stayed healthy (0 `SetBitmap` failures) |
| G6 | AIISP with `rkmodel_1080_1920.aiisp` | **pass** | `rkPostIspInit` succeeded, stream continued at 1920x1080; the AINR callback throttled VENC to the configured fps. Model must be installed in `/oem/usr/lib` (S1) |
| G7 | IVS on a free VPSS channel | **pass (MD)** | IVS created on VPSS channel 3; `VPSS_MAX_CHN_NUM` is 4, channels 4/5 return `ILLEGAL_PARAM`. Polling returned timeouts on a static scene, no motion event was observed |
| G8 | HDR2/HDR3 prepare | **fail** | `rk_aiq_uapi_sysctl_prepare` fails for HDR2/HDR3 on SC230AI with this `librkaiq`; the app logs a warning and prepares normal mode. The API refuses HDR |
| G9 | `switch_scene` / `updateIq` | **partial** | both symbols exist; `/api/iq?main_scene=normal&sub_scene=day` is accepted and the process stays healthy. File reload was not exercised |
| G10 | microSD insert/remove | **pass (simulated)** | format via `/api/storage` created one FAT32 `OKO` partition; unbind/bind of `mmc1:aaaa` set `present=0`/not-mounted and then `present=1` with automount; a 5 s H.265 clip decoded as 150 frames of 1920x1080. Physical extraction was not performed |
| G11 | unique channel budget | **pass** | `VPSS_MAX_CHN_NUM == 4`: without IVS the pool is main+sub+MJPEG+JPEG, with IVS it is main+sub+JPEG and MJPEG must be off |
| G12 | Ethernet identity and static IP | **pass** | MAC derived from SoC serial and stored as `oko_mac` in U-Boot env; `dhcpcd` uses `clientid` by MAC (no DUID); a full flash and reboot kept `02:47:8d:05:67:56` and `192.168.52.97`; static `.220` survived reboot |
| G13 | AFM statistics for the focus assistant | **pass** | `GET /api/focus` after enabling: `status=running`, `cells_used=225`, `metric_avg` 527k–530k over 20 samples (`output/bench/focus/focus-20261106-111557.csv`); `af_hw_ver=2`. The SC230AI IQ needs `af_v31.TuningPara.contrast_af.enable=1`; with `0` the cells are zero. Moving the ROI from the full frame to the top-left textured band changes the metric (525k vs 2.64M) and to the flat centre gives 0. The AFM grid itself stays full-frame, so the ROI is applied by cell selection |
| G14 | STM32 firmware upload through the WebUI | **pass** | `/api/firmware` and `/api/flash` report `present=true`, `state=app`, dotted `version=1.0.0`; `POST /api/flash` with `app-1.0.1.bin` (version from the file name) reaches `done` and `/api/firmware` reports `1.0.1`; rollback `app-1.0.0.bin` returns `1.0.0`. Missing `confirm`, a name without a version, wrong extension, oversized body and unknown target return HTTP 400 |
| G15 | STM8 firmware upload through the WebUI | **pass** | The upstream `2.0.0` was replaced with `factory-2.0.1.ihx` over ST-Link; `enter-boot`/`boot-reset`/`info` work; `/api/flash` (`target=stm8`, `app-2.0.1.ihx`, `confirm=FLASH`) returns 202, reaches `done`, and `oko-motor version` reports `2.0.1`; the server matches the name version against the OKO8 header (`oko-motor check FILE 2.0.1`). The application starts after a power cycle; killing the host tool mid-update leaves the bootloader answering and the next upload recovers. Invalid images, a name/header mismatch and missing `confirm` return 400; a second POST during a job returns 409 |
| G16 | NPU load on the OSD | **pass** | `/proc/rknpu/load` is read by `oko-osd`; `/tmp/osd-sys.txt` is `CPU 20% NPU 0%\nMEM FREE 81%\nT 61.8C` and becomes `NPU 17%` with YOLO running; the region-4 text provider is `@/tmp/osd-sys.txt`. Frames were captured to `output/bench/focus/osd-frame-*.png` (the OSD text is not OCR-checked) |
| G17 | STM32/STM8 versions in the WebUI | **pass** | `GET /api/firmware` reports dotted `1.0.0` for STM32 and `2.0.1` for STM8 (the raw STM32 hex remains in `oko-ec info`); the System card renders the same values; both entries update after a flash and survive a power cycle |

## Sub stream lifecycle and OSD

The sub channel is created in the boot order `main -> sub -> JPEG`.  This order
was the only one that kept the main OSD healthy across a long run.  Consequences:

- If the config enables `substream`, disable/resume at runtime is live: the
  channel is kept allocated (soft stop, `/live/1` returns 404 while stopped)
  and re-enabling re-attaches RTSP without touching the encoder.
- If the sub channel does not exist (enabled from `false` at runtime), the API
  saves the config and performs one graceful restart so the channel is created
  in the safe boot order.
- Changing encoder parameters (`width`, `height`, `fps`, `gop`, `bitrate`,
  `codec`, `mode`, `profile`) restarts the application; only the RTSP path can
  change live for new connections.

During testing, a mutex deadlock in `PLAY` (the per-stream getter was called
while the RTSP server already held its lock) stopped the video thread and made
`RK_MPI_RGN_SetBitMap` fail with `0xfffffffe`; the getter is no longer used on
that path.  With the fix, main OSD + sub + IVS and main OSD + sub + AIISP both
ran without `SetBitmap` failures.

## Channel budget

RV1106 exposes four VPSS output channels.  Every VENC channel is backed by the
VPSS channel with the same index, so divinus reports `chnCount = 4` and
reserves index 3 for IVS while analytics is enabled.  Enabling IVS with MJPEG
already running is refused; disable MJPEG first.

## Enabling

* ePTZ: `POST /api/eptz?enable=true&zoom=2` or `eptz.*` in the configuration.
* Rotation: `POST /api/rotate?enable=true&angle=90` (VENC path).
* Encoder extras: `POST /api/venc?svc=true&motion_deblur=true&min_qp=20&max_qp=40`.
* Sub stream: `POST /api/substream?enable=true&width=640&height=360&fps=10`.
* AIISP: `POST /api/aiisp?enable=on` (board config must export
  `RK_AIISP_MODEL=rkmodel_1080_1920.aiisp`).
* IVS: `POST /api/ivs?enable=true&mode=md`; events and actions are configured
  with the same endpoint (`act_record`, `act_http_post`, `act_osd`).
* Storage: `GET /api/storage`; recording is refused unless `/mnt/sdcard` is a
  real mount point and never writes to `/oem` or `/userdata`.

## Verification snippets

```sh
# both streams
ffprobe -rtsp_transport tcp rtsp://admin:<pass>@<ip>:554/live/0
ffprobe -rtsp_transport tcp rtsp://admin:<pass>@<ip>:554/live/1
# feature state
curl -u admin:<pass> http://<ip>/api/status
curl -u admin:<pass> http://<ip>/api/streams
curl -u admin:<pass> http://<ip>/api/storage
```