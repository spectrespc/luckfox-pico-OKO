#!/usr/bin/env python3
"""Power-cycle or reset the DUT through the ESP32 telnet-UART bridge.

The bridge (default http://192.168.52.17) switches the DUT 5 V rail and can
pulse the reset line.  A cold power cycle is the only way to clear a zombie
HPMCU left by a failed suspend, so bench runs use `cycle` instead of
`reboot` when the MCU state matters.

The bridge keeps at most three browser-style sessions alive for 24 h, so the
script always logs out; pass --cookie-file to reuse one saved session when
the pool is already full (a second login then answers 429) and log out with
--logout-only after a manual curl session.

Endpoints (bridge firmware):
    POST /auth/login   {"username": "...", "password": "..."} -> cookie
    POST /auth/logout
    POST /ext/power    {"on": true|false}
    POST /ext/reset

Examples:
    bench/run.sh power.py cycle --off-seconds 5
    bench/run.sh power.py off
    bench/run.sh power.py on --cookie-file /tmp/oko-bridge-cookie
    bench/run.sh power.py reset
"""
import argparse
import http.cookiejar
import json
import os
import sys
import time
import urllib.request

BRIDGE = os.environ.get("OKO_BRIDGE", "192.168.52.17")
USER = os.environ.get("OKO_BRIDGE_USER", "admin")
PASSWORD = os.environ.get("OKO_BRIDGE_PASSWORD", "admin")


def open_session(base, cookie_file):
    jar = http.cookiejar.MozillaCookieJar(cookie_file) if cookie_file else \
        http.cookiejar.CookieJar()
    if cookie_file and os.path.exists(cookie_file):
        jar.load(ignore_discard=True, ignore_expires=True)
    opener = urllib.request.build_opener(
        urllib.request.HTTPCookieProcessor(jar))
    if not len(jar):
        body = json.dumps({"username": USER, "password": PASSWORD}).encode()
        req = urllib.request.Request(base + "/auth/login", data=body,
                                     method="POST",
                                     headers={"Content-Type": "application/json"})
        with opener.open(req, timeout=10) as resp:
            answer = json.loads(resp.read().decode() or "{}")
        if not answer.get("authenticated"):
            raise RuntimeError(f"bridge login failed: {answer}")
        if cookie_file:
            jar.save(ignore_discard=True, ignore_expires=True)
    return opener, jar


def post(opener, base, path, payload=None):
    data = None if payload is None else json.dumps(payload).encode()
    req = urllib.request.Request(base + path, data=data, method="POST",
                                 headers={"Content-Type": "application/json"})
    with opener.open(req, timeout=10) as resp:
        text = resp.read().decode(errors="replace").strip()
    print(f"{path} -> {text}")
    return text


def run(action, bridge=BRIDGE, off_seconds=5.0, cookie_file=None,
        keep_session=False):
    """Execute one bridge action; importable for bench scripts."""
    base = "http://" + bridge
    opener, _jar = open_session(base, cookie_file)
    try:
        if action in ("off", "cycle"):
            post(opener, base, "/ext/power", {"on": False})
        if action == "cycle":
            time.sleep(off_seconds)
        if action in ("on", "cycle"):
            post(opener, base, "/ext/power", {"on": True})
        if action == "reset":
            post(opener, base, "/ext/reset")
    finally:
        if not keep_session:
            try:
                post(opener, base, "/auth/logout")
            except OSError as exc:
                print(f"logout failed: {exc}", file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("action",
                        choices=["off", "on", "cycle", "reset", "logout"])
    parser.add_argument("--bridge", default=BRIDGE, help="bridge host (env OKO_BRIDGE)")
    parser.add_argument("--off-seconds", type=float, default=5.0,
                        help="time to keep the rail off during a cycle")
    parser.add_argument("--cookie-file", default=None,
                        help="reuse/save the bridge session cookie here")
    parser.add_argument("--keep-session", action="store_true",
                        help="do not log out at the end (debugging only)")
    args = parser.parse_args()

    run(args.action, args.bridge, args.off_seconds, args.cookie_file,
        args.keep_session)
    return 0
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, OSError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        sys.exit(1)