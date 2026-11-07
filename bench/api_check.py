#!/usr/bin/env python3
"""Smoke-test the divinus HTTP API of the bench DUT.

Every GET endpoint must return HTTP 200 and parsable JSON; every POST endpoint
must accept an empty form body (a no-op that returns the current state) and
keep answering; the static handler must not serve files outside the microSD
card.  Run it through the wrapper so the venv is used:

    bench/run.sh api_check.py
"""
import base64
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


def focus_checks(failures, checks):
    """Home/zero and the manual focus/zoom matrix."""
    _status, dec = get_focus()
    motor = dec.get("motor") or {}

    checks += 1
    if "home" not in dec or "focus" not in motor or "zoom" not in motor:
        failures.append("/api/focus is missing home/focus/zoom: %r" %
            list(dec.keys()))
    print("focus motor:", "enabled" if motor.get("enabled") else "disabled",
          "present" if motor.get("present") else "absent")

    # Argument validation is safe without a motor.
    for query in ("home=bogus", "zero=bogus", "home=focus&zero=focus",
                  "move=1&motor=bogus"):
        checks += 1
        status, dec = post_focus(query)
        if status != 400 or dec.get("error") != "invalid":
            failures.append("%s: expected 400 invalid, got %s %r" %
                (query, status, dec))
        print("POST %-28s %s %s" % (query, status, dec.get("error", "?")))

    if not (motor.get("enabled") and motor.get("present")):
        checks += 1
        status, dec = post_focus("home=focus")
        if dec.get("error") != "blocked":
            failures.append("home with motor disabled: expected blocked, got %r"
                % dec)
        print("POST %-28s %s %s" % ("home=focus (disabled)", status,
            dec.get("error", "?")))
        print("focus motion: skipped (motor disabled/absent)")
        return checks

    if os.environ.get("OKO_FOCUS_MOTOR_TESTS") != "1":
        print("focus motion: skipped (set OKO_FOCUS_MOTOR_TESTS=1 to run)")
        return checks

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