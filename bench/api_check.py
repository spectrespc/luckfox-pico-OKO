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
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

HOST = os.environ.get("OKO_DUT_HOST", "192.168.52.33")
USER = os.environ.get("OKO_RTSP_USER", "admin")
PASSWORD = os.environ.get("OKO_DUT_PASSWORD", "oko-change-me")
TIMEOUT = 20

GET_JSON = [
    "/api/status",
    "/api/time",
    "/api/telemetry",
    "/api/network",
    "/api/mdns",
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
    "/api/clip",
    "/api/models",
    "/api/focus",
    "/api/flash",
    "/api/firmware",
    # KI-064: GET /api/update kills divinus; do not poll it in the smoke test.
    "/api/streams",
    "/api/stream",
    "/api/http_post",
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
    "/api/mdns",
    "/api/storage",
    "/api/time",
    "/api/stream",
    "/api/http_post",
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

    # Micro-fine sweep around the current position (af=fine).  The lens may
    # only move inside +-af_coarse of the start position, and the job always
    # ends with "done" because it is a local correction, not a full search.
    checks += 1
    _status, before = get_focus()
    start_pos = (((before.get("motor") or {}).get("focus") or {})
                 .get("position"))
    status, dec = start_af("enable=1&motor=1&af=fine&fps=5")
    af = dec.get("af") or {}
    if af.get("state") != "running":
        failures.append("AF fine did not start: %r" % af)
    else:
        print("POST af=fine -> running scope %r" % af.get("scope"))
        checks += 1
        if af.get("scope") != "fine":
            failures.append("AF fine scope is %r" % af.get("scope"))
        curve = []
        dec = wait_af(180, curve)
        write_af_curve(curve, "fine")
        af = dec.get("af") or {}
        checks += 1
        if af.get("state") != "done":
            failures.append("AF fine: state %r error %r" %
                            (af.get("state"), af.get("error")))
        elif not isinstance(start_pos, int):
            failures.append("AF fine: no start position (%r)" % start_pos)
        else:
            delta = abs((af.get("best_position") or 0) - start_pos)
            print("AF fine -> state %s best %s start %s delta %d" %
                  (af.get("state"), af.get("best_position"), start_pos,
                   delta))
            if delta > 150 + 25:
                failures.append("AF fine moved %d steps (window is 175)" %
                                delta)

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

    # KI-043: the AF measurement does not need the STM8, so enabling it
    # without `motor` must work even when the driver is absent or busy.
    checks += 1
    status, dec = post_focus("enable=1")
    # The statistics thread needs a frame before it reports "running".
    deadline = time.time() + 20
    while status == 200 and dec.get("enable") and \
            dec.get("status") != "running" and time.time() < deadline:
        time.sleep(0.25)
        _status, dec = get_focus()
    if status != 200 or not dec.get("enable") or dec.get("status") != "running":
        failures.append("enable=1 without motor: expected enable=true "
                        "status=running, got %s %r" % (status, dec))
    print("POST %-28s %s enable=%s status=%s" %
          ("enable=1 (no motor)", status, dec.get("enable"),
           dec.get("status")))

    checks += 1
    status, dec = post_focus("enable=0")
    if status != 200 or dec.get("enable") or dec.get("status") != "idle":
        failures.append("enable=0: expected enable=false status=idle, got %s %r"
                        % (status, dec))
    print("POST %-28s %s enable=%s status=%s" %
          ("enable=0", status, dec.get("enable"), dec.get("status")))

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


def feature_checks(failures, checks):
    """Stream, IVS boxes, event upload and clip settings (non-destructive)."""
    # /api/stream: every destination is a separate scalar for applyValues.
    status, _headers, body = request("/api/stream")
    try:
        before = json.loads(body.decode())
    except Exception:                              # noqa: BLE001
        before = {}
    checks += 1
    if status != 200 or not all(k in before for k in
            ("enable", "udp_srcport", "dest0", "dest1", "dest2", "dest3",
             "running")):
        failures.append("GET /api/stream: HTTP %s %r" % (status, before))
    else:
        print("GET  %-18s %s enable=%s dest0=%r" %
              ("/api/stream", status, before.get("enable"),
               before.get("dest0")))

    # Bad schemes and the KI-052 ordering are rejected without a config change.
    for query, label in (
            ("dest0=ftp%3A%2F%2Fhost", "scheme"),
            ("dest0=udp%3A%2F%2F239.255.255.0%3A5600"
             "&dest1=udp%3A%2F%2F192.168.52.200%3A5600", "multicast first")):
        checks += 1
        status, _headers, body = request("/api/stream?" + query, "POST", b"")
        try:
            dec = json.loads(body.decode())
        except Exception:                          # noqa: BLE001
            dec = {}
        if status != 400 or dec.get("error") != "invalid":
            failures.append("POST /api/stream %s: expected 400, got %s %r" %
                            (label, status, dec))
        print("POST %-18s %s %s" % ("/api/stream " + label, status,
                                    dec.get("error", "?")))

    # A disabled stream must round-trip through POST and GET without losing
    # the configured destinations or the source port.
    checks += 1
    status, _headers, body = request("/api/stream", "POST", b"enable=false")
    try:
        dec = json.loads(body.decode())
    except Exception:                              # noqa: BLE001
        dec = {}
    if status != 200 or dec.get("enable") is not False:
        failures.append("POST /api/stream enable=false: HTTP %s %r" %
                        (status, dec))
    for key in ("dest0", "dest1", "dest2", "dest3", "udp_srcport"):
        if key in before and dec.get(key) != before.get(key):
            failures.append("POST /api/stream enable=false lost %s: %r -> %r"
                            % (key, before.get(key), dec.get(key)))
    print("POST %-18s %s enable=%s dest0=%r port=%s" % (
        "/api/stream off", status, dec.get("enable"), dec.get("dest0"),
        dec.get("udp_srcport")))

    # Restore the original destinations/port/state.
    if before:
        form = {"enable": "true" if before.get("enable") else "false",
                "udp_srcport": str(before.get("udp_srcport", 0))}
        for i in range(4):
            form["dest%d" % i] = before.get("dest%d" % i) or ""
        checks += 1
        status, _headers, body = request("/api/stream", "POST",
                                         urllib.parse.urlencode(form).encode())
        print("POST %-18s %s (restore)" % ("/api/stream", status))
        if status != 200:
            failures.append("POST /api/stream restore: HTTP %s" % status)

    # IVS boxes: 400 only when the RGN canvas is unavailable.  In the default
    # restart mode a real 0/1 switch saves the config and restarts the app, so
    # the smoke test re-applies the current value and only checks the shape.
    _status, _headers, body = request("/api/ivs")
    try:
        ivs_json = json.loads(body.decode())
    except Exception:                              # noqa: BLE001
        ivs_json = {}
    ivs_before = ivs_json.get("boxes")
    ivs_restart = ivs_json.get("boxes_mode") == "restart"
    checks += 1
    value = int(bool(ivs_before)) if ivs_restart else 1
    status, _headers, body = request("/api/ivs?boxes=%d" % value, "POST", b"")
    try:
        dec = json.loads(body.decode())
    except Exception:                              # noqa: BLE001
        dec = {}
    if status == 200 and dec.get("boxes") is not bool(value):
        failures.append("POST /api/ivs boxes=%d: %r" % (value, dec))
    elif status == 200 and ivs_restart and "applied" not in dec:
        failures.append("POST /api/ivs boxes: no applied field %r" % dec)
    elif status != 200 and dec.get("error") != "invalid":
        failures.append("POST /api/ivs boxes=%d: HTTP %s %r"
                        % (value, status, dec))
    print("POST %-18s %s boxes=%s applied=%s" % (
        "/api/ivs boxes", status, dec.get("boxes"), dec.get("applied")))
    if not ivs_restart and ivs_before is not None:
        request("/api/ivs?boxes=%d" % (1 if ivs_before else 0), "POST", b"")

    # IVS motion-area threshold: reported by GET and applied live (no IVS
    # restart, so the events ring must survive).
    checks += 1
    status, _headers, body = request("/api/ivs")
    try:
        dec = json.loads(body.decode())
    except Exception:                              # noqa: BLE001
        dec = {}
    ivs_area_before = dec.get("min_area")
    if status != 200 or not isinstance(ivs_area_before, int):
        failures.append("GET /api/ivs min_area: HTTP %s %r" % (status, dec))
    for event in dec.get("events") or []:
        if "area" not in event or "cause" not in event:
            failures.append("GET /api/ivs event fields: %r" % event)
            break
    print("GET  %-18s %s min_area=%r" % ("/api/ivs", status, ivs_area_before))

    checks += 1
    status, _headers, body = request("/api/ivs?min_area=3", "POST", b"")
    try:
        dec = json.loads(body.decode())
    except Exception:                              # noqa: BLE001
        dec = {}
    if status != 200 or dec.get("min_area") != 3:
        failures.append("POST /api/ivs min_area=3: HTTP %s %r" % (status, dec))
    print("POST %-18s %s min_area=%r" % ("/api/ivs min_area", status,
                                         dec.get("min_area")))
    if isinstance(ivs_area_before, int):
        request("/api/ivs?min_area=%d" % ivs_area_before, "POST", b"")

    # mDNS: the endpoint reports the MAC-derived name and the live state; the
    # toggle is reversible, a bad value is a 400.  The responder itself is
    # covered by mdns_check.py.
    checks += 1
    status, _headers, body = request("/api/mdns")
    try:
        mdns = json.loads(body.decode())
    except Exception:                              # noqa: BLE001
        mdns = {}
    if status != 200 or not all(k in mdns for k in
            ("enable", "running", "name", "addresses")):
        failures.append("GET /api/mdns: HTTP %s %r" % (status, mdns))
    elif mdns.get("name") and not re.match(r"^oko-[0-9a-f]{4}$",
                                           mdns["name"]):
        failures.append("GET /api/mdns name is not oko-xxxx: %r" %
                        mdns.get("name"))
    elif not isinstance(mdns.get("addresses"), list):
        failures.append("GET /api/mdns addresses: %r" % mdns.get("addresses"))
    print("GET  %-18s %s name=%r running=%s addresses=%s" %
          ("/api/mdns", status, mdns.get("name"), mdns.get("running"),
           mdns.get("addresses")))

    if mdns:
        enabled_before = mdns.get("enable")
        for value in ("0", "1"):
            checks += 1
            status, _headers, body = request("/api/mdns?enable=" + value,
                                             "POST", b"")
            try:
                dec = json.loads(body.decode())
            except Exception:                      # noqa: BLE001
                dec = {}
            if status != 200 or dec.get("enable") is not (value == "1"):
                failures.append("POST /api/mdns enable=%s: HTTP %s %r" %
                                (value, status, dec))
            print("POST %-18s %s enable=%s running=%s" %
                  ("/api/mdns " + value, status, dec.get("enable"),
                   dec.get("running")))
        checks += 1
        status, _headers, body = request("/api/mdns?enable=abc", "POST", b"")
        try:
            dec = json.loads(body.decode())
        except Exception:                          # noqa: BLE001
            dec = {}
        if status != 400 or dec.get("error") != "invalid":
            failures.append("POST /api/mdns enable=abc: expected 400, got "
                            "%s %r" % (status, dec))
        print("POST %-18s %s %s" % ("/api/mdns bad value", status,
                                    dec.get("error", "?")))
        if enabled_before is False:
            request("/api/mdns?enable=0", "POST", b"")

    # Event upload: read-only GET and URL validation; the sender is not
    # enabled here, so the camera keeps sending nothing.
    checks += 1
    status, _headers, body = request("/api/http_post")
    try:
        dec = json.loads(body.decode())
    except Exception:                              # noqa: BLE001
        dec = {}
    if status != 200 or not all(k in dec for k in
            ("enable", "host", "url", "login", "width", "height", "interval",
             "qfactor", "receiver_url", "password")):
        failures.append("GET /api/http_post: HTTP %s %r" % (status, dec))
    elif not isinstance(dec.get("password"), bool):
        failures.append("GET /api/http_post leaks the password: %r" %
                        dec.get("password"))
    print("GET  %-18s %s host=%r password=%r" %
          ("/api/http_post", status, dec.get("host"), dec.get("password")))

    checks += 1
    status, _headers, body = request("/api/http_post?url=ingest%2Fframe",
                                     "POST", b"")
    try:
        dec = json.loads(body.decode())
    except Exception:                              # noqa: BLE001
        dec = {}
    if status != 400 or dec.get("error") != "invalid":
        failures.append("POST /api/http_post relative url: expected 400, "
                        "got %s %r" % (status, dec))
    print("POST %-18s %s %s" % ("/api/http_post url", status,
                                dec.get("error", "?")))

    # Clip settings: change the window only while the collector is stopped.
    checks += 1
    status, _headers, body = request("/api/clip")
    try:
        clip = json.loads(body.decode())
    except Exception:                              # noqa: BLE001
        clip = {}
    if clip.get("enable") or clip.get("running"):
        print("clip settings: skipped (collector is enabled)")
    else:
        pre = min(int(clip.get("pre_seconds", 5)), 15)
        post = max(1, min(int(clip.get("post_seconds", 10)), 30))
        max_bytes = int(clip.get("max_bytes", 0))
        checks += 1
        query = "enable=0&pre_seconds=%d&post_seconds=%d&max_bytes=%d" % (
            pre, post, max_bytes)
        status, _headers, body = request("/api/clip?" + query, "POST", b"")
        try:
            dec = json.loads(body.decode())
        except Exception:                          # noqa: BLE001
            dec = {}
        if status != 200 or dec.get("pre_seconds") != pre or \
                dec.get("post_seconds") != post:
            failures.append("POST /api/clip settings: HTTP %s %r" %
                            (status, dec))
        print("POST %-18s %s pre=%s post=%s" % (
            "/api/clip settings", status, dec.get("pre_seconds"),
            dec.get("post_seconds")))
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

    # Detection history (KI-005): read-only, always JSON, and a bad `since`
    # must not turn into an error or mutate the configuration.
    for query, expected_records in (("detections=1", None),
                                    ("detections=1&since=1", None),
                                    ("detections=1&since=", 0)):
        checks += 1
        status, _headers, body = request("/api/yolo?" + query)
        try:
            dec = json.loads(body.decode())
        except Exception:                          # noqa: BLE001
            dec = {}
        if status != 200 or not isinstance(dec.get("detections"), list) or \
                not isinstance(dec.get("last"), int) or \
                "conf_threshold" not in dec or "aiisp" not in dec:
            failures.append("GET /api/yolo?%s: HTTP %s %r" %
                (query, status, dec))
        elif expected_records is not None and \
                len(dec["detections"]) != expected_records:
            failures.append("GET /api/yolo?%s: %d records, expected %d" %
                (query, len(dec["detections"]), expected_records))
        print("GET  %-18s %s %d records" %
              ("/api/yolo?detect", status, len(dec.get("detections") or [])))

    # Event clips: the read-only state must be JSON with the honest
    # availability fields; POST without an action is a 400, not a hidden
    # no-op, so the bench does not start clips by accident.
    checks += 1
    status, _headers, body = request("/api/clip")
    try:
        dec = json.loads(body.decode())
    except Exception:                              # noqa: BLE001
        dec = {}
    if status != 200 or not all(k in dec for k in
            ("enable", "running", "available", "ring_kib", "sent", "lost",
             "outbox_count")):
        failures.append("GET /api/clip: HTTP %s %r" % (status, dec))
    print("GET  %-18s %s ring %s KiB" % ("/api/clip", status,
        dec.get("ring_kib")))

    checks += 1
    status, _headers, body = request("/api/clip", "POST", b"")
    if not 400 <= status < 500:
        failures.append("POST /api/clip without action: expected 400, got %s"
            % status)
    print("POST %-18s %s" % ("/api/clip empty", status))

    # Architecture detection: /api/yolo reports the running model type and
    # /api/models records it per file.
    checks += 1
    status, _headers, body = request("/api/yolo")
    try:
        dec = json.loads(body.decode())
    except Exception:                              # noqa: BLE001
        dec = {}
    if status != 200 or dec.get("arch") not in ("none", "yolov5", "yolo11"):
        failures.append("GET /api/yolo arch: HTTP %s %r" %
            (status, dec.get("arch")))
    print("GET  %-18s %s arch=%s" % ("/api/yolo", status, dec.get("arch")))

    checks += 1
    status, _headers, body = request("/api/models")
    try:
        dec = json.loads(body.decode())
    except Exception:                              # noqa: BLE001
        dec = {}
    models = dec.get("models") or []
    if status != 200 or any("arch" not in m for m in models):
        failures.append("GET /api/models arch field: HTTP %s %r" %
            (status, models))
    elif any("yolo11" in m.get("name", "") and m.get("arch") not in (0, 2)
             for m in models):
        failures.append("yolo11 model recorded with another architecture: %r"
            % models)
    print("GET  %-18s %s %d models" % ("/api/models", status, len(models)))

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

    # Time zone: unsafe or unknown names are refused; a valid IANA name is
    # echoed back and persisted by the application itself.
    for name in ("../..", "Not/Zone", "/etc/passwd"):
        checks += 1
        status, _headers, body = request("/api/time", "POST",
            ("tz=" + urllib.parse.quote(name)).encode())
        dec = {}
        try:
            dec = json.loads(body.decode())
        except Exception:                          # noqa: BLE001
            pass
        if dec.get("error") != "blocked":
            failures.append("tz invalid %r: expected blocked, got %r" % (name, dec))
        print("POST %-18s %s %s" % ("/api/time tz bad", status,
            dec.get("error", "?")))

    checks += 1
    status, _headers, body = request("/api/time", "POST", b"tz=Europe%2FMoscow")
    dec = {}
    try:
        dec = json.loads(body.decode())
    except Exception:                              # noqa: BLE001
        pass
    if status != 200 or dec.get("tz") != "Europe/Moscow" or \
            dec.get("tz_current") != "Europe/Moscow":
        failures.append("tz valid zone: HTTP %s %r" % (status, dec))
    print("POST %-18s %s %s/%s" % ("/api/time tz", status,
        dec.get("tz", "?"), dec.get("tz_current", "?")))

    checks += 1
    status, _headers, body = request("/api/time")
    dec = {}
    try:
        dec = json.loads(body.decode())
    except Exception:                              # noqa: BLE001
        pass
    if not isinstance(dec.get("tz"), str) or not dec.get("tz") or \
            dec.get("tz_current") != dec.get("tz"):
        failures.append("tz GET field: HTTP %s %r" % (status, dec))
    print("GET  %-18s %s %s/%s" % ("/api/time tz", status,
        dec.get("tz", "?"), dec.get("tz_current", "?")))

    checks = feature_checks(failures, checks)
    checks = focus_checks(failures, checks)

    print("\n%d checks, %d failures" % (checks, len(failures)))
    for failure in failures:
        print("FAIL:", failure)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())