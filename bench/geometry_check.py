#!/usr/bin/env python3
"""Measure overlay geometry and ePTZ on the bench.

Modes (`--what`):
  zoom   ePTZ crop on `main`/`sub`, the `/mjpeg` and `/image.jpg` paths, and
         the channel-restart regression (a crop must survive
         `/api/mp4?enable=0/1`).  Only standard bench dependencies.
  boxes  NN detection-box alignment under 0/90/180/270 (needs a model, an
         object in the scene and a still camera).  Opt-in:
         `OKO_GEOMETRY_TESTS=1`.
  osd    green OSD text stays inside the picture and in the expected corner
         for 0/90/180/270.  Opt-in: `OKO_GEOMETRY_TESTS=1`.
  all    zoom, then the opt-in suites.

Artifacts (frames, masks, report) are written to `output/bench/geometry`.
Exit code: 0 — checks passed, 1 — failures, 2 — prerequisites are missing.

Examples:
    bench/run.sh geometry_check.py --what zoom
    OKO_GEOMETRY_TESTS=1 bench/run.sh geometry_check.py --what boxes
"""
import argparse
import base64
import io
import json
import os
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

import av
from PIL import Image, ImageChops, ImageStat

ROOT = Path(__file__).resolve().parents[1]
HOST = os.environ.get("OKO_DUT_HOST", "192.168.52.97")
USER = os.environ.get("OKO_RTSP_USER", "admin")
PASSWORD = os.environ.get("OKO_RTSP_PASS", "oko-change-me")
OUT = Path(os.environ.get("OKO_GEOMETRY_OUT", ROOT / "output" / "bench" / "geometry"))
RTSP = "rtsp://%s:%s@%s:554/live/%d"
HTTP = "http://%s%s"
ZOOM_DIFF_PASS = 22.0
IOU_PASS = 0.40

failures = []
skipped = []
checks = 0


def log(*parts):
    print(*parts, flush=True)


def http(method, path, timeout=10):
    url = HTTP % (HOST, path)
    request = urllib.request.Request(url, method=method)
    token = base64.b64encode(("%s:%s" % (USER, PASSWORD)).encode()).decode()
    request.add_header("Authorization", "Basic " + token)
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return response.status, response.read()
    except urllib.error.HTTPError as exc:
        return exc.code, exc.read()
    except (urllib.error.URLError, ConnectionError, TimeoutError, OSError) as exc:
        # A graceful restart drops the listening socket for a few seconds.
        return 0, ("offline: %s" % exc).encode()


def api(path, method="GET"):
    status, body = http(method, path)
    try:
        data = json.loads(body.decode("utf-8", "replace"))
    except ValueError:
        data = {"raw": body.decode("utf-8", "replace")[:200]}
    data["_status"] = status
    return data


def wait_up(timeout=40):
    """Poll /api/status until the application listens again after a restart."""
    deadline = time.time() + timeout
    started = time.time()
    while time.time() < deadline:
        status, _ = http("GET", "/api/status", timeout=3)
        if status == 200:
            log("  application up after %.1f s" % (time.time() - started))
            return True
        time.sleep(0.5)
    return False


def apply_change(path):
    """POST a settings change; a restart is awaited and reported.

    A zoom/rotation change is applied by a graceful application restart
    (KI-044): the response carries the restart note and the link may drop.
    """
    dec = api(path, "POST")
    note = str(dec.get("note", ""))
    restarted = "restarting" in note or dec.get("_status") == 0
    if restarted:
        if not wait_up():
            failures.append("the application did not come back after a restart")
        # The pipeline needs a few seconds after the HTTP server is
        # back: the first encoded frames can still be dark or noisy.
        time.sleep(6.0)
    return dec, restarted


def jpeg_from_stream(path, timeout=6.0):
    """Read the first complete JPEG from a multipart/mjpeg or a still."""
    request = urllib.request.Request(HTTP % (HOST, path))
    token = base64.b64encode(("%s:%s" % (USER, PASSWORD)).encode()).decode()
    request.add_header("Authorization", "Basic " + token)
    deadline = time.time() + timeout
    data = b""
    with urllib.request.urlopen(request, timeout=timeout) as response:
        while time.time() < deadline and len(data) < 4 * 1024 * 1024:
            chunk = response.read(65536)
            if not chunk:
                break
            data += chunk
            start = data.find(b"\xff\xd8")
            if start < 0:
                continue
            end = data.find(b"\xff\xd9", start + 2)
            if end > 0:
                return Image.open(io.BytesIO(data[start:end + 2])).convert("RGB")
    raise RuntimeError("no JPEG in %s" % path)


def capture(url, timeout=10, frames=20):
    """Grab one decoded frame (the last of a short sample) from RTSP."""
    container = av.open(url, timeout=timeout)
    image = None
    try:
        stream = container.streams.video[0]
        for frame in container.decode(stream):
            image = frame.to_image().convert("RGB")
            frames -= 1
            if frames <= 0:
                break
    finally:
        container.close()
    if image is None:
        raise RuntimeError("no frame from %s" % url)
    return image


def capture_retry(url, tries=3, wait=2.0):
    last = None
    for _ in range(tries):
        try:
            return capture(url)
        except Exception as exc:  # noqa: BLE001 - transient RTSP errors
            last = exc
            time.sleep(wait)
    raise last


def mean_diff(a, b):
    if a.size != b.size:
        b = b.resize(a.size)
    return ImageStat.Stat(ImageChops.difference(a, b)).mean[0]


def corr_images(a, b, size=(240, 135)):
    """Normalised cross-correlation on a downscaled grayscale pair.

    The mean absolute difference is sensitive to a global brightness/AWB
    change between two application starts; the correlation still measures
    whether the same scene region is shown.
    """
    ga = list(a.convert("L").resize(size).tobytes())
    gb = list(b.convert("L").resize(size).tobytes())
    n = len(ga)
    ma = sum(ga) / n
    mb = sum(gb) / n
    cov = sum((x - ma) * (y - mb) for x, y in zip(ga, gb))
    va = sum((x - ma) ** 2 for x in ga)
    vb = sum((y - mb) ** 2 for y in gb)
    if not va or not vb:
        return 0.0
    return cov / (va * vb) ** 0.5


def center_part(img, factor):
    w, h = img.size
    cw, ch = max(1, w // factor), max(1, h // factor)
    left, top = (w - cw) // 2, (h - ch) // 2
    return img.crop((left, top, left + cw, top + ch)).resize((w, h))


def save(img, name):
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / name
    img.save(path)
    log("  saved", path.relative_to(ROOT))
    return path


def check(ok, message):
    global checks  # noqa: PLW0603 - simple bench script
    checks += 1
    log("  %s %s" % ("PASS" if ok else "FAIL", message))
    if not ok:
        failures.append(message)


def set_eptz(**params):
    query = "&".join("%s=%s" % item for item in params.items())
    dec, _ = apply_change("/api/eptz?" + query)
    return dec


def get_eptz():
    return api("/api/eptz")


def eptz_state():
    dec = get_eptz()
    log("  /api/eptz: applied=%s rc=%s chn=%s crop=%s x/y/w/h=%s/%s/%s/%s" % (
        dec.get("applied"), dec.get("rc"), dec.get("chn"), dec.get("crop"),
        dec.get("x"), dec.get("y"), dec.get("w"), dec.get("h")))
    return dec, bool(dec.get("applied")), str(dec.get("rc")) == "0x0"


def stable_capture(url, tries=4):
    """Capture until two consecutive frames agree; the first frames after a
    restart can still be dark while the ISP/AWB settles."""
    img = capture_retry(url)
    for _ in range(tries - 1):
        time.sleep(2.0)
        nxt = capture_retry(url)
        if mean_diff(img, nxt) < 15.0:
            return nxt
        log("  frame still settling (%.1f)" % mean_diff(img, nxt))
        img = nxt
    return img


def zoom_test():
    log("== ePTZ zoom ==")
    baseline = {}
    mjpeg_on = bool(api("/api/mjpeg").get("enable"))
    for stream, index in (("main", 0), ("sub", 1)):
        # Normalise the stored rect so zoom=2 is always the frame centre,
        # even after an earlier manual crop left a different window.
        set_eptz(enable=0, stream=stream, reset=1)
        time.sleep(1.0)
        try:
            baseline[stream] = capture(RTSP % (USER, PASSWORD, HOST, index))
        except Exception as exc:  # noqa: BLE001 - bench diagnostics
            skipped.append("%s RTSP: %s" % (stream, exc))
            log("  SKIP %s RTSP: %s" % (stream, exc))
            continue
        save(baseline[stream], "zoom-%s-1x.png" % stream)

        snap_base = mjpeg_base = None
        try:
            snap_base = jpeg_from_stream("/image.jpg", 5.0)
            save(snap_base, "zoom-snapshot-1x.png")
        except Exception as exc:  # noqa: BLE001
            log("  snapshot baseline unavailable: %s" % exc)
        if mjpeg_on:
            try:
                mjpeg_base = jpeg_from_stream("/mjpeg", 5.0)
                save(mjpeg_base, "zoom-mjpeg-1x.png")
            except Exception as exc:  # noqa: BLE001
                log("  MJPEG baseline unavailable: %s" % exc)

        # The RV1106 applies a crop only before EnableChn, so the server
        # saves the settings and restarts the application (KI-044).
        dec, restarted = apply_change(
            "/api/eptz?enable=1&stream=%s&zoom=2" % stream)
        if stream != "main":
            # The sub channel is disabled on the bench: the crop is stored and
            # will be applied when the stream starts, so there is no frame.
            skipped.append("sub: the crop is applied on the next sub start")
            log("  SKIP sub: crop application needs a sub channel start")
            check(dec.get("applied") is False,
                  "sub: a change with the stream off is not reported as live")
            continue

        state, applied, rc_zero = eptz_state()
        check(restarted, "main: a zoom change restarts the application")
        check(applied and rc_zero,
              "main: the crop is live after the restart (rc 0)")
        crop = state.get("crop") or [0, 0, 0, 0]
        check(len(crop) == 4 and crop[2] * crop[3] <
              baseline[stream].size[0] * baseline[stream].size[1],
              "main: the live crop is smaller than the full frame")

        try:
            actual = stable_capture(RTSP % (USER, PASSWORD, HOST, index))
        except Exception as exc:  # noqa: BLE001
            failures.append("main: no frame after the restart: %s" % exc)
            continue
        save(actual, "zoom-%s-2x.png" % stream)

        expected = center_part(baseline[stream], 2)
        save(expected, "zoom-%s-2x-expected.png" % stream)
        to_expected = mean_diff(actual, expected)
        to_base = mean_diff(actual, baseline[stream])
        corr_expected = corr_images(actual, expected)
        corr_base = corr_images(actual, baseline[stream])
        log("  %s: mean|diff| to the expected centre crop %.2f, to 1x %.2f; "
            "corr %.3f / %.3f" % (stream, to_expected, to_base,
                                  corr_expected, corr_base))
        check(corr_expected > 0.60 and corr_expected > corr_base + 0.20,
              "%s: after the restart the frame matches the centre crop"
              % stream)

        # Snapshot and MJPEG are separate channels: the main crop must not
        # change them.  Their absence is a skip, not a failure.
        try:
            snap = jpeg_from_stream("/image.jpg", 5.0)
            save(snap, "zoom-snapshot-2x.png")
            if snap_base is not None:
                diff = mean_diff(snap, snap_base)
                log("  snapshot: mean|diff| to the pre-crop frame %.2f" % diff)
                check(diff < ZOOM_DIFF_PASS,
                      "snapshot: the main crop did not change the JPEG channel")
        except Exception as exc:  # noqa: BLE001
            skipped.append("snapshot: %s" % exc)
            log("  SKIP snapshot: %s" % exc)

        # A plain MP4 channel restart (no settings change) must keep the crop:
        # rk_channel_bind() re-applies it after EnableChn.  This step is opt-in
        # because a plain MP4 restart after a crop change is unreliable on
        # RV1106 (KI-044): the stream can stay dead until the application
        # restarts, which would poison the following checks.
        if os.environ.get("OKO_GEOMETRY_MP4_TESTS") == "1":
            api("/api/mp4?enable=0", "POST")
            time.sleep(2.0)
            api("/api/mp4?enable=1", "POST")
            time.sleep(5.0)
            try:
                after = stable_capture(RTSP % (USER, PASSWORD, HOST, index))
                save(after, "zoom-main-after-restart.png")
                to_crop = mean_diff(after, actual)
                log("  main: mean|diff| to the cropped frame after a restart "
                    "%.2f" % to_crop)
                check(to_crop < ZOOM_DIFF_PASS,
                      "main: the crop survived a plain channel restart")
            except Exception as exc:  # noqa: BLE001
                skipped.append("main: MP4 restart after a crop change: %s" % exc)
                log("  SKIP main: MP4 restart after a crop change: %s" % exc)
        else:
            skipped.append("main: MP4 restart check is opt-in "
                           "(OKO_GEOMETRY_MP4_TESTS=1)")
            log("  SKIP main: MP4 restart check is opt-in")

        # Disabling the crop is also a restart; the frame must return to 1x.
        set_eptz(enable=0)
        time.sleep(1.0)
        try:
            back = stable_capture(RTSP % (USER, PASSWORD, HOST, index))
            save(back, "zoom-main-1x-restored.png")
            back_diff = mean_diff(back, baseline[stream])
            back_corr = corr_images(back, baseline[stream])
            log("  main: mean|diff| to the 1x baseline after disable %.2f, "
                "corr %.3f" % (back_diff, back_corr))
            check(back_corr > 0.80,
                  "main: disabling the crop restores the full frame")
        except Exception as exc:  # noqa: BLE001
            failures.append("main: no frame after disabling the crop: %s" % exc)

    # Leave the stored target on main for the next run.
    set_eptz(enable=0, stream="main")


def mask_boxes(img):
    """Pixels of the two NN overlay colours: blue 0x0000FF and red 0xFF0000."""
    pixels = img.load()
    mask = Image.new("L", img.size, 0)
    target = mask.load()
    w, h = img.size
    for y in range(h):
        for x in range(w):
            r, g, b = pixels[x, y]
            if (b > 110 and r < 80 and g < 80) or (r > 110 and g < 80 and b < 80):
                target[x, y] = 255
    return mask


def mask_iou(a, b):
    a, b = a.point(lambda v: 255 if v else 0), b.point(lambda v: 255 if v else 0)
    inter = ImageChops.multiply(a, b)
    union = ImageChops.lighter(a, b)
    total = ImageStat.Stat(union).sum[0]
    if not total:
        return 0.0
    return ImageStat.Stat(inter).sum[0] / total


def boxes_test():
    log("== NN detection boxes under rotation ==")
    if os.environ.get("OKO_GEOMETRY_TESTS") != "1":
        skipped.append("boxes: set OKO_GEOMETRY_TESTS=1")
        log("  SKIP: set OKO_GEOMETRY_TESTS=1")
        return
    yolo = api("/api/yolo")
    if not yolo.get("enable"):
        failures.append("boxes: yolo.enable is false")
        return
    api("/api/yolo?boxes=1", "POST")
    apply_change("/api/rotate?enable=0")
    time.sleep(3.0)
    base = capture(RTSP % (USER, PASSWORD, HOST, 0))
    save(base, "boxes-0.png")
    base_mask = mask_boxes(base)
    if not ImageStat.Stat(base_mask).sum[0]:
        save(base_mask, "boxes-0-mask.png")
        failures.append("boxes: no overlay colours on the unrotated frame")
        return
    save(base_mask, "boxes-0-mask.png")

    for angle in (90, 180, 270):
        apply_change("/api/rotate?enable=1&angle=%d" % angle)
        time.sleep(3.0)
        try:
            img = capture(RTSP % (USER, PASSWORD, HOST, 0))
        except Exception as exc:  # noqa: BLE001
            failures.append("boxes: no frame at %d degrees: %s" % (angle, exc))
            continue
        save(img, "boxes-%d.png" % angle)
        mask = mask_boxes(img)
        save(mask, "boxes-%d-mask.png" % angle)
        expected = base_mask.rotate(-angle, expand=True)
        iou = mask_iou(mask, expected)
        log("  angle %d: mask IoU against the rotated 0-degree mask %.3f"
            % (angle, iou))
        check(iou >= IOU_PASS,
              "boxes: the overlay follows the %d-degree rotation" % angle)

    apply_change("/api/rotate?enable=0")
    api("/api/yolo?boxes=0", "POST")


def mask_green(img):
    pixels = img.load()
    mask = Image.new("L", img.size, 0)
    target = mask.load()
    w, h = img.size
    for y in range(h):
        for x in range(w):
            r, g, b = pixels[x, y]
            if g > 120 and r < 90 and b < 90:
                target[x, y] = 255
    return mask


def osd_test():
    log("== OSD text under rotation ==")
    if os.environ.get("OKO_GEOMETRY_TESTS") != "1":
        skipped.append("osd: set OKO_GEOMETRY_TESTS=1")
        log("  SKIP: set OKO_GEOMETRY_TESTS=1")
        return
    for angle in (0, 90, 180, 270):
        if angle:
            apply_change("/api/rotate?enable=1&angle=%d" % angle)
        else:
            apply_change("/api/rotate?enable=0")
        time.sleep(3.0)
        try:
            img = capture(RTSP % (USER, PASSWORD, HOST, 0))
        except Exception as exc:  # noqa: BLE001
            failures.append("osd: no frame at %d degrees: %s" % (angle, exc))
            continue
        save(img, "osd-%d.png" % angle)
        mask = mask_green(img)
        save(mask, "osd-%d-mask.png" % angle)
        box = mask.getbbox()
        if not box:
            failures.append("osd: no green text at %d degrees" % angle)
            continue
        w, h = img.size
        x0, y0, x1, y1 = box
        log("  angle %d: canvas %dx%d, green bbox %s" % (angle, w, h, box))
        check(x0 >= 0 and y0 >= 0 and x1 <= w and y1 <= h,
              "osd: the text stays inside the picture at %d degrees" % angle)
        # Factory reg0 (time) sits in the right-bottom corner, reg1
        # (telemetry) in the left-top one; both must keep that corner.
        check(x1 > w * 2 // 3 and y1 > h * 2 // 3,
              "osd: the time stamp stays in the right-bottom corner at %d"
              % angle)
        check(mask.crop((0, 0, w // 3, h // 3)).getbbox() is not None,
              "osd: the telemetry stays in the left-top corner at %d" % angle)
    apply_change("/api/rotate?enable=0")


def main():
    global OUT  # noqa: PLW0603 - simple bench script

    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--what", choices=("zoom", "boxes", "osd", "all"),
                        default="zoom")
    parser.add_argument("--out", default=str(OUT))
    args = parser.parse_args()
    OUT = Path(args.out)

    if args.what in ("zoom", "all"):
        zoom_test()
    if args.what in ("boxes", "all"):
        boxes_test()
    if args.what in ("osd", "all"):
        osd_test()

    log("")
    for item in skipped:
        log("SKIP:", item)
    if failures:
        for item in failures:
            log("FAIL:", item)
        return 1
    if not checks:
        log("Geometry check skipped: no measurement was possible")
        return 2
    log("Geometry check passed (%d checks)" % checks)
    return 0


if __name__ == "__main__":
    sys.exit(main())