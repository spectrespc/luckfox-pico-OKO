#!/usr/bin/env python3
"""End-to-end check of the sleep heartbeat snapshot (proof-of-life JPEG).

Configures the camera for a short quiet sleep cycle, lets it run several
probe cycles and verifies that one JPEG per quiet cycle reaches the
`services/events` receiver with the expected headers.  Optional phases:

    bench/run.sh heartbeat_check.py
    bench/run.sh heartbeat_check.py --neg
    bench/run.sh heartbeat_check.py --cycles 3 --deploy
    bench/run.sh heartbeat_check.py --no-motion --keep-config

The motion phase needs divinus started with OKO_BENCH_API=1 (the
`POST /api/sleep?motion=1` injector is bench-only); without it the phase is
reported as skipped.  `--neg` stops the receiver and expects the heartbeat
failure streak to grow while the camera keeps cycling.

Artifacts (report.json, received JPEGs) land in output/bench/heartbeat.
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
import urllib.parse
import urllib.request
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HOST = os.environ.get("OKO_DUT_HOST", "192.168.52.33")
USER = os.environ.get("OKO_RTSP_USER", "admin")
PASSWORD = os.environ.get("OKO_DUT_PASSWORD", "oko-change-me")
SERVICE = os.environ.get("OKO_EVENTS_URL", "http://127.0.0.1:8090")
# The DUT must reach the receiver by its LAN address, not the host loopback.
RECEIVER = os.environ.get("OKO_EVENTS_ADDR", "192.168.52.200:8090")
COMPOSE = ROOT / "services" / "events" / "docker-compose.yml"
EVENTS = Path(os.environ.get("OKO_EVENTS_DIR", ROOT / "output" / "srv" / "events"))
OUT = ROOT / "output" / "bench" / "heartbeat"

SETTINGS = ["enable", "pmu_timer_wake", "aov_mcu", "aov_keep_isp",
            "idle_timeout_s", "wake_interval_s", "wake_check_s",
            "heartbeat", "heartbeat_interval_s", "heartbeat_retries"]


def request(url, method="GET", body=None, headers=None, timeout=8):
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


def dut(path, method="GET", body=None, timeout=8):
    status, payload = request("http://%s%s" % (HOST, path), method, body,
                              timeout=timeout)
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


def heartbeats(limit=100):
    _status, dec = service("/api/heartbeats?limit=%d" % limit)
    return dec.get("heartbeats", [])


def received_ms(item):
    """Host arrival time of a record, for the camera-clock drift check."""
    try:
        return int(datetime.fromisoformat(item.get("received_at")).timestamp() * 1000)
    except (TypeError, ValueError):
        return None


def wait_reachable(timeout=180):
    """Waits for the DUT HTTP API; a suspended camera is unreachable."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        status, dec = dut("/api/status", timeout=4)
        if status == 200 and dec.get("sleep"):
            return dec
        time.sleep(1)
    return None


def wait_heartbeat_state(timeout=120, want_enable=None):
    """Waits for a readable /api/sleep, optionally with heartbeat enabled."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        dec = sleep_state()
        if dec:
            hb = dec.get("heartbeat") or {}
            if want_enable is None:
                return dec
            if bool(dec.get("enable")) == bool(want_enable) and \
                    bool(hb.get("enable")) == bool(want_enable):
                return dec
        time.sleep(1)
    return None


def wait_streak_zero(timeout=180):
    """Waits for a moment when the last heartbeat send succeeded.

    The AP is only reachable while awake, and a failed attempt is followed by
    the suspend, so the sample is taken right after a successful cycle.
    """
    deadline = time.time() + timeout
    while time.time() < deadline:
        dec = sleep_state()
        hb = (dec or {}).get("heartbeat") or {}
        if dec and hb.get("fail_streak") == 0 and hb.get("last_sent_s", -1) >= 0:
            return hb
        time.sleep(0.5)
    return None


def restore_config(original, original_post, timeout=180):
    """Restores the pre-run settings; a suspended DUT must be retried."""
    if not original and not original_post:
        return True
    deadline = time.time() + timeout
    while time.time() < deadline:
        if wait_reachable(20) is None:
            continue
        if original:
            post_sleep(as_query(original))
        if original_post:
            post_http_post(as_http_query(original_post))
        dec = sleep_state() or {}
        hb = dec.get("heartbeat") or {}
        sleep_ok = (not original or
                    bool(dec.get("enable")) == bool(original.get("enable")))
        post_ok = (not original_post or
                   dec.get("heartbeat") is not None)
        if sleep_ok and post_ok and \
                bool(hb.get("enable")) == bool(original.get("heartbeat", False)):
            restart_streamer()
            return wait_reachable(60) is not None
        time.sleep(2)
    return False


def sleep_state(timeout=70):
    status, dec = dut("/api/sleep", timeout=6)
    if status == 200:
        return dec
    return None


def post_sleep(values):
    query = urllib.parse.urlencode(values)
    return dut("/api/sleep?" + query, "POST", b"")


def restart_streamer():
    dut("/api/cmd?restart", "POST", b"")


HTTP_KEYS = ["enable", "host", "url", "login", "width", "height", "interval",
             "qfactor", "receiver_url"]


def http_post_conf():
    status, dec = dut("/api/http_post")
    if status != 200:
        return {}
    return {key: dec[key] for key in HTTP_KEYS if key in dec}


def post_http_post(values):
    query = urllib.parse.urlencode(values)
    return dut("/api/http_post?" + query, "POST", b"")


def as_http_query(dec):
    out = {}
    for key, value in dec.items():
        if key not in HTTP_KEYS:
            continue
        if isinstance(value, bool):
            out[key] = "true" if value else "false"
        else:
            out[key] = str(value)
    return out


def cfg_from_sleep(dec):
    out = {}
    for key in SETTINGS:
        if key in dec:
            out[key] = dec[key]
    hb = dec.get("heartbeat")
    if isinstance(hb, dict):
        out["heartbeat"] = hb.get("enable", False)
    return out


def as_query(dec):
    out = {}
    for key in SETTINGS:
        if key not in dec:
            continue
        value = dec[key]
        if isinstance(value, bool):
            out[key] = "true" if value else "false"
        else:
            out[key] = str(value)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--no-start-service", action="store_true")
    parser.add_argument("--no-motion", action="store_true",
                        help="skip the motion-injection phase")
    parser.add_argument("--keep-config", action="store_true",
                        help="leave the bench sleep settings on the camera")
    parser.add_argument("--deploy", action="store_true",
                        help="copy the built divinus and restart it first")
    parser.add_argument("--receiver", default=RECEIVER,
                        help="receiver host:port as seen from the DUT "
                             "(env OKO_EVENTS_ADDR, default %s)" % RECEIVER)
    parser.add_argument("--cycles", type=int, default=3,
                        help="quiet heartbeat cycles to collect (default 3)")
    parser.add_argument("--neg", action="store_true",
                        help="receiver off: expect fail_streak to grow")
    parser.add_argument("--idle", type=int, default=30)
    parser.add_argument("--wake-interval", type=int, default=10)
    parser.add_argument("--wake-check", type=int, default=5)
    parser.add_argument("--timeout", type=int, default=420)
    args = parser.parse_args()

    OUT.mkdir(parents=True, exist_ok=True)
    report = {"host": HOST, "service": SERVICE, "checks": [], "ok": False}

    def check(name, ok, detail=""):
        report["checks"].append({"name": name, "ok": bool(ok),
                                 "detail": detail, "skipped": False})
        print("%-36s %s %s" % (name, "ok" if ok else "FAIL", detail))
        return ok

    def skip(name, detail=""):
        report["checks"].append({"name": name, "ok": None, "detail": detail,
                                 "skipped": True})
        print("%-36s SKIP %s" % (name, detail))

    ok = True
    if args.deploy:
        deployed = subprocess.run(
            [sys.executable, str(ROOT / "bench" / "deploy_divinus.py")],
            cwd=str(ROOT))
        ok &= check("deploy divinus", deployed.returncode == 0)
        wait_reachable(120)

    wait_reachable(120)
    status, rec = dut("/api/sleep")
    original = cfg_from_sleep(rec) if status == 200 else {}
    report["original"] = original
    original_post = http_post_conf()
    report["original_http_post"] = original_post

    # Gating needs an enabled [http_post] with a receiver host; the API applies
    # these switches live, while the sleep settings need the restart below.
    status, dec = post_http_post({
        "enable": "true",
        "host": args.receiver,
        "url": "/ingest/frame",
        "interval": "3600",
    })
    ok &= check("http_post receiver configured",
                status == 200 and not dec.get("error"),
                dec.get("note", ""))

    if not args.no_start_service and not args.neg:
        compose("up", "-d")
        ok &= check("service health", wait_service(args.timeout))

    target = {
        "enable": "true",
        "pmu_timer_wake": "true",
        "aov_mcu": "false",
        "aov_keep_isp": "false",
        "idle_timeout_s": str(args.idle),
        "wake_interval_s": str(args.wake_interval),
        "wake_check_s": str(args.wake_check),
        "heartbeat": "true",
        "heartbeat_interval_s": "0",
        "heartbeat_retries": "3",
    }
    status, dec = post_sleep(target)
    ok &= check("sleep settings accepted", status == 200 and not dec.get("error"),
                dec.get("warning", ""))
    restart_streamer()
    time.sleep(5)
    reachable = wait_reachable(120)
    ok &= check("streamer restarted", reachable is not None)
    if reachable is None:
        report["ok"] = False
        (OUT / "report.json").write_text(json.dumps(report, indent=1))
        return 1
    applied = wait_heartbeat_state(120, want_enable=True)
    ok &= check("sleep target applied (restart)",
                applied is not None and applied.get("enable") is True and
                (applied.get("heartbeat") or {}).get("enable") is True,
                str((applied or {}).get("heartbeat", {}).get("reason")))

    if args.neg:
        compose("stop")
        check("receiver stopped", not wait_service(3))
        before_hb = sleep_state() or {}
        base_fail = (before_hb.get("heartbeat") or {}).get("fail_streak", 0)
        deadline = time.time() + args.timeout
        grown = None
        while time.time() < deadline:
            dec = wait_reachable(60)
            streak = ((dec or {}).get("sleep", {})
                      .get("heartbeat_fail_streak"))
            if streak is not None and streak > base_fail:
                grown = dec["sleep"]
                break
            time.sleep(2)
        ok &= check("fail_streak grows without a receiver", grown is not None,
                    "fail_streak=%s (base %s)" % ((grown or {})
                    .get("heartbeat_fail_streak"), base_fail))
        dec = wait_reachable(120)
        ok &= check("camera keeps cycling", dec is not None,
                    (dec or {}).get("sleep", {}).get("state", ""))
        report["negative"] = grown
        if not args.keep_config:
            check("original settings restored",
                  restore_config(original, original_post))
        report["ok"] = bool(ok)
        (OUT / "report.json").write_text(json.dumps(report, indent=1))
        print("report:", OUT / "report.json")
        return 0 if ok else 1

    before = {item.get("file") for item in heartbeats()}
    deadline = time.time() + args.timeout
    received = []
    while time.time() < deadline and len(received) < args.cycles:
        for item in heartbeats():
            if item.get("file") in before:
                continue
            received.append(item)
            before.add(item.get("file"))
            print("heartbeat %s cycle=%s idle=%s area=%s" % (
                item.get("ts"), item.get("wake_cycle"), item.get("idle_s"),
                item.get("area")))
        time.sleep(2)

    ok &= check("heartbeat cycles", len(received) >= args.cycles,
                "%d/%d" % (len(received), args.cycles))
    report["heartbeats"] = received

    cycles = [item.get("wake_cycle") for item in received
              if isinstance(item.get("wake_cycle"), int)]
    ok &= check("wake cycle grows",
                len(cycles) >= 2 and cycles == sorted(cycles) and
                cycles[-1] > cycles[0], str(cycles))

    now_ms = int(time.time() * 1000)
    for item in received:
        name = "hb-%s.jpg" % item.get("ts")
        if item.get("url"):
            _status, body = request(SERVICE + item["url"])
            if body:
                (OUT / name).write_bytes(body)
        ok &= check("header sleep quiet (%s)" % item.get("ts"),
                    item.get("sleep") == "quiet", str(item.get("sleep")))
        ok &= check("header area (%s)" % item.get("ts"),
                    isinstance(item.get("area"), int) and item["area"] >= -1,
                    str(item.get("area")))
        # Compare with the arrival time of this record, not the end of the
        # run: a long soak would drift past any fixed tolerance.
        arrived = received_ms(item) or now_ms
        ok &= check("header ts close to host (%s)" % item.get("ts"),
                    isinstance(item.get("ts"), int) and
                    abs(arrived - item["ts"]) < 120 * 1000,
                    str(item.get("ts")))

    hb = wait_streak_zero(120)
    ok &= check("fail_streak resets after a success", hb is not None,
                str((hb or {}).get("fail_streak")))

    if not args.no_motion:
        motion = motion_phase(args)
        if motion is None:
            skip("motion cycle without heartbeat",
                 "OKO_BENCH_API=1 is off or the probe is too short "
                 "(try --wake-check 30)")
        else:
            ok &= check("motion cycle without heartbeat", motion[0], motion[1])
            report["motion"] = {"ok": motion[0], "detail": motion[1]}

    if not args.keep_config:
        ok &= check("original settings restored",
                    restore_config(original, original_post))

    report["ok"] = bool(ok)
    (OUT / "report.json").write_text(json.dumps(report, indent=1))
    print("report:", OUT / "report.json")
    return 0 if ok else 1


def motion_phase(args):
    """Injects motion during a probe window and checks the cycle gap.

    Returns (ok, detail) or None when the injector is unavailable.  A
    successful injection moves the policy from probe to awake; the cycle that
    would have sent a heartbeat must not produce one, while the next quiet
    cycle must.
    """
    current = heartbeats()
    known = {item.get("file") for item in current}
    last_cycle = max([item.get("wake_cycle") for item in current
                      if isinstance(item.get("wake_cycle"), int)] or [0])
    deadline = time.time() + args.timeout
    injected_at = None
    while time.time() < deadline:
        dec = sleep_state()
        state = (dec or {}).get("state")
        if state == "probe":
            dut("/api/sleep?motion=1", "POST", b"")
            injected_at = time.time()
            break
        time.sleep(0.5)
    if injected_at is None:
        return None

    moved = False
    deadline = injected_at + args.wake_check + 4
    while time.time() < deadline:
        dec = sleep_state()
        if (dec or {}).get("state") == "awake":
            moved = True
            break
        time.sleep(0.5)
    if not moved:
        return None

    # The motion cycle ends awake; after idle it suspends without a heartbeat,
    # then resumes and its probe cycle sends wake_cycle+1.
    deadline = time.time() + args.timeout
    while time.time() < deadline:
        fresh = [item for item in heartbeats() if item.get("file") not in known]
        bad = [item for item in fresh
               if isinstance(item.get("wake_cycle"), int) and
               item["wake_cycle"] <= last_cycle]
        good = [item for item in fresh
                if isinstance(item.get("wake_cycle"), int) and
                item["wake_cycle"] > last_cycle]
        if bad:
            return (False, "heartbeat came from motion cycle %s" % bad[0])
        if good:
            return (True, "next cycle %s" % good[0].get("wake_cycle"))
        time.sleep(2)
    return (False, "no heartbeat after the motion cycle")


if __name__ == "__main__":
    sys.exit(main())