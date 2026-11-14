#!/usr/bin/env python3
"""Soak the detector and the event-clip path.

Polls the camera and the events receiver, periodically triggers the synthetic
event when no real event arrived, and reports counters at the end:

    bench/run.sh clip_soak.py --minutes 120
    bench/run.sh clip_soak.py --minutes 10 --event-every 120

Pass criteria: the camera API never disappears, `/api/clip` stays enabled and
running, no event is lost, the outbox stays empty, and MemFree does not fall
below the start value by more than 24 MiB.  The report goes to
output/bench/events/soak-<timestamp>.json.
"""
import argparse
import base64
import json
import os
import sys
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "output" / "bench" / "events"
HOST = os.environ.get("OKO_DUT_HOST", "192.168.52.33")
USER = os.environ.get("OKO_RTSP_USER", "admin")
# The web UI uses the admin account, SSH uses the root account; they are
# different credentials on the bench.
PASSWORD = os.environ.get("OKO_WEB_PASSWORD", "oko-change-me")
SSH_PASSWORD = os.environ.get("OKO_DUT_PASSWORD", "luckfox")
SERVICE = os.environ.get("OKO_EVENTS_URL", "http://127.0.0.1:8090")


def request(url, method="GET", body=None, timeout=15):
    req = urllib.request.Request(url, data=body, method=method)
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


def json_get(url, method="GET", body=None):
    status, payload = request(url, method, body)
    try:
        return status, json.loads(payload.decode())
    except Exception:                              # noqa: BLE001
        return status, {}


_ssh = None


def _ssh_client():
    global _ssh
    if _ssh is None:
        import paramiko

        client = paramiko.SSHClient()
        client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        client.connect(HOST, username="root", password=SSH_PASSWORD, timeout=10)
        _ssh = client
    return _ssh


def mem_free():
    global _ssh
    try:
        out = _ssh_client().exec_command(
            "awk '/MemFree/{print $2}' /proc/meminfo")[1]
        return int(out.read().decode().strip())
    except Exception:                              # noqa: BLE001
        if _ssh is not None:
            try:
                _ssh.close()
            except Exception:                      # noqa: BLE001
                pass
            _ssh = None
        return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--minutes", type=int, default=120)
    parser.add_argument("--interval", type=int, default=10)
    parser.add_argument("--event-every", type=int, default=300,
                        help="trigger a test event when no event arrived for "
                             "this many seconds")
    args = parser.parse_args()

    OUT.mkdir(parents=True, exist_ok=True)
    start = time.time()
    deadline = start + args.minutes * 60
    samples = []
    mem_start = mem_free()
    mem_min = mem_start
    camera_failures = 0
    consecutive = 0
    last_event_at = 0.0
    last_event_count = None
    triggers = 0
    initial_events = None
    initial_sent = None
    lost_total = 0
    outbox_max = 0
    retries_max = 0
    dropped_start = None
    dropped_max = 0

    print("soak %.0f min, poll %d s, test event every %d s" %
          (args.minutes, args.interval, args.event_every))
    while time.time() < deadline:
        now = time.time()
        status, state = json_get("http://%s/api/clip" % HOST)
        sstatus, stats = json_get(SERVICE + "/api/stats")
        camera_ok = status == 200 and state.get("running")
        if camera_ok:
            consecutive = 0
            if initial_sent is None:
                initial_sent = state.get("sent")
            if dropped_start is None:
                dropped_start = state.get("ring_dropped", 0)
            dropped_max = max(dropped_max,
                              state.get("ring_dropped", 0) - dropped_start)
            lost_total = max(lost_total, state.get("lost", 0))
            outbox_max = max(outbox_max, state.get("outbox_count", 0))
            retries_max = max(retries_max, state.get("retries_failed", 0))
        else:
            consecutive += 1
            camera_failures += 1

        events = stats.get("events")
        if events is not None:
            if initial_events is None:
                initial_events = events
            if last_event_count is None or events > last_event_count:
                last_event_at = now
            last_event_count = events

        free = mem_free()
        if free is not None:
            mem_min = free if mem_min is None else min(mem_min, free)

        samples.append({"t": round(now - start, 1), "camera": camera_ok,
                        "sent": state.get("sent"), "lost": state.get("lost"),
                        "dropped": state.get("ring_dropped"),
                        "outbox": state.get("outbox_count"),
                        "events": events, "mem_free_kib": free})
        if len(samples) % 12 == 0:
            print("t=%5.0f s sent=%s lost=%s dropped=%s events=%s mem=%s" % (
                now - start, state.get("sent"), state.get("lost"),
                state.get("ring_dropped"), events, free))

        if now - last_event_at >= args.event_every:
            _status, dec = json_get("http://%s/api/clip?test=1" % HOST, "POST",
                                    b"")
            if dec.get("event") == "started":
                triggers += 1
                last_event_at = now

        time.sleep(max(1, args.interval))

    report = {
        "started": datetime.fromtimestamp(start, timezone.utc).isoformat(),
        "minutes": args.minutes,
        "camera_failures": camera_failures,
        "test_events": triggers,
        "lost": lost_total,
        "outbox_max": outbox_max,
        "retries_failed_max": retries_max,
        "ring_dropped_growth": dropped_max,
        "mem_free_kib_start": mem_start,
        "mem_free_kib_min": mem_min,
        "samples": samples,
    }
    final_sent = state.get("sent") if camera_ok else None
    event_activity = (triggers > 0 or
                      (initial_sent is not None and final_sent is not None and
                       final_sent > initial_sent) or
                      (initial_events is not None and
                       last_event_count is not None and
                       last_event_count > initial_events))
    ok = (camera_failures == 0 and lost_total == 0 and outbox_max == 0 and
          event_activity and
          (mem_start is None or mem_min is None or
           mem_start - mem_min < 24 * 1024))
    report["initial_events"] = initial_events
    report["final_events"] = last_event_count
    report["initial_sent"] = initial_sent
    report["final_sent"] = final_sent
    report["ok"] = ok
    path = OUT / ("soak-%s.json" % time.strftime("%Y%m%d-%H%M%S"))
    path.write_text(json.dumps(report, indent=1))
    if _ssh is not None:
        _ssh.close()
    print("soak %s: camera_failures=%d lost=%d outbox_max=%d dropped=%d "
          "mem %s→%s KiB, report %s" %
          ("ok" if ok else "FAIL", camera_failures, lost_total, outbox_max,
           dropped_max, mem_start, mem_min, path))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
