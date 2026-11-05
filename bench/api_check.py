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
    "/api/streams",
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

    print("\n%d checks, %d failures" % (checks, len(failures)))
    for failure in failures:
        print("FAIL:", failure)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())