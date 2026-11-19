#!/usr/bin/env python3
"""End-to-end check of the MQTT chain (camera -> mosquitto -> recorder).

Starts from the `services/mqtt` compose profile, verifies broker
authentication, configures `[mqtt]` on the DUT through `POST /api/mqtt`,
waits for the retained status, one event and one telemetry sample in the
recorder, then kills divinus with SIGKILL to observe the broker LWT
(`online:false`) and restarts it.

    bench/run.sh mqtt_check.py
    bench/run.sh mqtt_check.py --deploy
    bench/run.sh mqtt_check.py --neg        # profile is down
    bench/run.sh mqtt_check.py --keep-config

Environment: OKO_DUT_HOST (default 192.168.52.33), OKO_DUT_PASSWORD
(default `luckfox`), OKO_MQTT_USER/PASS (default `oko`/`oko`),
OKO_MQTT_REC_USER/PASS (`oko-rec`), OKO_MQTT_ADDR (the address the camera
uses, default `192.168.52.200:1883`), OKO_MQTT_URL (recorder,
default `http://127.0.0.1:8092`).  Artifacts: output/bench/mqtt/report.json.
"""
import argparse
import base64
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

import paramiko

ROOT = Path(__file__).resolve().parent.parent
HOST = os.environ.get("OKO_DUT_HOST", "192.168.52.33")
USER = os.environ.get("OKO_RTSP_USER", "admin")
PASSWORD = os.environ.get("OKO_DUT_PASSWORD", "oko-change-me")
ROOT_PASSWORD = os.environ.get("OKO_ROOT_PASSWORD", "luckfox")
MQTT_USER = os.environ.get("OKO_MQTT_USER", "oko")
MQTT_PASS = os.environ.get("OKO_MQTT_PASS", "oko")
REC_USER = os.environ.get("OKO_MQTT_REC_USER", "oko-rec")
REC_PASS = os.environ.get("OKO_MQTT_REC_PASS", "oko-rec")
MQTT_ADDR = os.environ.get("OKO_MQTT_ADDR", "192.168.52.200:1883")
RECORDER = os.environ.get("OKO_MQTT_URL", "http://127.0.0.1:8092")
COMPOSE = ROOT / "services" / "mqtt" / "docker-compose.yml"
BROKER = "oko-mqtt-broker"
OUT = ROOT / "output" / "bench" / "mqtt"

CONFIG_KEYS = ["enable", "host", "login", "topic_prefix", "client_id",
               "keepalive_s", "telemetry_interval_s", "publish_events",
               "publish_telemetry"]

failures = []
checks = []


def record(ok, name, detail=""):
    checks.append({"ok": ok, "check": name, "detail": detail})
    if not ok:
        failures.append(name)
    print("%s %-38s %s" % ("ok  " if ok else "FAIL", name, detail))


def request(url, method="GET", body=None, timeout=10):
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


def dut(path, method="GET", body=None, timeout=10):
    status, payload = request("http://%s%s" % (HOST, path), method, body,
                              timeout)
    try:
        return status, json.loads(payload.decode())
    except Exception:                              # noqa: BLE001
        return status, {}


def recorder(path, timeout=10):
    status, payload = request(RECORDER + path, timeout=timeout)
    try:
        return status, json.loads(payload.decode())
    except Exception:                              # noqa: BLE001
        return status, {}


def ssh_run(cmd):
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(HOST, username="root", password=ROOT_PASSWORD, timeout=10)
    _, out, err = client.exec_command(cmd, timeout=30)
    text = (out.read() + err.read()).decode(errors="replace").strip()
    code = out.channel.recv_exit_status()
    client.close()
    return code, text


def compose_up():
    return subprocess.run(
        ["docker", "compose", "-f", str(COMPOSE), "up", "-d", "--build"],
        cwd=str(ROOT), capture_output=True, text=True)


def broker_pub(password, topic="oko/bench/probe", payload='{"type":"probe"}'):
    return subprocess.run(
        ["docker", "exec", BROKER, "mosquitto_pub", "-h", "127.0.0.1",
         "-p", "1883", "-u", MQTT_USER, "-P", password, "-t", topic,
         "-m", payload], capture_output=True, text=True)


def wait_for(predicate, timeout, interval=1.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(interval)
    return None


def start_divinus(bench_api=False):
    """Restarts divinus, optionally with OKO_BENCH_API=1 (the injector).

    A graceful SIGHUP keeps the old environment, so the process is started
    from scratch; SIGKILL in the LWT phase left /userdata/divinus.yaml intact.
    The final call in a run is without the bench variable so the injector is
    not left enabled.
    """
    env = "OKO_BENCH_API=1 " if bench_api else ""
    return ssh_run(
        "kill -9 $(pidof divinus) 2>/dev/null; sleep 1; "
        "cd /userdata && LD_LIBRARY_PATH=/oem/usr/lib " + env +
        "setsid /oem/usr/bin/divinus >/tmp/divinus-bench.log 2>&1 "
        "< /dev/null & sleep 2; pidof divinus")


def mqtt_status():
    status, dec = dut("/api/mqtt")
    return dec if status == 200 else {}


def device_status(client_id):
    _status, dec = recorder("/api/status")
    entry = (dec.get("devices") or {}).get(client_id) or {}
    return entry.get("payload") or {}


def telemetry(client_id):
    _status, dec = recorder("/api/telemetry")
    entry = (dec.get("devices") or {}).get(client_id) or {}
    return entry.get("payload") or {}


def events(since_ms):
    _status, dec = recorder("/api/events?limit=100")
    # The device stamps ts_ms with one-second granularity, so a window that
    # starts "now" would drop an event published in the same second.
    floor = since_ms - 2000
    return [e for e in dec.get("events", [])
            if (e.get("payload") or {}).get("ts_ms", 0) >= floor]


def apply_config(values):
    query = urllib.parse.urlencode(values)
    status, dec = dut("/api/mqtt?" + query, "POST", b"")
    want = str(values.get("enable", "true")).lower() == "true"
    record(status == 200 and dec.get("enable") is want,
           "POST /api/mqtt applied", "HTTP %s" % status)
    return dec if status == 200 else {}


def restore_config(before):
    values = {}
    for key in CONFIG_KEYS:
        value = before.get(key)
        if value is None:
            continue
        if isinstance(value, bool):
            value = "true" if value else "false"
        values[key] = value
    query = urllib.parse.urlencode(values)
    status, _dec = dut("/api/mqtt?" + query, "POST", b"")
    return status == 200


def negative_case():
    status, dec = dut("/api/mqtt")
    if status != 200:
        record(False, "GET /api/mqtt", "HTTP %s" % status)
        return
    before = dec
    temp = {"enable": "true", "host": MQTT_ADDR, "login": MQTT_USER,
            "password": MQTT_PASS, "publish_events": "true",
            "publish_telemetry": "true", "keepalive_s": "10"}
    apply_config(temp)
    time.sleep(8)
    dec = mqtt_status()
    record(dec.get("connected") is False, "publisher must stay disconnected",
           "connected=%r" % dec.get("connected"))
    record(bool(dec.get("last_error")), "connection error is reported",
           dec.get("last_error", ""))
    if before.get("enable"):
        restore_config(before)
    else:
        restore_config({"enable": "false", "host": "none"})


def positive_case(args):
    # 1. Broker auth: right password publishes, wrong one is refused.
    ok = broker_pub(MQTT_PASS)
    record(ok.returncode == 0, "broker accepts the device password",
           (ok.stderr or ok.stdout).strip()[:120])
    bad = broker_pub("definitely-wrong")
    record(bad.returncode != 0, "broker refuses a bad password",
           (bad.stderr or bad.stdout).strip()[:120])

    # 2. The recorder is alive.  `up --build` recreates the container on every
    # positive run, so wait for uvicorn instead of a single probe.
    def healthy():
        status, dec = recorder("/health")
        return dec if status == 200 and dec.get("ok") is True else None

    health = wait_for(healthy, 60)
    record(health is not None, "recorder /health", str(health or {})[:120])

    # 3. Configure the camera live (no restart).
    before = mqtt_status()
    if not before:
        record(False, "GET /api/mqtt", "the DUT firmware has no MQTT support")
        return
    interval = 10
    apply_config({
        "enable": "true",
        "host": MQTT_ADDR,
        "login": MQTT_USER,
        "password": MQTT_PASS,
        "topic_prefix": "oko",
        "keepalive_s": "15",
        "telemetry_interval_s": str(interval),
        "publish_events": "true",
        "publish_telemetry": "true",
    })

    connected = wait_for(lambda: mqtt_status().get("connected") is True, 30)
    status_now = mqtt_status()
    client_id = status_now.get("resolved_client_id", "")
    record(bool(connected), "publisher connected",
           "id=%s last_error=%s" % (client_id, status_now.get("last_error", "")))
    if not connected:
        return

    # 4. Retained status (online:true) in the recorder.
    online = wait_for(lambda: device_status(client_id).get("online") is True, 20)
    record(online is not None, "retained status online:true", client_id)

    # 5. Telemetry from /tmp/oko-ec.json.
    tele = wait_for(
        lambda: telemetry(client_id)
        if telemetry(client_id).get("available") is True else None,
        interval + 20)
    record(tele is not None, "telemetry delivered",
           "available=%r" % (tele or {}).get("available"))

    # 6. LWT: SIGKILL skips our own offline publish, so the broker must send
    # the will.  The camera is then restarted with the bench API enabled so a
    # synthetic event can exercise the real queue and topic.
    code, text = ssh_run("kill -9 $(pidof divinus) 2>/dev/null; true")
    offline = wait_for(
        lambda: device_status(client_id).get("online") is False, 30)
    record(offline is not None, "LWT online:false after SIGKILL",
           text[:80])

    code, text = start_divinus(bench_api=True)
    record(code == 0 and text.strip().isdigit(), "divinus restarted",
           text[:80])
    if not wait_for(lambda: mqtt_status().get("connected") is True, 90):
        record(False, "publisher reconnected after the restart",
               mqtt_status().get("last_error", ""))
        return
    record(True, "publisher reconnected after the restart", client_id)
    online_again = wait_for(
        lambda: device_status(client_id).get("online") is True, 30)
    record(online_again is not None, "retained status back to online:true",
           client_id)

    # 7. One synthetic event through the normal queue and topic.
    event_ms = int(time.time() * 1000)
    status, dec = dut("/api/mqtt?test=1", "POST", b"")
    record(status == 200 and dec.get("note") == "MQTT test event queued",
           "test event queued", "HTTP %s %r" % (status, dec.get("note")))
    found = wait_for(
        lambda: [e for e in events(event_ms)
                 if (e.get("payload") or {}).get("type") == "test"], 30)
    payload = (found[0].get("payload") if found else {}) or {}
    record(bool(found), "event delivered",
           "type=%s id=%s" % (payload.get("type"), payload.get("event_id")))
    if found:
        record(str(payload.get("event_id", "")).startswith("bench-"),
               "event carries the injector id", str(payload.get("event_id")))
        record((payload.get("cause") or "") == "bench", "event cause",
               payload.get("cause", ""))

    if not args.keep_config:
        record(restore_config(before), "previous [mqtt] config restored")

    # Leave the camera without the bench-only injector; the config is already
    # restored (or intentionally kept) above.
    code, text = start_divinus(bench_api=False)
    record(code == 0 and text.strip().isdigit(),
           "divinus restarted without the bench API", text[:80])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--deploy", action="store_true",
                        help="deploy the freshly built divinus first")
    parser.add_argument("--neg", action="store_true",
                        help="profile is down: expect no connection")
    parser.add_argument("--keep-config", action="store_true",
                        help="keep the test [mqtt] settings on the DUT")
    parser.add_argument("--no-up", action="store_true",
                        help="do not start the compose profile")
    args = parser.parse_args()

    OUT.mkdir(parents=True, exist_ok=True)

    if args.deploy:
        code = subprocess.run(
            [str(ROOT / "bench" / "run.sh"), "deploy_divinus.py"],
            cwd=str(ROOT)).returncode
        if code:
            print("deploy failed")
            return 1
        time.sleep(5)

    if not args.neg and not args.no_up:
        up = compose_up()
        if up.returncode:
            print(up.stderr[-2000:])
            return 1

    status, dec = dut("/api/mqtt")
    if status != 200:
        record(False, "GET /api/mqtt",
               "HTTP %s: the image has no MQTT support" % status)
    elif args.neg:
        negative_case()
    else:
        positive_case(args)

    report = {
        "date": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "dut": HOST,
        "broker": MQTT_ADDR,
        "recorder": RECORDER,
        "checks": checks,
        "failed": failures,
        "result": "pass" if not failures else "fail",
    }
    (OUT / "report.json").write_text(json.dumps(report, ensure_ascii=False,
                                                indent=1))
    print("\n%d check(s), %d failure(s); report: %s" %
          (len(checks), len(failures), OUT / "report.json"))
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())