#!/usr/bin/env python3
"""End-to-end check of YOLO event clips and the services/events receiver.

Starts the `oko-events` compose profile, triggers the camera's synthetic event
(`POST /api/clip?test=1`), waits for the JPEG package and the fragmented-MP4
clip, verifies their duration and codecs with ffprobe, and (optionally) checks
the delivery retry path by stopping the receiver mid-flight.

    bench/run.sh event_check.py
    bench/run.sh event_check.py --retry
    bench/run.sh event_check.py --outbox --outbox-wait 900
    bench/run.sh event_check.py --require-detection

Artifacts (report.json) land in output/bench/events.
"""
import argparse
import base64
import json
import os
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HOST = os.environ.get("OKO_DUT_HOST", "192.168.52.33")
USER = os.environ.get("OKO_RTSP_USER", "admin")
PASSWORD = os.environ.get("OKO_DUT_PASSWORD", "oko-change-me")
SERVICE = os.environ.get("OKO_EVENTS_URL", "http://127.0.0.1:8090")
COMPOSE = ROOT / "services" / "events" / "docker-compose.yml"
EVENTS = Path(os.environ.get("OKO_EVENTS_DIR", ROOT / "output" / "srv" / "events"))
OUT = ROOT / "output" / "bench" / "events"
FFPROBE_IMAGE = "bluenviron/mediamtx:1.21.3-ffmpeg"


def request(url, method="GET", body=None, headers=None, timeout=20):
    req = urllib.request.Request(url, data=body, method=method)
    if headers:
        for key, value in headers.items():
            req.add_header(key, value)
    if url.startswith("http://%s" % HOST):
        token = base64.b64encode(("%s:%s" % (USER, PASSWORD)).encode()).decode()
        req.add_header("Authorization", "Basic " + token)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.status, resp.read()
    except urllib.error.HTTPError as err:
        return err.code, err.read()
    except Exception as err:                       # noqa: BLE001
        return 0, str(err).encode()


def dut(path, method="GET", body=None):
    status, payload = request("http://%s%s" % (HOST, path), method, body)
    try:
        return status, json.loads(payload.decode())
    except Exception:                              # noqa: BLE001
        return status, {}


def service(path):
    status, payload = request(SERVICE + path)
    try:
        return status, json.loads(payload.decode())
    except Exception:                              # noqa: BLE001
        return status, {}


def compose(*args):
    return subprocess.run(
        ["docker", "compose", "-f", str(COMPOSE)] + list(args),
        cwd=str(ROOT), capture_output=True, text=True)


def wait_service(timeout=60):
    deadline = time.time() + timeout
    while time.time() < deadline:
        status, dec = service("/health")
        if status == 200 and dec.get("ok"):
            return True
        time.sleep(1)
    return False


def ffprobe(path):
    if shutil.which("ffprobe"):
        cmd = ["ffprobe"]
    else:
        cmd = ["docker", "run", "--rm", "--entrypoint", "ffprobe",
               "-v", "%s:/media:ro" % EVENTS, FFPROBE_IMAGE]
        path = Path("/media") / Path(path).relative_to(EVENTS)
    cmd += ["-v", "error", "-show_entries",
            "stream=codec_name,width,height:format=duration",
            "-of", "json", str(path)]
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
        if out.returncode:
            return {}
        data = json.loads(out.stdout)
        streams = data.get("streams") or [{}]
        return {"codec": streams[0].get("codec_name"),
                "width": streams[0].get("width"),
                "height": streams[0].get("height"),
                "duration": float((data.get("format") or {}).get("duration", 0))}
    except Exception:                              # noqa: BLE001
        return {}


def event_ids():
    _status, dec = service("/api/events?limit=50")
    return {e.get("event_id") for e in dec.get("events", [])}


def trigger_test(retries=30):
    for _ in range(retries):
        status, dec = dut("/api/clip?test=1", "POST", b"")
        if status == 200 and dec.get("event") == "started":
            return True
        time.sleep(2)
    return False


def wait_event(before, timeout=120):
    deadline = time.time() + timeout
    while time.time() < deadline:
        _status, dec = service("/api/events?limit=50")
        for event in dec.get("events", []):
            if event.get("event_id") in before:
                continue
            if event.get("has_clip") and event.get("frames", 0) > 0:
                return event
        time.sleep(2)
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--no-start-service", action="store_true")
    parser.add_argument("--retry", action="store_true",
                        help="stop the receiver after the event and check that "
                             "the 30 s retry delivers it")
    parser.add_argument("--outbox", action="store_true",
                        help="keep the receiver down until the clip is spooled "
                             "to the microSD outbox (needs a card)")
    parser.add_argument("--outbox-wait", type=int, default=900)
    parser.add_argument("--require-detection", action="store_true",
                        help="fail when /api/yolo?detections=1 has no boxes")
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()

    OUT.mkdir(parents=True, exist_ok=True)
    report = {"host": HOST, "service": SERVICE, "checks": [], "ok": False}

    def check(name, ok, detail=""):
        report["checks"].append({"name": name, "ok": bool(ok),
                                 "detail": detail})
        print("%-32s %s %s" % (name, "ok" if ok else "FAIL", detail))
        return ok

    ok = True
    if not args.no_start_service:
        compose("up", "-d")
    ok &= check("service health", wait_service(args.timeout))

    status, clip = dut("/api/clip")
    ok &= check("clip state", status == 200 and clip.get("enable") and
                clip.get("running") and clip.get("available"),
                clip.get("reason", ""))
    if not ok:
        report["clip"] = clip
        (OUT / "report.json").write_text(json.dumps(report, indent=1))
        return 1
    pre = int(clip.get("pre_seconds", 5))
    post = int(clip.get("post_seconds", 10))

    status, yolo = dut("/api/yolo")
    check("yolo11 model", yolo.get("arch") == "yolo11",
          "%s runtime %s" % (yolo.get("model"), yolo.get("runtime")))

    before = event_ids()
    ok &= check("synthetic event accepted", trigger_test())

    event = wait_event(before, args.timeout)
    ok &= check("event received", event is not None,
                (event or {}).get("event_id", "timeout"))
    if event:
        report["event"] = event
        event_dir = EVENTS / event.get("date", "") / event["event_id"]
        clip_path = event_dir / "clip.mp4"
        h264_path = event_dir / "clip-h264.mp4"
        ok &= check("JPEG package", event.get("frames", 0) > 0,
                    "%d frames" % event.get("frames", 0))
        ok &= check("clip file", clip_path.is_file(),
                    "%d bytes" % clip_path.stat().st_size
                    if clip_path.is_file() else "missing")
        ok &= check("h264 browser copy", h264_path.is_file())
        hevc = ffprobe(clip_path) if clip_path.is_file() else {}
        h264 = ffprobe(h264_path) if h264_path.is_file() else {}
        report["clip_probe"] = hevc
        report["h264_probe"] = h264
        ok &= check("clip codec hevc", hevc.get("codec") == "hevc",
                    str(hevc))
        deadline = time.time() + 180
        while event.get("transcode") == "pending" and time.time() < deadline:
            _status, one = service("/api/events/%s" % event["event_id"])
            event = one or event
            time.sleep(2)
        ok &= check("browser codec h264", h264.get("codec") == "h264",
                    str(h264))
        duration = hevc.get("duration", 0)
        expected = pre + post
        ok &= check("clip duration", abs(duration - expected) <= 2.0,
                    "%.1f s, expected %d ±2" % (duration, expected))

    if args.retry:
        known = event_ids()
        compose("stop")
        check("receiver stopped", not wait_service(3))
        trigger_test()
        time.sleep(25)                 # the clip is finalized, first PUT fails
        compose("start")
        check("receiver restarted", wait_service(60))
        delivered = wait_event(known, 240)
        ok &= check("retry delivery", delivered is not None,
                    (delivered or {}).get("event_id", "timeout"))
        if delivered:
            report["retry_event"] = delivered

    if args.outbox:
        if not args.no_start_service:
            compose("build")
        # Keep the receiver down long enough for all three attempts to fail;
        # the fourth step is the microSD outbox.
        compose("down")
        time.sleep(5)
        trigger_test()
        deadline = time.time() + args.outbox_wait
        spooled = None
        while time.time() < deadline:
            _status, state = dut("/api/clip")
            if state.get("outbox_count", 0) > 0:
                spooled = state
                break
            time.sleep(10)
        ok &= check("outbox spool", spooled is not None,
                    str((spooled or {}).get("outbox_count")))
        compose("up", "-d")
        check("receiver restarted", wait_service(60))
        if spooled:
            deadline = time.time() + 180
            drained = False
            while time.time() < deadline:
                _status, state = dut("/api/clip")
                if state.get("outbox_count", 0) == 0:
                    drained = True
                    break
                time.sleep(5)
            ok &= check("outbox drain", drained)

    if args.require_detection:
        deadline = time.time() + 60
        found = False
        while time.time() < deadline and not found:
            status, dec = dut("/api/yolo?detections=1")
            for record in dec.get("detections") or []:
                if record.get("count", 0) > 0:
                    found = True
                    break
            time.sleep(2)
        ok &= check("live YOLO11 detections", found)

    report["ok"] = bool(ok)
    (OUT / "report.json").write_text(json.dumps(report, indent=1))
    print("report:", OUT / "report.json")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
