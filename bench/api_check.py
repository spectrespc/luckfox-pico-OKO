#!/usr/bin/env python3
"""Smoke-test the divinus HTTP API of the bench DUT.

Every GET endpoint must return HTTP 200 and parsable JSON; every POST endpoint
must accept an empty form body (a no-op that returns the current state) and
keep answering; the static handler must not serve files outside the microSD
card.  Run it through the wrapper so the venv is used:

    bench/run.sh api_check.py
"""
import base64
import csv
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

HOST = os.environ.get("OKO_DUT_HOST", "192.168.52.97")
USER = os.environ.get("OKO_RTSP_USER", "admin")
PASSWORD = os.environ.get("OKO_DUT_PASSWORD", "oko-change-me")
TIMEOUT = 20

GET_JSON = [
    "/api/status",
    "/api/time",
    "/api/telemetry",
    "/api/network",
    "/api/isp",
    "/api/night",
    "/api/aiisp",
    "/api/hdr",
    "/api/iq",
    "/api/mp4",
    "/api/mjpeg",
    "/api/jpeg",
    "/api/audio",
    "/api/rtsp",
    "/api/onvif",
    "/api/substream",
    "/api/venc",
    "/api/eptz",
    "/api/rotate",
    "/api/record",
    "/api/recordings",
    "/api/storage",
    "/api/ivs",
    "/api/yolo",
    "/api/models",
    "/api/focus",
    "/api/flash",
    "/api/firmware",
    "/api/streams",
    "/api/vpn",
] + ["/api/osd/%d" % i for i in range(10)]

POST_NOOP = [
    "/api/isp",
    "/api/night",
    "/api/aiisp",
    "/api/hdr",
    "/api/iq",
    "/api/mp4",
    "/api/mjpeg",
    "/api/jpeg",
    "/api/audio",
    "/api/rtsp",
    "/api/onvif",
    "/api/substream",
    "/api/venc",
    "/api/eptz",
    "/api/rotate",
    "/api/record",
    "/api/ivs",
    "/api/yolo",
    "/api/models",
    "/api/focus",
    "/api/network",
    "/api/storage",
    "/api/time",
] + ["/api/osd/%d" % i for i in range(10)]


def request(path, method="GET", body=None, content_type=None):
    req = urllib.request.Request("http://%s%s" % (HOST, path), method=method)
    token = base64.b64encode(("%s:%s" % (USER, PASSWORD)).encode()).decode()
    req.add_header("Authorization", "Basic " + token)
    if body is not None:
        req.data = body
        req.add_header("Content-Type",
            content_type or "application/x-www-form-urlencoded")
    try:
        with urllib.request.urlopen(req, timeout=TIMEOUT) as resp:
            return resp.status, resp.headers, resp.read()
    except urllib.error.HTTPError as err:
        return err.code, err.headers, err.read()
    except Exception as err:                       # noqa: BLE001 - report it
        return 0, {}, str(err).encode()


def post_focus(query):
    status, _headers, body = request("/api/focus?" + query, "POST", b"")
    try:
        return status, json.loads(body.decode())
    except Exception:                              # noqa: BLE001
        return status, {}


def get_focus():
    status, _headers, body = request("/api/focus")
    try:
        return status, json.loads(body.decode())
    except Exception:                              # noqa: BLE001
        return status, {}


def wait_home(timeout=180):
    deadline = time.time() + timeout
    dec = {}
    while time.time() < deadline:
        _status, dec = get_focus()
        state = (dec.get("home") or {}).get("state")
        if state in ("done", "error", "idle"):
            return dec
        time.sleep(0.5)
    return dec


def wait_motor(motor, timeout=60):
    deadline = time.time() + timeout
    dec = {}
    while time.time() < deadline:
        _status, dec = get_focus()
        status = ((dec.get("motor") or {}).get(motor) or {}).get("status")
        if status in ("idle", "error"):
            return dec
        time.sleep(0.3)
    return dec


def wait_af(timeout=180, curve=None):
    """Polls the AF job and records a (position, metric) curve while it runs."""
    deadline = time.time() + timeout
    dec = {}
    seen = set()
    while time.time() < deadline:
        _status, dec = get_focus()
        af = dec.get("af") or {}
        pos = af.get("position")
        metric = af.get("metric")
        if curve is not None and pos is not None:
            key = (pos, metric)
            if key not in seen:
                seen.add(key)
                curve.append((pos, metric, af.get("best_position"),
                              af.get("best_metric"), af.get("state")))
        if af.get("state") in ("done", "error", "idle"):
            return dec
        time.sleep(0.5)
    return dec


def start_af(query="af=1&motor=1", tries=15):
    """Starts a search, retrying while the previous worker is still unwinding
    (its terminal state is published just before the busy flag clears)."""
    status, dec = post_focus(query)
    for _ in range(tries):
        if (dec.get("af") or {}).get("state") == "running":
            return status, dec
        if dec.get("error") != "busy":
            return status, dec
        time.sleep(0.2)
        status, dec = post_focus(query)
    return status, dec


def write_af_curve(curve, name):
    os.makedirs(os.path.join("output", "bench"), exist_ok=True)
    path = os.path.join("output", "bench", "af_curve_%s_%s.csv" %
                        (name, time.strftime("%Y%m%d-%H%M%S")))
    with open(path, "w", newline="") as fh:
        writer = csv.writer(fh)
        writer.writerow(["position", "metric", "best_position",
                         "best_metric", "state"])
        writer.writerows(curve)
    print("AF curve %s: %d points -> %s" % (name, len(curve), path))
    return path


def focus_af_checks(failures, checks):
    """Opt-in real-scene auto-focus run (OKO_FOCUS_AF_TESTS=1)."""
    checks += 1
    status, dec = post_focus("enable=1&motor=1&mode=full&fps=5")
    if dec.get("enable") is not True:
        failures.append("cannot start the AF statistics: %s %r" % (status, dec))
    print("AF statistics:", dec.get("status"), "fps", dec.get("fps"))

    # A trusted scale: AF starts from a homed position.
    post_focus("home=focus&motor=1")
    dec = wait_home(180)
    print("AF home ->", (dec.get("home") or {}).get("state"),
          ((dec.get("motor") or {}).get("focus") or {}).get("position"))

    for mode in ("full", "roi"):
        status, dec = start_af(
            "mode=%s&rx=2500&ry=2500&rw=5000&rh=5000&fps=5&motor=1&af=1" %
            mode)
        checks += 1
        af = dec.get("af") or {}
        if af.get("state") != "running":
            failures.append("AF %s did not start: %r" % (mode, af))
            continue
        print("POST af=1 mode=%s -> running" % mode)

        checks += 1
        busy_status, busy = post_focus("af=1&motor=1")
        if busy_status != 409:
            failures.append("second AF: expected 409, got %s %r" %
                            (busy_status, busy))
        checks += 1
        home_status, home = post_focus("home=focus&motor=1")
        if home_status != 409:
            failures.append("home during AF: expected 409, got %s %r" %
                            (home_status, home))

        curve = []
        dec = wait_af(180, curve)
        write_af_curve(curve, mode)
        af = dec.get("af") or {}
        checks += 1
        if af.get("state") != "done":
            failures.append("AF %s: state %r error %r" %
                            (mode, af.get("state"), af.get("error")))
        print("AF %s -> state %s best %s metric %s" %
              (mode, af.get("state"), af.get("best_position"),
               af.get("best_metric")))

    # Cancel path: start and cancel immediately.
    checks += 1
    status, dec = start_af()
    af = dec.get("af") or {}
    if af.get("state") == "running":
        post_focus("af=cancel")
        dec = wait_af(30)
        af = dec.get("af") or {}
        if af.get("error") != "cancelled" or af.get("state") != "error":
            failures.append("AF cancel: state %r error %r" %
                            (af.get("state"), af.get("error")))
        print("AF cancel ->", af.get("state"), af.get("error"))
    else:
        failures.append("AF did not start for the cancel test: %r" % af)

    # A plain motor stop also cancels a running search.
    checks += 1
    status, dec = start_af()
    af = dec.get("af") or {}
    if af.get("state") == "running":
        post_focus("stop=1&motor=0")
        dec = wait_af(30)
        af = dec.get("af") or {}
        if af.get("error") != "cancelled" or af.get("state") != "error":
            failures.append("AF stop=1: state %r error %r" %
                            (af.get("state"), af.get("error")))
        print("AF stop=1 ->", af.get("state"), af.get("error"))
    else:
        failures.append("AF did not start for the stop test: %r" % af)

    post_focus("enable=0&af=cancel&motor=0")
    return checks


def focus_checks(failures, checks):
    """Home/zero and the manual focus/zoom matrix."""
    _status, dec = get_focus()
    motor = dec.get("motor") or {}

    checks += 1
    if "home" not in dec or "af" not in dec or \
       "focus" not in motor or "zoom" not in motor:
        failures.append("/api/focus is missing home/af/focus/zoom: %r" %
            list(dec.keys()))
    print("focus motor:", "enabled" if motor.get("enabled") else "disabled",
          "present" if motor.get("present") else "absent")

    # Argument validation is safe without a motor.
    for query in ("home=bogus", "zero=bogus", "home=focus&zero=focus",
                  "move=1&motor=bogus", "af=bogus", "scale=bogus"):
        checks += 1
        status, dec = post_focus(query)
        if status != 400 or dec.get("error") != "invalid":
            failures.append("%s: expected 400 invalid, got %s %r" %
                (query, status, dec))
        print("POST %-28s %s %s" % (query, status, dec.get("error", "?")))

    if not motor.get("present"):
        checks += 1
        status, dec = post_focus("home=focus")
        if dec.get("error") != "blocked":
            failures.append("home with motor absent: expected blocked, got %r"
                % dec)
        print("POST %-28s %s %s" % ("home=focus (absent)", status,
            dec.get("error", "?")))
        print("focus motion: skipped (motor absent)")
        return checks

    if not motor.get("enabled"):
        checks += 1
        status, dec = post_focus("home=focus")
        if dec.get("error") != "blocked":
            failures.append("home with motor disabled: expected blocked, got %r"
                % dec)
        print("POST %-28s %s %s" % ("home=focus (disabled)", status,
            dec.get("error", "?")))

    if os.environ.get("OKO_FOCUS_AF_TESTS") == "1":
        checks = focus_af_checks(failures, checks)
    else:
        print("focus auto-focus: skipped (set OKO_FOCUS_AF_TESTS=1 to run)")

    if os.environ.get("OKO_FOCUS_MOTOR_TESTS") != "1":
        print("focus motion: skipped (set OKO_FOCUS_MOTOR_TESTS=1 to run)")
        return checks

    # The WebUI arms the driver on every motion command; scripts may still
    # arm once and use the legacy scale spelling.
    post_focus("motor=1")

    # Zero focus and verify the readback through SET_POS.
    checks += 1
    post_focus("zero=focus&value=0")
    dec = wait_home(15)
    pos = ((dec.get("motor") or {}).get("focus") or {}).get("position")
    if (dec.get("home") or {}).get("state") != "done" or pos != 0:
        failures.append("zero focus: state %r position %r" %
            ((dec.get("home") or {}).get("state"), pos))
    print("zero focus ->", pos)

    # Relative move on both scales and the position clamp.
    for query, expect in (("move=10&motor=focus", 10),
                          ("move=-10&motor=focus", 0),
                          ("move=10&motor=zoom", 10),
                          ("move=-10&motor=zoom", 0),
                          ("position=-1&motor=focus", 0)):
        checks += 1
        _status, dec = post_focus(query)
        motor_name = "zoom" if "zoom" in query else "focus"
        dec = wait_motor(motor_name)
        pos = ((dec.get("motor") or {}).get(motor_name) or {}).get("position")
        if pos != expect:
            failures.append("%s: expected %r, got %r" % (query, expect, pos))
        print("POST %-28s -> %s" % (query, pos))

    # Stay away from the mechanical end: a long stall against the stop can
    # corrupt the top byte of the RAM position (BOR is off).  The max clamp
    # itself is covered by the host codec test (focus_proto_clamp).
    checks += 1
    _status, dec = post_focus("move=200&motor=focus")
    dec = wait_motor("focus", 30)
    pos = ((dec.get("motor") or {}).get("focus") or {}).get("position")
    if pos != 200:
        failures.append("move +200: expected 200, got %r" % pos)
    print("move +200 ->", pos)

    # Home from the far end gives a long enough job to check the busy reply.
    checks += 1
    _status, dec = post_focus("home=focus")
    if (dec.get("home") or {}).get("state") != "running":
        failures.append("home focus did not start: %r" % dec.get("home"))
    status, busy = post_focus("home=focus")
    if status != 409:
        failures.append("second home: expected 409, got %s %r" % (status, busy))
    print("POST home focus -> running, second -> %s" % status)
    dec = wait_home(180)
    pos = ((dec.get("motor") or {}).get("focus") or {}).get("position")
    if (dec.get("home") or {}).get("state") != "done" or pos != 0:
        failures.append("home focus: state %r position %r" %
            ((dec.get("home") or {}).get("state"), pos))
    print("home focus ->", pos,
          "travel", (dec.get("home") or {}).get("travel"))

    # Both scales in one job and a zero of both.
    checks += 1
    _status, dec = post_focus("home=both")
    dec = wait_home(60)
    posf = ((dec.get("motor") or {}).get("focus") or {}).get("position")
    posz = ((dec.get("motor") or {}).get("zoom") or {}).get("position")
    if (dec.get("home") or {}).get("state") != "done" or posf != 0 or posz != 0:
        failures.append("home both: state %r focus %r zoom %r" %
            ((dec.get("home") or {}).get("state"), posf, posz))
    print("home both ->", posf, posz)

    checks += 1
    _status, dec = post_focus("zero=both&value=0")
    dec = wait_home(15)
    if (dec.get("home") or {}).get("state") != "done":
        failures.append("zero both: state %r" % (dec.get("home"),))
    print("zero both ->", (dec.get("home") or {}).get("state"))
    return checks


def main():
    failures = []
    checks = 0

    for path in GET_JSON:
        checks += 1
        status, _headers, body = request(path)
        try:
            json.loads(body.decode())
        except Exception:                          # noqa: BLE001
            failures.append("GET %s: HTTP %s, not JSON: %r" %
                (path, status, body[:120]))
            continue
        if status != 200:
            failures.append("GET %s: HTTP %s" % (path, status))
        print("GET  %-18s %s" % (path, status))

    for path in POST_NOOP:
        checks += 1
        status, _headers, body = request(path, "POST", b"")
        try:
            json.loads(body.decode())
        except Exception:                          # noqa: BLE001
            failures.append("POST %s: HTTP %s, not JSON: %r" %
                (path, status, body[:120]))
            continue
        if status != 200:
            failures.append("POST %s: HTTP %s" % (path, status))
        print("POST %-18s %s" % (path, status))

    # Attachments: the export must look like YAML, the log like text.
    checks += 1
    status, headers, body = request("/api/cmd?export")
    if status != 200 or b"system:" not in body:
        failures.append("export: HTTP %s, %d bytes" % (status, len(body)))
    print("GET  %-18s %s (%d bytes)" % ("/api/cmd?export", status, len(body)))

    checks += 1
    status, headers, body = request("/api/cmd?log")
    if status != 200 or not body:
        failures.append("log: HTTP %s, %d bytes" % (status, len(body)))
    print("GET  %-18s %s (%d bytes)" % ("/api/cmd?log", status, len(body)))

    # Negative cases: static files outside the card and invalid names.
    negative = [
        ("//etc/passwd", 404),
        ("/tmp/divinus.yaml", 404),
        ("/mnt/sdcard/recordings/nope.mp4", 404),
    ]
    for path, expected in negative:
        checks += 1
        status, _headers, _body = request(path)
        if status != expected:
            failures.append("GET %s: expected %d, got %s" % (path, expected, status))
        print("GET  %-18s %s (expected %d)" % (path, status, expected))

    # yolo.source=vi1 hangs the RV1106 board and unknown values must not be
    # silently mapped to it; both are refused with HTTP 400.
    for value in ("vi1", "bogus"):
        checks += 1
        path = "/api/yolo?source=" + value
        status, _headers, body = request(path)
        try:
            dec = json.loads(body.decode())
        except Exception:                          # noqa: BLE001
            dec = {}
        if not 400 <= status < 500 or dec.get("error") != "invalid":
            failures.append("GET %s: expected 400 invalid, got HTTP %s %r" %
                (path, status, dec))
        print("GET  %-18s %s %s" % (path, status, dec.get("error", "?")))

    # VPN: a bad profile name, a missing action and a path traversal in the
    # export must be refused without touching the profiles.
    checks += 1
    status, _headers, body = request("/api/vpn", "POST",
        b"action=up&profile=..%2Fetc")
    dec = json.loads(body.decode())
    if dec.get("error") != "blocked":
        failures.append("VPN bad profile: expected blocked, got %r" % dec)
    print("POST %-18s %s %s" % ("/api/vpn profile", status,
        dec.get("error", "?")))

    checks += 1
    status, _headers, body = request("/api/vpn", "POST", b"")
    if not 400 <= status < 500:
        failures.append("VPN empty action: expected 400, got %s" % status)
    print("POST %-18s %s" % ("/api/vpn empty", status))

    checks += 1
    status, _headers, body = request(
        "/api/vpn/export?profile=..%2Fetc%2Fpasswd")
    dec = json.loads(body.decode())
    if dec.get("error") != "blocked":
        failures.append("VPN export traversal: expected blocked, got %r" % dec)
    print("GET  %-18s %s %s" % ("/api/vpn/export", status,
        dec.get("error", "?")))

    for name in ("../../etc/passwd", "x.txt", "..", ""):
        checks += 1
        status, _headers, body = request("/api/recordings/delete", "POST",
            ("name=" + urllib.parse.quote(name)).encode())
        try:
            dec = json.loads(body.decode())
        except Exception:                          # noqa: BLE001
            dec = {}
        if dec.get("error") != "blocked":
            failures.append("delete %r: expected blocked, got %r" % (name, dec))
        print("POST %-18s %s %s" % ("/api/recordings/delete", status,
            dec.get("error", "?")))

    checks += 1
    status, _headers, body = request("/api/time", "POST", b"ntp_server=bad..host")
    dec = {}
    try:
        dec = json.loads(body.decode())
    except Exception:                              # noqa: BLE001
        pass
    if dec.get("error") != "blocked":
        failures.append("ntp invalid server: %r" % (dec,))
    print("POST %-18s %s %s" % ("/api/time ntp", status, dec.get("error", "?")))

    checks = focus_checks(failures, checks)

    print("\n%d checks, %d failures" % (checks, len(failures)))
    for failure in failures:
        print("FAIL:", failure)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())