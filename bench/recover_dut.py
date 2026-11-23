#!/usr/bin/env python3
"""Recover a DUT left in a failed `[sleep]` run, using one bridge session.

A crashed `sleep_power.py` run can leave `/userdata/divinus.yaml` with
`sleep.enable: true`: after the next boot the camera suspends once more and
stops answering.  This helper performs exactly one bridge power cycle, then
as soon as the API answers it disables sleep with a long idle timeout, so the
next window is safe.  With `--ssh` it also normalises the YAML file; with
`--restart` it restarts divinus so the runtime state matches the file.

The bridge allows only three live sessions and has no API to evict them; pass
one `--cookie-file` and do not run other bridge clients in parallel.  On
`429 Too many active sessions` the script stops and asks for a manual bridge
reboot — it never retries the login.

Examples:
    bench/run.sh recover_dut.py
    bench/run.sh recover_dut.py --ssh
    bench/run.sh recover_dut.py --ssh --restart
"""
import argparse
import base64
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import power                                                  # noqa: E402
import sleep_power                                            # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
COOKIE = ROOT / "output" / "bench" / "bridge.cookie"
YAML = "/userdata/divinus.yaml"
AWK = (
    "awk 'BEGIN{s=0} /^sleep:/{s=1} s==1 && /^  enable:/{sub(/true/,"
    "\"false\")} s==1 && /^  idle_timeout_s:/{sub(/[0-9]+/,\"15\")} "
    "s==1 && /^[a-z]/ && !/^sleep:/{s=0} {print}' "
    + YAML + " > /tmp/divinus.new && cp /tmp/divinus.new " + YAML
    + " && echo YAML_OK"
)


def log(message):
    print(message, flush=True)


def dut(host, path, method="GET", timeout=2):
    token = base64.b64encode(("%s:%s" % (sleep_power.USER,
                                         sleep_power.PASSWORD)).encode()).decode()
    req = urllib.request.Request(
        "http://%s%s" % (host, path), method=method,
        data=b"" if method == "POST" else None,
        headers={"Authorization": "Basic " + token})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.status, resp.read().decode(errors="replace")
    except urllib.error.HTTPError as err:
        return err.code, err.read().decode(errors="replace")
    except Exception:                                       # noqa: BLE001
        return 0, ""


def wait_api(host, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        status, body = dut(host, "/api/status", timeout=1.5)
        if status == 200:
            return True, body
        time.sleep(0.3)
    return False, ""


def disable_sleep(host, idle_timeout):
    query = urllib.parse.urlencode({
        "enable": "false", "idle_timeout_s": str(idle_timeout),
        "pmu_timer_wake": "true", "wake_interval_s": "60",
        "wake_check_s": "3", "heartbeat": "false"})
    for _attempt in range(10):
        status, body = dut(host, "/api/sleep?" + query, "POST", timeout=3)
        if status == 200 and "applied" in body:
            return True, body
        time.sleep(0.5)
    return False, body


def ssh_normalise(host, restart):
    client = sleep_power.ssh_client(timeout=20)
    try:
        code, out, err = sleep_power.ssh_run(
            client, "echo test > /userdata/.rwtest && echo RW_OK || echo RO; "
                    "rm -f /userdata/.rwtest", timeout=20)
        log("userdata: %s" % (out.strip() or err.strip()))
        if "RO" in out:
            return False, "userdata is read-only; power cycle again"
        code, out, err = sleep_power.ssh_run(client, AWK, timeout=30)
        if "YAML_OK" not in out:
            return False, (out + err).strip()
        if restart:
            sleep_power.ssh_run(
                client, "/etc/init.d/S21appinit stop >/tmp/stop.log 2>&1; "
                        "sleep 1; /etc/init.d/S21appinit start "
                        ">/tmp/start.log 2>&1; echo RESTARTED", timeout=90)
            wait_api(host, 90)
        return True, "yaml ok"
    finally:
        client.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--host", default=sleep_power.HOST)
    parser.add_argument("--bridge", default=power.BRIDGE)
    parser.add_argument("--cookie-file", default=str(COOKIE))
    parser.add_argument("--off-seconds", type=float, default=8)
    parser.add_argument("--wait", type=float, default=120)
    parser.add_argument("--idle-timeout", type=int, default=3600,
                        help="safe idle timeout written immediately (3600 s)")
    parser.add_argument("--ssh", action="store_true",
                        help="also normalise /userdata/divinus.yaml")
    parser.add_argument("--restart", action="store_true",
                        help="restart divinus after the YAML fix")
    args = parser.parse_args()

    if args.restart and not args.ssh:
        parser.error("--restart requires --ssh")
    sleep_power.HOST = args.host
    Path(args.cookie_file).parent.mkdir(parents=True, exist_ok=True)
    try:
        power.run("cycle", args.bridge, args.off_seconds, args.cookie_file)
    except urllib.error.HTTPError as exc:
        if exc.code == 429:
            log("bridge pool is full (429). Reboot the ESP32 bridge manually "
                "or wait for the 24 h session TTL; no retry was attempted.")
            return 2
        raise
    log("power cycle done; waiting for the API")
    ok, _body = wait_api(args.host, args.wait)
    if not ok:
        log("DUT API did not return within %.0f s; check power/bridge" %
            args.wait)
        return 3
    ok, body = disable_sleep(args.host, args.idle_timeout)
    log("sleep disable: %s %s" % ("ok" if ok else "FAILED", body.strip()[:200]))
    if args.ssh:
        try:
            ok, detail = ssh_normalise(args.host, args.restart)
            log("ssh: %s (%s)" % ("ok" if ok else "FAILED", detail))
        except Exception as exc:                            # noqa: BLE001
            log("ssh failed: %s" % exc)
            ok = False
    time.sleep(20)
    status, body = dut(args.host, "/api/status", timeout=3)
    log("after 20 s: /api/status=%s" % status)
    if status != 200:
        log("DUT is not stable; a manual power cycle is still required")
        return 4
    try:
        state = json.loads(dut(args.host, "/api/sleep", timeout=3)[1])
        log("sleep: %s" % {key: state.get(key) for key in
                           ("enable", "enabled", "state", "idle_timeout_s")})
        if state.get("enabled") and not args.restart:
            log("runtime sleep is still enabled: rerun with --ssh --restart")
    except ValueError:
        pass
    return 0 if ok else 5


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, OSError, KeyboardInterrupt) as exc:
        print("error: %s" % exc, file=sys.stderr)
        sys.exit(1)
