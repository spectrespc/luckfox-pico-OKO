#!/usr/bin/env python3
"""Sleep and power measurement matrix for the OKO camera.

Drives the camera (`[sleep]`, IVS/YOLO, motors, RTSP) while `bench/ina_log.py`
records the ESP32 bridge INA3221.  The bridge CH3 is the DUT 5 V input (T0
2026-11-22), so the CSV contains the whole-device power even while the SoC is
in deep suspend and its own INA3221 is offline.

Scenarios (`--scenario`):

    levels   rail off -> boot -> idle -> suspend floor
    cycle    one awake -> suspend -> probe -> suspend cycle
    soak     long sleep run with heartbeat, per-cycle energy, post checks
    peaks    awake: YOLO, full focus travel, RTSP/JPEG stream load
    stm8     STM8 driver sleep soak (divinus stopped), onboard cross-check

Every run writes output/bench/power/<stamp>-<scenario>/:

    raw.csv        ina_log CSV with phase markers
    events.log     JSONL state transitions and orchestration notes
    summary.json   phase statistics, energies, sanity checks
    report.md      Russian summary table

The DUT rail is restored in a `finally` block: `POST /ext/power` persists in
the bridge NVS, so an aborted run must never leave the camera unpowered.
Sleep and HTTP-post settings are restored unless --keep-config is given.

Examples:
    bench/run.sh sleep_power.py --scenario levels
    bench/run.sh sleep_power.py --scenario cycle --idle 30 --wake-check 5
    bench/run.sh sleep_power.py --scenario peaks --hz 8
    bench/run.sh sleep_power.py --scenario soak --soak-minutes 60
    bench/run.sh sleep_power.py --scenario stm8 --stm8-soak 600
"""
import argparse
import base64
import json
import os
import re
import shutil
import statistics
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import aov_i2c_diag                                           # noqa: E402
import ina_log                                                # noqa: E402
import power                                                  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
OUT_ROOT = ROOT / "output" / "bench" / "power"

HOST = os.environ.get("OKO_DUT_HOST", "192.168.52.33")
USER = os.environ.get("OKO_RTSP_USER", "admin")
PASSWORD = os.environ.get("OKO_DUT_PASSWORD", "oko-change-me")
SSH_PASSWORD = os.environ.get("OKO_DUT_SSH_PASSWORD", "luckfox")
BRIDGE = os.environ.get("OKO_BRIDGE", ina_log.BRIDGE)
SERVICE = os.environ.get("OKO_EVENTS_URL", "http://127.0.0.1:8090")
RECEIVER = os.environ.get("OKO_EVENTS_ADDR", "192.168.52.200:8090")
COMPOSE = ROOT / "services" / "events" / "docker-compose.yml"

SLEEP_KEYS = ["enable", "pmu_timer_wake", "aov_mcu", "aov_keep_isp",
              "idle_timeout_s", "wake_interval_s", "wake_check_s",
              "heartbeat", "heartbeat_interval_s", "heartbeat_retries"]
HTTP_KEYS = ["enable", "host", "url", "login", "width", "height", "interval",
             "qfactor", "receiver_url"]
FD_MIN, FD_MAX = 113, 116


def now_ms():
    return int(time.time() * 1000)


def log(message):
    print(message)
    sys.stdout.flush()


# --------------------------------------------------------------------------
# DUT HTTP API


def request(url, method="GET", body=None, timeout=8):
    req = urllib.request.Request(url, data=body, method=method)
    if url.startswith("http://%s" % HOST):
        token = base64.b64encode(
            ("%s:%s" % (USER, PASSWORD)).encode()).decode()
        req.add_header("Authorization", "Basic " + token)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.status, resp.read()
    except urllib.error.HTTPError as err:
        return err.code, err.read()
    except Exception as err:                                # noqa: BLE001
        return 0, str(err).encode()


def dut(path, method="GET", body=None, timeout=8):
    status, payload = request("http://%s%s" % (HOST, path), method, body,
                              timeout=timeout)
    try:
        return status, json.loads(payload.decode())
    except Exception:                                       # noqa: BLE001
        return status, {}


def post(path, values, timeout=8):
    query = urllib.parse.urlencode(values)
    return dut(path + "?" + query, "POST", b"", timeout=timeout)


def post_retry(path, values, attempts=6, timeout=15):
    """Retry transient connection races while the camera re-enters suspend."""
    status, dec = 0, {}
    for _attempt in range(attempts):
        status, dec = post(path, values, timeout=timeout)
        if status == 200 and not dec.get("error"):
            return status, dec
        if status not in (0, 503):
            return status, dec
        time.sleep(1)
    return status, dec


def wait_reachable(timeout=180):
    deadline = time.time() + timeout
    while time.time() < deadline:
        status, dec = dut("/api/status", timeout=4)
        if status == 200 and dec.get("sleep"):
            return dec
        time.sleep(1)
    return None


def sleep_state(timeout=5):
    status, dec = dut("/api/sleep", timeout=timeout)
    return dec if status == 200 else None


def live_state(timeout=2):
    """DUT sleep state or "unreachable" while the SoC is suspended."""
    dec = sleep_state(timeout=timeout)
    if dec is None:
        return "unreachable"
    return dec.get("state") or "unknown"


def cfg_from_sleep(dec):
    out = {}
    for key in SLEEP_KEYS:
        if key in dec:
            out[key] = dec[key]
    heartbeat = dec.get("heartbeat")
    if isinstance(heartbeat, dict):
        out["heartbeat"] = heartbeat.get("enable", False)
    return out


def as_query(dec):
    out = {}
    for key in SLEEP_KEYS:
        if key not in dec:
            continue
        value = dec[key]
        out[key] = ("true" if value else "false") if isinstance(value, bool) \
            else str(value)
    return out


def snapshot_config():
    """Sleep, [http_post] and analytics switches before the run."""
    status, dec = dut("/api/sleep")
    original = {"sleep": cfg_from_sleep(dec) if status == 200 else {},
                "http_post": {}, "quiet": {}}
    status, dec = dut("/api/http_post")
    if status == 200:
        original["http_post"] = {key: dec[key] for key in HTTP_KEYS
                                 if key in dec}
    for name, path in (("ivs", "/api/ivs"), ("yolo", "/api/yolo")):
        status, dec = dut(path)
        if status == 200:
            original["quiet"][name] = bool(dec.get("enable"))
    return original


def restart_streamer(timeout=20):
    dut("/api/cmd?restart", "POST", b"", timeout=timeout)


def apply_sleep(values):
    status, dec = post("/api/sleep", values)
    if status != 200 or dec.get("error"):
        raise RuntimeError("sleep settings refused: HTTP %s %s" %
                           (status, dec))
    restart_streamer()
    if wait_reachable(180) is None:
        raise RuntimeError("DUT did not come back after restart")
    return sleep_state() or {}


def set_quiet(enable):
    value = "true" if enable else "false"
    result = {}
    for name, path in (("ivs", "/api/ivs"), ("yolo", "/api/yolo")):
        status, dec = dut("%s?enable=%s" % (path, value), "POST", b"")
        result[name] = {"status": status, "enable": dec.get("enable")}
    return result


def restore_config(original, timeout=240):
    """Put [sleep], [http_post] and analytics back; False if anything failed."""
    if not original:
        return True
    deadline = time.time() + timeout
    errors = []
    while time.time() < deadline:
        if wait_reachable(30) is None:
            continue
        if original.get("sleep"):
            status, dec = post_retry("/api/sleep",
                                     as_query(original["sleep"]))
            if status != 200 or dec.get("error"):
                errors.append("sleep HTTP %s %s" % (status, dec))
                if "could not be saved" in str(dec.get("reason", "")):
                    errors.append("sleep save failed: проверьте /userdata "
                                  "(ro — KI-071, нужен power cycle)")
        if original.get("http_post"):
            values = {}
            for key in HTTP_KEYS:
                value = original["http_post"].get(key)
                if value is None:
                    continue
                values[key] = ("true" if value else "false") \
                    if isinstance(value, bool) else str(value)
            status, dec = post_retry("/api/http_post", values)
            if status != 200 or dec.get("error"):
                errors.append("http_post HTTP %s %s" % (status, dec))
        for name, enable in (original.get("quiet") or {}).items():
            status, dec = post_retry(
                "/api/%s" % name,
                {"enable": "true" if enable else "false"})
            if status != 200:
                errors.append("%s HTTP %s" % (name, status))
        restart_streamer()
        if wait_reachable(120) is not None:
            for message in errors:
                log("restore: %s" % message)
            return not errors
        errors.append("camera did not come back after restart")
        time.sleep(3)
    if not errors:
        errors.append("camera did not become reachable")
    for message in errors:
        log("restore: %s" % message)
    return False


# --------------------------------------------------------------------------
# SSH helpers (post-run checks, STM8 scenario)


def ssh_client(timeout=10, attempts=3):
    """SSH with banner retries: sshd on a loaded DUT often misses 8-10 s."""
    import paramiko
    last = None
    for attempt in range(1, attempts + 1):
        client = paramiko.SSHClient()
        client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        try:
            client.connect(HOST, username="root", password=SSH_PASSWORD,
                           timeout=max(timeout, 12),
                           banner_timeout=max(timeout, 20),
                           auth_timeout=max(timeout, 20))
            return client
        except Exception as exc:                            # noqa: BLE001
            last = exc
            try:
                client.close()
            except Exception:                               # noqa: BLE001
                pass
            if attempt < attempts:
                time.sleep(attempt)
    raise last if last is not None else RuntimeError("ssh failed")


def ssh_run(client, command, timeout=120):
    _stdin, stdout, stderr = client.exec_command(command, timeout=timeout)
    out = stdout.read().decode(errors="replace")
    err = stderr.read().decode(errors="replace")
    return stdout.channel.recv_exit_status(), out, err


AOV_SYSFS = "/sys/devices/platform/rockchip-suspend"
I2C_BUS = 3
I2C_MASTER = "ff460000.i2c"
GPIO1_EXT_PORT = 0xff530070
I2C_DEVICES = {0x40: "sht21", 0x41: "ina", 0x42: "ec", 0x60: "stm8"}

I2C_LINE_RE = re.compile(r"^([0-9a-fA-F]{2}):(.*)$")
PROBE_CMD = (
    "printf 'SCAN:'; i2cdetect -r -y 3 | tr '\\n' ';'; echo;"
    "printf 'AOV:%s\\n' \"$(cat " + AOV_SYSFS + "/aov_state 2>&1)\";"
    "printf 'PROXY:%s\\n' \"$(cat " + AOV_SYSFS + "/aov_wakeproxy 2>&1)\";"
    "printf 'MCURUN:%s\\n' \"$(devmem 0x01001C0C 32 2>&1)\";"
    "printf 'MCUCNT:%s\\n' \"$(devmem 0x01001C10 32 2>&1)\";"
    "printf 'GPIO:%s\\n' \"$(devmem 0xff530070 32 2>&1)\";"
    "printf 'EC:%s\\n' \"$(/oem/usr/bin/oko-ec --json battery 2>&1 | tail -1)\""
)


def parse_i2c_scan(text):
    """Addresses from `i2cdetect -r -y 3` output (rows like `40: 40 41 --`)."""
    found = []
    for line in text.splitlines():
        match = I2C_LINE_RE.match(line.strip())
        if not match:
            continue
        base = int(match.group(1), 16)
        for offset, token in enumerate(match.group(2).split()):
            if token in ("--", ""):
                continue
            found.append(base + offset)
    return sorted(found)


def focus_probe(timeout=4):
    """Motor block of `GET /api/focus`; safe while the SoC is awake."""
    status, dec = dut("/api/focus", timeout=timeout)
    motor = (dec.get("motor") or {}) if status == 200 else {}
    return {"http": status, "present": motor.get("present"),
            "version": motor.get("version"),
            "state": motor.get("status"),
            "bootloader": motor.get("bootloader")}


def _parse_probe(out, code, err=""):
    result = {"ssh": code, "present": None, "i2c": [], "aov_state": None,
              "aov_wakeproxy": None, "mcu_run_count": None,
              "mcu_counter": None, "gpio": None, "ec": None,
              "ec_error": (err or "").strip() or None}
    scan_text = ""
    for line in out.splitlines():
        if line.startswith("SCAN:"):
            scan_text = line[5:].replace(";", "\n")
        elif line.startswith("AOV:"):
            result["aov_state"] = line[4:].strip()
        elif line.startswith("PROXY:"):
            result["aov_wakeproxy"] = line[6:].strip()
        elif line.startswith("MCURUN:"):
            result["mcu_run_count"] = line[7:].strip()
        elif line.startswith("MCUCNT:"):
            result["mcu_counter"] = line[7:].strip()
        elif line.startswith("GPIO:"):
            result["gpio"] = line[5:].strip()
        elif line.startswith("EC:"):
            try:
                result["ec"] = json.loads(line[3:].strip())
            except ValueError:
                result["ec"] = None
    result["i2c"] = parse_i2c_scan(scan_text)
    result["present"] = 0x60 in result["i2c"]
    result["ec_present"] = 0x42 in result["i2c"]
    result["ina_present"] = 0x41 in result["i2c"]
    result["sht_present"] = 0x40 in result["i2c"]
    try:
        value = int(result["gpio"], 0)
        result["sda_level"] = 1 if value & (1 << 26) else 0
        result["scl_level"] = 1 if value & (1 << 27) else 0
    except (TypeError, ValueError):
        result["sda_level"] = result["scl_level"] = None
    return result


def stm8_probe(client=None, attempts=4):
    """I2C/AOV/EC probe with retries: a busy bus can fail one scan."""
    own = client is None
    last = {"ssh": None, "present": None, "i2c": [], "error": "no attempt"}
    try:
        for attempt in range(attempts):
            if own:
                try:
                    client = ssh_client()
                except Exception as exc:                    # noqa: BLE001
                    last = _parse_probe("", None, str(exc))
                    last["error"] = str(exc)
                    time.sleep(0.5)
                    continue
            try:
                code, out, err = ssh_run(client, PROBE_CMD, timeout=30)
                last = _parse_probe(out, code, err)
            except Exception as exc:                        # noqa: BLE001
                last = _parse_probe("", None, str(exc))
                last["error"] = str(exc)
            if own:
                try:
                    client.close()
                except Exception:                           # noqa: BLE001
                    pass
                client = None
            if last.get("present"):
                break
            if attempt + 1 < attempts:
                time.sleep(1.0)
        return last
    finally:
        if own and client is not None:
            try:
                client.close()
            except Exception:                               # noqa: BLE001
                pass


def dump_dmesg(outdir, tag):
    """Save the kernel log before an i2cdetect scan floods the ring buffer."""
    try:
        client = ssh_client()
        try:
            code, out, err = ssh_run(client, "dmesg", timeout=25)
        finally:
            client.close()
        text = out if code == 0 else (out + err)
        (outdir / ("dmesg-%s.txt" % tag)).write_text(text)
        return len(text.splitlines())
    except Exception as exc:                                # noqa: BLE001
        (outdir / ("dmesg-%s.txt" % tag)).write_text(
            "dmesg failed: %s\n" % exc)
        return 0


def stm8_recovery(outdir, events, notes, reason="", allow_rebind=True,
                  allow_ec_reset=False, allow_bitbang=True):
    """Recovery ladder from T1.3; returns the first step that revived the bus."""
    lines = ["# Лестница восстановления STM8", "", "Причина: %s" % reason, ""]
    verdict = {"layer": "unknown", "recovered": False, "step": None,
               "steps": []}
    client = None

    def save_diag(snap, tag):
        try:
            (outdir / ("i2c_regs-%s.txt" % tag)).write_text(
                aov_i2c_diag.format_snapshot(snap, "I2C3 %s" % tag))
        except Exception:                                   # noqa: BLE001
            pass
        lines.extend(["", "```", "\n".join(aov_i2c_diag.interpret(snap)),
                      "```", ""])
        events.write("i2c-diag", tag=tag, pads=snap.get("pads", {}).get("raw"),
                     verdict=aov_i2c_diag.interpret(snap)[:3])

    def record(name, detail, probe=None):
        verdict["steps"].append({"step": name, "detail": detail,
                                 "present": None if probe is None
                                 else probe.get("present")})
        lines.extend(["## %s" % name, "```", detail.strip() or "(пусто)",
                      "```", "STM8 present: %s" % (None if probe is None
                                                   else probe.get("present")),
                      ""])
        events.write("recovery-step", step=name, present=None if probe is None
                     else probe.get("present"), detail=detail.strip()[-400:])

    try:
        client = ssh_client(timeout=8)
        base = stm8_probe(client)
        record("Шаг 1. dmesg и скан шины",
               "i2c: %s\nEC: %s, INA: %s, SHT: %s" % (
                   ",".join("0x%02x" % address for address in base["i2c"]) or
                   "пусто", base.get("ec_present"), base.get("ina_present"),
                   base.get("sht_present")), base)
        code, dmesg, _err = ssh_run(client, "dmesg | tail -50")
        lines.extend(["```", dmesg.strip(), "```", ""])
        if base["present"]:
            verdict.update(layer="bus-alive", recovered=True, step="probe")
            return verdict
        if not (base.get("ec_present") or base.get("ina_present") or
                base.get("sht_present")):
            verdict["layer"] = "bus-or-controller-ap"
        else:
            verdict["layer"] = "stm8-specific"
        try:
            diag = aov_i2c_diag.snapshot(client, scan=False)
            save_diag(diag, "dead")
            record("Шаг 1b. Регистры I2C3 на мёртвой шине",
                   "\n".join(aov_i2c_diag.interpret(diag)), base)
        except Exception as exc:                            # noqa: BLE001
            notes.append("Дамп регистров I2C3 не снят: %s" % exc)
        if allow_rebind:
            ssh_run(client, "echo %s > /sys/bus/platform/drivers/rk3x-i2c/"
                            "unbind; sleep 1" % I2C_MASTER, timeout=30)
            try:
                diag = aov_i2c_diag.snapshot(client, scan=False)
                save_diag(diag, "unbound")
                record("Шаг 2a. I2C3 после unbind",
                       "\n".join(aov_i2c_diag.interpret(diag)), None)
            except Exception as exc:                        # noqa: BLE001
                notes.append("Дамп после unbind не снят: %s" % exc)
            if allow_bitbang:
                try:
                    bang = aov_i2c_diag.bitbang(client)
                    record("Шаг 2b. bit-bang 9 тактов SCL", bang["output"], None)
                    diag = aov_i2c_diag.snapshot(client, scan=False)
                    save_diag(diag, "bitbang")
                except Exception as exc:                    # noqa: BLE001
                    record("Шаг 2b. bit-bang 9 тактов SCL",
                           "ошибка: %s" % exc, None)
            else:
                record("Шаг 2b. bit-bang 9 тактов SCL",
                       "пропущен по --no-ladder-bitbang", None)
            aov_i2c_diag.restore_iomux(client)
            code, out, err = ssh_run(
                client, "echo %s > /sys/bus/platform/drivers/rk3x-i2c/"
                        "bind; sleep 2; i2cdetect -r -y %d" %
                        (I2C_MASTER, I2C_BUS), timeout=40)
            after = stm8_probe(client)
            record("Шаг 2c. rebind rk3x-i2c", out + err, after)
            if after["present"]:
                verdict.update(layer=verdict["layer"] + "/rebind",
                               recovered=True, step="unbind-bitbang-rebind")
                return verdict
        else:
            record("Шаг 2. unbind/rebind rk3x-i2c",
                   "пропущен по --no-ladder-rebind; на i2c3 "
                   "(ff460000.i2c) шаг безопасен и восстанавливает шину; "
                   "bit-bang без unbind не выполняется", base)
        sda, scl = base.get("sda_level"), base.get("scl_level")
        detail = "GPIO1_D2/SDA=%s, GPIO1_D3/SCL=%s (devmem 0x%08x)" % (
            sda, scl, GPIO1_EXT_PORT)
        record("Шаг 3. Уровни GPIO шины", detail, base)
        if scl == 0:
            notes.append("SCL удержан низко: кандидат H3 (EC/STM32 держит "
                         "шину, регресс KI-021).")
        elif sda == 0:
            notes.append("SDA удержан низко: slave-залипание STM8/EC.")
        code, out, err = ssh_run(
            client, "/oem/usr/bin/oko-motor reset; sleep 2; "
                    "/oem/usr/bin/oko-motor sleep show", timeout=30)
        after = stm8_probe(client)
        record("Шаг 4. oko-motor reset", out + err, after)
        if after["present"]:
            verdict.update(layer="stm8-stuck", recovered=True,
                           step="motor-reset")
            return verdict
        if allow_ec_reset:
            code, out, err = ssh_run(
                client, "/oem/usr/bin/oko-ec reset; sleep 3; "
                        "/oem/usr/bin/oko-motor sleep show", timeout=40)
            after = stm8_probe(client)
            record("Шаг 5. oko-ec reset (EC reboot)", out + err, after)
            if after["present"]:
                verdict.update(layer="stm8-stuck/ec-reset", recovered=True,
                               step="ec-reset")
                return verdict
        else:
            record("Шаг 5. oko-ec reset (EC reboot)",
                   "пропущен: перезагрузка EC может нарушить питание/шину; "
                   "включать только с --ladder-ec-reset", base)
        notes.append("Лестница не восстановила STM8: нужен power cycle и "
                     "SWIM-диагностика на тестовой плате.")
        verdict["layer"] = verdict["layer"] + "/unrecovered"
        return verdict
    except Exception as exc:                                # noqa: BLE001
        notes.append("Лестница восстановления не выполнена: %s" % exc)
        verdict["layer"] = "ssh-error"
        verdict["error"] = str(exc)
        return verdict
    finally:
        if client is not None:
            try:
                client.close()
            except Exception:                               # noqa: BLE001
                pass
        try:
            (outdir / "stm8-recovery.md").write_text("\n".join(lines) + "\n")
        except OSError:
            pass


def motor_sleep_config(action):
    """Set [motor] sleep_enable and restart divinus; returns the old YAML."""
    client = ssh_client()
    try:
        _code, original, _err = ssh_run(
            client, "cat /userdata/divinus.yaml", timeout=20)
        ssh_run(client, "/etc/init.d/S21appinit stop", timeout=60)
        value = "true" if action == "on" else "false"
        ssh_run(client, "sed -i 's/^\\(  sleep_enable: \\).*/\\1%s/' "
                        "/userdata/divinus.yaml" % value, timeout=20)
        ssh_run(client, "/etc/init.d/S21appinit start", timeout=60)
    finally:
        client.close()
    wait_reachable(120)
    return original


def motor_sleep_restore(original):
    if not original:
        return False
    client = ssh_client()
    try:
        ssh_run(client, "/etc/init.d/S21appinit stop", timeout=60)
        encoded = base64.b64encode(original.encode()).decode()
        ssh_run(client, "printf %s | base64 -d > /userdata/divinus.yaml" %
                encoded, timeout=20)
        ssh_run(client, "/etc/init.d/S21appinit start", timeout=60)
    finally:
        client.close()
    return wait_reachable(120) is not None


SUSPEND_RE = re.compile(r"\[\s*(\d+\.\d+)\]\s+PM: suspend (entry|exit)")


def kernel_suspend_durations(notes=None):
    """`PM: suspend entry` -> `exit` windows from the kernel ring buffer.

    Kernel log timestamps use CLOCK_MONOTONIC, which does not advance while
    the AP is in deep suspend, so this window is the **resume path only**;
    it used to be misread as the sleep length (a 60 s sleep shows ~2.4 s).
    Use `last_suspend_sleep()` for the asleep time.  The ring buffer only
    keeps the latest part of a long run, so the count is reported as
    "last N windows".
    """
    try:
        client = ssh_client()
        try:
            code, out, err = ssh_run(client, "dmesg")
        finally:
            client.close()
        if code != 0:
            raise RuntimeError((out + err).strip())
    except Exception as exc:                                # noqa: BLE001
        if notes is not None:
            notes.append("Окна deep suspend не сняты: %s" % exc)
        return []
    durations = []
    pending = None
    for line in out.splitlines():
        match = SUSPEND_RE.search(line)
        if not match:
            continue
        stamp = float(match.group(1))
        if match.group(2) == "entry":
            pending = stamp
        elif pending is not None:
            durations.append(round(stamp - pending, 2))
            pending = None
    return durations


def last_suspend_sleep(notes=None):
    """Asleep time of the last suspend from /sys/kernel/wakeup_reasons.

    The kernel writes "<resume path> <sleep>" into `last_suspend_time`,
    where the second value is derived from boottime and therefore *includes*
    the deep-suspend window.  It is the only direct sleep measurement on the
    device; one reading covers the last cycle of the run.
    """
    try:
        client = ssh_client()
        try:
            code, out, err = ssh_run(
                client, "cat /sys/kernel/wakeup_reasons/last_suspend_time")
        finally:
            client.close()
        if code != 0:
            raise RuntimeError((out + err).strip())
        resume_s, sleep_s = (float(value) for value in out.split())
        return {"resume_s": round(resume_s, 2), "sleep_s": round(sleep_s, 2)}
    except Exception as exc:                                # noqa: BLE001
        if notes is not None:
            notes.append("last_suspend_time не прочитан: %s" % exc)
        return {}


def sleep_table(reading):
    return ["| Resume-путь, с | Сон, с |", "|---|---|",
            "| %.2f | %.2f |" % (reading["resume_s"], reading["sleep_s"])]


def kernel_pm_profile(notes=None):
    """`oko-pm:` phase lines of the diagnostic PM kernel (empty otherwise)."""
    try:
        client = ssh_client()
        try:
            code, out, err = ssh_run(
                client, "dmesg | grep oko-pm: | tail -12")
        finally:
            client.close()
        if code != 0:
            raise RuntimeError((out + err).strip())
    except Exception as exc:                                # noqa: BLE001
        if notes is not None:
            notes.append("oko-pm профиль не снят: %s" % exc)
        return []
    return [line.strip() for line in out.splitlines() if "oko-pm:" in line]


def profile_table(lines):
    table = ["| Строка профиля |", "|---|"]
    for line in lines:
        table.append("| `%s` |" % line.replace("|", "\\|"))
    return table


def suspend_table(durations):
    if not durations:
        return [], {}
    lines = ["| Окно | Секунд |", "|---|---|"]
    for index, seconds in enumerate(durations, 1):
        lines.append("| %d | %.2f |" % (index, seconds))
    mean = sum(durations) / len(durations)
    lines.append("| **среднее** | %.2f |" % mean)
    return lines, {"count": len(durations), "mean_s": round(mean, 2)}


def floor_stats(rows, channel):
    """Low-current floor from the distribution (no clock alignment)."""
    values = sorted(row["cur"] for row in rows if row["ch"] == channel)
    if not values:
        return {}
    median = values[len(values) // 2]
    threshold = values[0] + 0.25 * (median - values[0])
    low = [value for value in values if value <= threshold]

    def percentile(fraction):
        return values[min(len(values) - 1, int(len(values) * fraction))]

    return {"min_ma": values[0],
            "p05_ma": percentile(0.05),
            "p10_ma": percentile(0.10),
            "median_ma": median,
            "threshold_ma": round(threshold, 1),
            "low_n": len(low),
            "low_mean_ma": round(sum(low) / len(low), 1) if low else 0.0,
            "fraction": round(100.0 * len(low) / len(values), 1)}


def suspend_starts(rows, channel, median_ma=None):
    """First sample of every low-current (deep suspend) window.

    A plain threshold misses windows when the 2 Hz sampler never lands on the
    trough, merging two cycles into one.  A local minimum well below the
    median is stable at 2 Hz and matches the dmesg cycle count.
    """
    points = [(row["ts"], row["cur"]) for row in rows
              if row["ch"] == channel]
    if len(points) < 3:
        return []
    if median_ma is None:
        median_ma = statistics.median([value for _ts, value in points])
    starts = []
    armed = True
    values = [value for _ts, value in points]
    for index, (stamp, value) in enumerate(points):
        window = values[max(0, index - 2):index + 3]
        if armed and value == min(window) and value < 0.72 * median_ma:
            starts.append(stamp)
            armed = False
        elif not armed and value > 0.95 * median_ma:
            armed = True
    return starts


def floor_table(floor, channel):
    if not floor:
        return []
    return ["| CH%d | мА |" % channel, "|---|---|",
            "| минимум | %.1f |" % floor["min_ma"],
            "| p05 | %.1f |" % floor["p05_ma"],
            "| p10 | %.1f |" % floor["p10_ma"],
            "| порог низкой моды | %.1f |" % floor["threshold_ma"],
            "| среднее ниже порога | %.1f |" % floor["low_mean_ma"],
            "| доля отсчётов | %.1f %% |" % floor["fraction"]]


# --------------------------------------------------------------------------
# Artifacts


class Events:
    def __init__(self, path):
        self.path = Path(path)
        self.stream = open(self.path, "w")

    def write(self, event, **fields):
        record = {"ts_host_ms": now_ms(),
                  "ts_iso": datetime.now().isoformat(timespec="milliseconds"),
                  "event": event}
        record.update(fields)
        self.stream.write(json.dumps(record, ensure_ascii=False) + "\n")
        self.stream.flush()

    def close(self):
        self.stream.close()


def read_rows(path):
    import csv
    rows = []
    with open(path, newline="") as stream:
        for row in csv.DictReader(stream):
            if row.get("err"):
                continue
            if not row.get("cur_ma"):
                continue
            try:
                row["ts"] = int(row["ts_host_epoch_ms"])
                row["cur"] = float(row["cur_ma"])
                row["pwr"] = float(row["power_mw"])
                row["ch"] = int(row["ch"])
            except (TypeError, ValueError):
                continue
            rows.append(row)
    return rows


def integrate(rows, channel, start=None, end=None, phase=None,
              max_gap_ms=5000):
    points = [row for row in rows if row["ch"] == channel and
              (phase is None or row.get("phase") == phase) and
              (start is None or row["ts"] >= start) and
              (end is None or row["ts"] <= end)]
    if not points:
        return {"n": 0, "seconds": 0.0, "energy_mwh": 0.0,
                "charge_mah": 0.0}
    energy = charge = 0.0
    for first, second in zip(points, points[1:]):
        gap = second["ts"] - first["ts"]
        if gap > max_gap_ms:
            continue
        hours = max(0, gap) / 3600000.0
        energy += (first["pwr"] + second["pwr"]) / 2.0 * hours
        charge += (first["cur"] + second["cur"]) / 2.0 * hours
    seconds = (points[-1]["ts"] - points[0]["ts"]) / 1000.0
    return {"n": len(points),
            "seconds": round(seconds, 1),
            "energy_mwh": round(energy, 2),
            "charge_mah": round(charge, 2),
            "mean_mw": round(sum(row["pwr"] for row in points) / len(points), 1),
            "mean_ma": round(sum(row["cur"] for row in points) / len(points), 1),
            "min_mw": min(row["pwr"] for row in points),
            "max_mw": max(row["pwr"] for row in points),
            "max_ma": max(row["cur"] for row in points)}


def write_report(path, scenario, args, summary, extras):
    lines = ["# Замер мощности: %s" % scenario, "",
             "- Дата: %s" % datetime.now().isoformat(timespec="seconds"),
             "- DUT: `%s`, мост: `%s`" % (HOST, BRIDGE),
             "- Канал: CH%d (вход 5 В DUT), `--hz %s`" %
             (args.channel, args.hz),
             "- Артефакты: `%s`" % path.parent, ""]
    stats = summary.get("stats", {})
    channel = "ch%d" % args.channel
    if channel in stats:
        item = stats[channel]
        lines += ["## Сводка канала CH%d" % args.channel, "",
                  "| Показатель | мА | мВт |", "|---|---|---|",
                  "| среднее | %.1f | %.1f |" %
                  (item["cur_ma"]["mean"], item["power_mw"]["mean"]),
                  "| медиана | %.1f | %.1f |" %
                  (item["cur_ma"]["p50"], item["power_mw"]["p50"]),
                  "| минимум | %.1f | %.1f |" %
                  (item["cur_ma"]["min"], item["power_mw"]["min"]),
                  "| максимум | %.1f | %.1f |" %
                  (item["cur_ma"]["max"], item["power_mw"]["max"]),
                  "| энергия | — | %.1f мВт·ч |" %
                  item["energy_mwh"], ""]
    rows = read_rows(summary["csv"]) if summary.get("csv") else []
    if summary.get("phases_s"):
        lines += ["## Фазы", "", "| Фаза | Секунды | мА (сред.) | "
                  "мВт (сред.) | мВт·ч |", "|---|---|---|---|---|"]
        for phase, seconds in summary["phases_s"].items():
            item = integrate(rows, args.channel, phase=phase)
            lines.append("| `%s` | %.1f | %.1f | %.1f | %.2f |" %
                         (phase, seconds, item.get("mean_ma", 0),
                          item.get("mean_mw", 0),
                          item.get("energy_mwh", 0)))
        lines.append("")
    for title, table in extras.get("tables", []):
        lines += ["## %s" % title, ""]
        lines += table
        lines.append("")
    for note in extras.get("notes", []):
        lines.append("- %s" % note)
    lines.append("")
    path.write_text("\n".join(lines))


# --------------------------------------------------------------------------
# Scenarios


class NullLogger:
    """No bridge INA session: scenarios still run over DUT API/SSH only."""

    def __init__(self):
        self.phase = None
        self.started = time.time()

    def set_phase(self, phase):
        self.phase = phase

    def stop(self):
        return {"duration_s": round(time.time() - self.started, 1),
                "rows": 0, "samples": 0, "errors": 0, "missed_polls": 0,
                "csv": None, "stats": {}, "phases_s": {}}


def start_logger(args, outdir, label):
    if getattr(args, "no_power_logger", False):
        return NullLogger()
    csv_path = outdir / "raw.csv"
    summary_path = outdir / "logger.json"
    return ina_log.start_logging(
        hz=args.hz, channels=(args.channel,), out=csv_path,
        summary_json=summary_path, label=label, bridge=args.bridge,
        cookie_file=args.cookie_file, keep_session=False)


def wait_for_suspend(args, handle, events, timeout=None):
    """Track states until the AP leaves HTTP reachability (suspend begins)."""
    timeout = timeout or args.cycle_timeout
    deadline = time.time() + timeout
    last = None
    while time.time() < deadline:
        state = live_state()
        phase = {"awake": "awake", "suspending": "suspending",
                 "suspended": "suspend", "probe": "probe",
                 "unreachable": "offline"}.get(state, state)
        if phase != last:
            handle.set_phase(phase)
            events.write("state", state=state, phase=phase)
            last = phase
        if phase in ("suspend", "offline", "suspending"):
            return True
        time.sleep(0.5)
    return False


def scenario_levels(args, outdir, events):
    handle = start_logger(args, outdir, "off")
    original = {}
    powered_off = False
    notes = []
    try:
        original = snapshot_config()
        powered_off = True
        power.run("off", bridge=args.bridge,
                  cookie_file=args.cookie_file, keep_session=True)
        time.sleep(2)
        handle.set_phase("off")
        events.write("phase", phase="off", detail="rail off")
        time.sleep(args.off_seconds)
        handle.set_phase("boot")
        events.write("phase", phase="boot", detail="rail on")
        power.run("on", bridge=args.bridge,
                  cookie_file=args.cookie_file, keep_session=True)
        powered_off = False
        if wait_reachable(args.boot_timeout) is None:
            raise RuntimeError("DUT did not boot after power on")
        boot_done = now_ms()
        events.write("boot", boot_ms=boot_done)
        time.sleep(args.settle)
        handle.set_phase("idle")
        events.write("phase", phase="idle")
        time.sleep(args.idle_seconds)
        set_quiet(False)
        floor_wake = max(args.wake_interval, int(args.suspend_seconds) + 30)
        applied = apply_sleep({
            "enable": "true", "pmu_timer_wake": "true",
            "aov_mcu": "false", "aov_keep_isp": "false",
            "idle_timeout_s": str(args.idle), "wake_interval_s": str(floor_wake),
            "wake_check_s": str(args.wake_check), "heartbeat": "false"})
        events.write("sleep-applied", state=applied.get("state"),
                     wake_interval_s=floor_wake)
        handle.set_phase("await-suspend")
        if not wait_for_suspend(args, handle, events):
            raise RuntimeError("camera did not enter suspend")
        handle.set_phase("suspend")
        events.write("phase", phase="suspend",
                     detail="suspend floor, wake_interval_s=%d" % floor_wake)
        time.sleep(args.suspend_seconds)
        notes.append("Сон: idle_timeout_s=%d, wake_interval_s=%d, "
                     "wake_check_s=%d; полка измерена за %.0f с." %
                     (args.idle, floor_wake, args.wake_check,
                      args.suspend_seconds))
    finally:
        if powered_off:
            for attempt in range(3):
                try:
                    power.run("on", bridge=args.bridge,
                              cookie_file=args.cookie_file,
                              keep_session=True)
                    powered_off = False
                    break
                except (RuntimeError, OSError) as exc:
                    notes.append("Включение питания, попытка %d: %s" %
                                 (attempt + 1, exc))
                    time.sleep(3)
            if powered_off:
                notes.append("КРИТИЧНО: DUT остался обесточен, включите "
                             "питание вручную (мост /ext/power).")
        handle.set_phase("restore")
        wait_reachable(180)
        durations = kernel_suspend_durations(notes)
        restored = True
        if not args.keep_config:
            restored = restore_config(original)
            if not restored:
                notes.append("Не удалось полностью восстановить конфиг "
                             "[sleep]/[http_post]/аналитику.")
        summary = handle.stop()
        events.write("logger", **{key: summary.get(key) for key in
                                  ("duration_s", "rows", "samples",
                                   "errors", "missed_polls")})
        extras = {"notes": notes}
        tables = []
        rows = read_rows(summary["csv"]) if summary.get("csv") else []
        suspend_rows = [row for row in rows
                        if row.get("phase") in ("suspend", "offline")]
        floor = floor_stats(suspend_rows, args.channel)
        if floor:
            tables.append(("Полка по распределению тока", floor_table(
                floor, args.channel)))
            notes.append("Полка: минимум %.1f мА, p05 %.1f мА, p10 %.1f мА, "
                         "ниже порога %.1f мА — %.1f %% отсчётов." %
                         (floor["min_ma"], floor["p05_ma"], floor["p10_ma"],
                          floor["threshold_ma"], floor["fraction"]))
        if durations:
            table, stats = suspend_table(durations)
            tables.append(("Resume-путь suspend entry→exit (dmesg, без сна)", table))
            notes.append("dmesg: последние %d окон entry→exit (resume-путь), среднее "
                         "%.2f с (кольцевой буфер)." %
                         (stats["count"], stats["mean_s"]))
        else:
            notes.append("Deep suspend не подтверждён dmesg: полка "
                         "недействительна, повторите прогон.")
        if tables:
            extras["tables"] = tables
        write_report(outdir / "report.md", "levels", args, summary, extras)
        result = {"summary": summary, "original": original,
                  "durations": durations, "restored": restored,
                  "floor_valid": bool(durations)}
    return result


def scenario_cycle(args, outdir, events):
    handle = start_logger(args, outdir, "setup")
    original = {}
    notes = []
    try:
        original = snapshot_config()
        set_quiet(False)
        applied = apply_sleep({
            "enable": "true", "pmu_timer_wake": "true",
            "aov_mcu": "false", "aov_keep_isp": "false",
            "idle_timeout_s": str(args.idle),
            "wake_interval_s": str(args.wake_interval),
            "wake_check_s": str(args.wake_check), "heartbeat": "false"})
        events.write("sleep-applied", state=applied.get("state"),
                     idle_timeout_s=applied.get("idle_timeout_s"),
                     wake_interval_s=applied.get("wake_interval_s"),
                     wake_check_s=applied.get("wake_check_s"))
        handle.set_phase("awake")
        events.write("phase", phase="awake", detail="waiting for idle timeout")
        deadline = time.time() + args.cycle_timeout
        seen_awake = seen_suspend = seen_probe = done = False
        last = None
        while time.time() < deadline and not done:
            state = live_state()
            phase = {"awake": "awake", "suspending": "suspending",
                     "suspended": "suspend", "probe": "probe",
                     "unreachable": "offline"}.get(state, state)
            if phase != last:
                handle.set_phase(phase)
                events.write("state", state=state, phase=phase)
                last = phase
            if phase == "awake":
                seen_awake = True
            elif phase == "probe":
                seen_probe = True
            elif phase in ("suspend", "offline") and seen_awake:
                if seen_probe:
                    done = True
                seen_suspend = True
            time.sleep(0.5)
        if not done:
            raise RuntimeError("no full awake->suspend->probe->suspend cycle")
        time.sleep(args.tail_seconds)
        notes.append("Полный цикл: idle=%d с, wake_interval=%d с, "
                     "wake_check=%d с." % (args.idle, args.wake_interval,
                                           args.wake_check))
        if not seen_suspend:
            notes.append("Suspend не наблюдался.")
    finally:
        handle.set_phase("restore")
        wait_reachable(180)
        durations = kernel_suspend_durations(notes)
        last_sleep = last_suspend_sleep(notes)
        restored = True
        if not args.keep_config:
            restored = restore_config(original)
            if not restored:
                notes.append("Не удалось полностью восстановить конфиг "
                             "[sleep]/[http_post]/аналитику.")
        summary = handle.stop()
        events.write("logger", **{key: summary.get(key) for key in
                                  ("duration_s", "rows", "samples",
                                   "errors", "missed_polls")})
        extras = {"notes": notes}
        tables = []
        rows = read_rows(summary["csv"]) if summary.get("csv") else []
        suspend_rows = [row for row in rows
                        if row.get("phase") in ("suspend", "offline")]
        floor = floor_stats(suspend_rows, args.channel)
        if floor:
            tables.append(("Полка по распределению тока", floor_table(
                floor, args.channel)))
            notes.append("Полка: минимум %.1f мА, p05 %.1f мА, p10 %.1f мА, "
                         "ниже порога %.1f мА — %.1f %% отсчётов." %
                         (floor["min_ma"], floor["p05_ma"], floor["p10_ma"],
                          floor["threshold_ma"], floor["fraction"]))
        if last_sleep:
            tables.append(("Фактический сон последнего цикла (sysfs)",
                           sleep_table(last_sleep)))
            notes.append("Сон последнего цикла: %.2f с (resume-путь %.2f с); "
                         "интервал должен совпадать с wake_interval_s." %
                         (last_sleep["sleep_s"], last_sleep["resume_s"]))
        if durations:
            table, stats = suspend_table(durations)
            tables.append(("Resume-путь suspend entry→exit (dmesg, без сна)", table))
            notes.append("dmesg: последние %d окон entry→exit (resume-путь), среднее "
                         "%.2f с (кольцевой буфер)." %
                         (stats["count"], stats["mean_s"]))
        else:
            notes.append("Deep suspend не подтверждён dmesg: полка "
                         "недействительна, повторите прогон.")
        if tables:
            extras["tables"] = tables
        write_report(outdir / "report.md", "cycle", args, summary, extras)
        result = {"summary": summary, "original": original,
                  "durations": durations, "restored": restored,
                  "last_sleep": last_sleep,
                  "floor_valid": bool(durations)}
    return result


def scenario_peaks(args, outdir, events):
    handle = start_logger(args, outdir, "base")
    original = {}
    notes = []
    moved = False
    try:
        original = snapshot_config()
        handle.set_phase("base")
        events.write("phase", phase="base")
        time.sleep(args.base_seconds)
        status, dec = dut("/api/yolo?enable=1", "POST", b"")
        yolo_on = status == 200 and dec.get("enable") is True
        handle.set_phase("yolo")
        events.write("phase", phase="yolo", status=status, enable=yolo_on,
                     reason=dec.get("reason", ""))
        if yolo_on:
            time.sleep(args.yolo_seconds)
        else:
            notes.append("YOLO недоступен: HTTP %s %s" % (status, dec))
        dut("/api/yolo?enable=0", "POST", b"")
        # Full focus travel: several seconds of continuous stepper current.
        status, dec = dut("/api/focus")
        motor = (dec.get("motor") or {}) if status == 200 else {}
        focus_before = (motor.get("focus") or {}).get("position")
        focus_was_enabled = dec.get("enable")
        motor_was_enabled = motor.get("enabled")
        handle.set_phase("motor")
        events.write("phase", phase="motor", status=status,
                     before=focus_before)
        steps = str(args.motor_steps)
        speed = str(args.motor_speed)
        out_status, out_dec = dut(
            "/api/focus?motor=1&scale=focus&speed=%s&move=%s" %
            (speed, steps), "POST", b"")
        if out_status == 200 and not out_dec.get("error"):
            moved = True
            travel = max(1.0, args.motor_steps / float(args.motor_speed))
            time.sleep(travel)
            dut("/api/focus?scale=focus&speed=%s&move=-%s" %
                (speed, steps), "POST", b"")
            time.sleep(travel)
            dut("/api/focus?motor=0", "POST", b"")
        else:
            notes.append("Мотор не отработал: HTTP %s %s" %
                         (out_status, out_dec))
        handle.set_phase("stream")
        events.write("phase", phase="stream", detail=args.rtsp_mode)
        pull_stream(args, notes)
        handle.set_phase("base-after")
        events.write("phase", phase="base-after")
        time.sleep(args.base_seconds)
        if moved and focus_before is not None:
            dut("/api/focus?motor=1&scale=focus&position=%s" %
                focus_before, "POST", b"")
        if not motor_was_enabled:
            dut("/api/focus?motor=0", "POST", b"")
        if not focus_was_enabled:
            dut("/api/focus?enable=0", "POST", b"")
        notes.append("Разрешение опроса %.1f Гц: пики короче %.0f мс не "
                     "видны, ограничение указано в отчёте." %
                     (args.hz, 1000.0 / args.hz))
    finally:
        handle.set_phase("restore")
        restored = True
        if not args.keep_config:
            restored = restore_config(original)
            if not restored:
                notes.append("Не удалось полностью восстановить конфиг "
                             "[sleep]/[http_post]/аналитику.")
        summary = handle.stop()
        events.write("logger", **{key: summary.get(key) for key in
                                  ("duration_s", "rows", "samples",
                                   "errors", "missed_polls")})
        extras = {"notes": notes}
        write_report(outdir / "report.md", "peaks", args, summary, extras)
        result = {"summary": summary, "original": original,
                  "restored": restored}
    return result


def pull_stream(args, notes):
    url = args.rtsp_url or ("rtsp://%s:%s@%s:554/" % (USER, PASSWORD, HOST))
    ffmpeg = shutil.which("ffmpeg")
    if ffmpeg:
        command = [ffmpeg, "-hide_banner", "-loglevel", "error",
                   "-rtsp_transport", "tcp", "-i", url, "-t",
                   str(args.stream_seconds), "-f", "null", "-"]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode != 0:
            notes.append("ffmpeg RTSP: код %d (%s)" %
                         (result.returncode, result.stderr.strip()[:160]))
        return
    deadline = time.time() + args.stream_seconds
    frames = errors = 0
    while time.time() < deadline:
        status, body = request("http://%s/image.jpg" % HOST, timeout=3)
        if status == 200:
            frames += 1
        else:
            errors += 1
        time.sleep(0.2)
    notes.append("ffmpeg нет: вместо RTSP снят /image.jpg (%d кадров, "
                 "%d ошибок)." % (frames, errors))


def scenario_soak(args, outdir, events):
    handle = start_logger(args, outdir, "setup")
    original = {}
    notes = []
    receiver_started = False
    heartbeats_before = heartbeats_after = None
    run_start_ms = now_ms()
    try:
        original = snapshot_config()
        if not args.no_start_service:
            subprocess.run(["docker", "compose", "-f", str(COMPOSE), "up",
                            "-d"], cwd=str(ROOT), capture_output=True,
                           text=True)
            receiver_started = wait_service(args.service_timeout)
            notes.append("Приёмник services/events: %s" %
                         ("поднят" if receiver_started else "недоступен"))
        heartbeats_before = heartbeat_count()
        post("/api/http_post", {
            "enable": "true", "host": args.receiver,
            "url": "/ingest/frame", "interval": "3600"})
        set_quiet(False)
        apply_sleep({
            "enable": "true", "pmu_timer_wake": "true",
            "aov_mcu": "false", "aov_keep_isp": "false",
            "idle_timeout_s": str(args.idle),
            "wake_interval_s": str(args.wake_interval),
            "wake_check_s": str(args.wake_check),
            "heartbeat": "true", "heartbeat_interval_s": "0",
            "heartbeat_retries": "3"})
        events.write("phase", phase="soak",
                     minutes=args.soak_minutes,
                     receiver=receiver_started)
        deadline = time.time() + args.soak_minutes * 60
        last = None
        while time.time() < deadline:
            state = live_state()
            phase = {"awake": "awake", "suspending": "suspending",
                     "suspended": "suspend", "probe": "probe",
                     "unreachable": "offline"}.get(state, state)
            if phase != last:
                handle.set_phase(phase)
                events.write("state", state=state, phase=phase)
                last = phase
            time.sleep(1)
        heartbeats_after = heartbeat_count(since_ms=run_start_ms)
        wait_reachable(180)
        dec = sleep_state() or {}
        heartbeat = dec.get("heartbeat") or {}
        notes.append("Heartbeat за прогон: ≥%s (буфер приёмника хранит "
                     "последние 200, до прогона %s), wake_cycle=%s, "
                     "fail_streak=%s." %
                     (heartbeats_after, heartbeats_before,
                      heartbeat.get("wake_cycle"),
                      heartbeat.get("fail_streak")))
    finally:
        handle.set_phase("restore")
        wait_reachable(180)
        durations = kernel_suspend_durations(notes)
        restored = True
        if not args.keep_config:
            restored = restore_config(original)
            if not restored:
                notes.append("Не удалось полностью восстановить конфиг "
                             "[sleep]/[http_post]/аналитику.")
        summary = handle.stop()
        events.write("logger", **{key: summary.get(key) for key in
                                  ("duration_s", "rows", "samples",
                                   "errors", "missed_polls")})
        extras = {"notes": notes}
        tables = []
        if args.post_checks:
            tables += post_checks(outdir, notes)
        rows = read_rows(summary["csv"]) if summary.get("csv") else []
        soak_rows = [row for row in rows
                     if row.get("phase") in ("awake", "offline",
                                             "probe", "suspending")]
        floor = floor_stats(soak_rows, args.channel)
        if floor:
            tables.append(("Полка по распределению тока", floor_table(
                floor, args.channel)))
            notes.append("Полка: минимум %.1f мА, p05 %.1f мА, p10 %.1f мА, "
                         "ниже порога %.1f мА — %.1f %% отсчётов." %
                         (floor["min_ma"], floor["p05_ma"], floor["p10_ma"],
                          floor["threshold_ma"], floor["fraction"]))
        if durations:
            table, stats = suspend_table(durations)
            tables.append(("Resume-путь suspend entry→exit (dmesg, без сна)", table))
            notes.append("dmesg: последние %d окон entry→exit (resume-путь), среднее "
                         "%.2f с (кольцевой буфер)." %
                         (stats["count"], stats["mean_s"]))
        else:
            notes.append("Deep suspend не подтверждён dmesg: полка "
                         "недействительна.")
        starts = suspend_starts(soak_rows, args.channel,
                                floor.get("median_ma") if floor else None)
        energies = []
        if len(starts) >= 2:
            table = ["| Цикл | Начало | Секунд | мВт·ч | мА·ч |",
                     "|---|---|---|---|---|"]
            for index, (first, second) in enumerate(
                    zip(starts, starts[1:]), 1):
                item = integrate(soak_rows, args.channel, first, second)
                energies.append(item["energy_mwh"])
                table.append("| %d | %s | %.1f | %.2f | %.2f |" %
                             (index,
                              datetime.fromtimestamp(first / 1000.0)
                              .strftime("%H:%M:%S"),
                              item["seconds"], item["energy_mwh"],
                              item["charge_mah"]))
            mean = sum(energies) / len(energies)
            sigma = (sum((value - mean) ** 2 for value in energies) /
                     len(energies)) ** 0.5
            ordered = sorted(energies)
            median = ordered[len(ordered) // 2]
            notes.append("Энергия на цикл: медиана %.2f мВт·ч, среднее "
                         "%.2f (σ %.2f, n=%d; границы — локальные минимумы "
                         "тока)." % (median, mean, sigma, len(energies)))
            if len(table) > 32:
                head = table[:12]
                tail = table[-12:]
                table = head + ["| … | … | … | … | … |"] + tail
            tables.append(("Энергия на цикл (первые и последние)", table))
        else:
            notes.append("Полных циклов меньше двух: энергия на цикл не "
                         "рассчитана.")
        if tables:
            extras["tables"] = tables
        write_report(outdir / "report.md", "soak", args, summary, extras)
        result = {"summary": summary, "original": original,
                  "cycles": len(energies), "cycle_energy_mwh": energies,
                  "durations": durations, "restored": restored,
                  "floor_valid": bool(durations)}
    return result


def scenario_aov(args, outdir, events):
    """MCU/AOV wake proxy and interval wake cadence."""
    handle = start_logger(args, outdir, "setup")
    original = {}
    notes = []
    transitions = []
    aov_counts = {}
    focus_counts = {}
    pads_log = []
    lost_at = None
    start_probe = {}
    try:
        try:
            client = ssh_client()
            try:
                start_probe = stm8_probe(client)
            finally:
                client.close()
        except Exception as exc:                            # noqa: BLE001
            start_probe = {"present": None, "error": str(exc)}
            notes.append("Стартовая проба STM8 не выполнена: %s" % exc)
        events.write("stm8-start", **start_probe)
        if start_probe.get("present") is False:
            lost_at = now_ms()
            notes.append("STM8 не отвечает уже на старте прогона (0x60 нет "
                         "в i2cdetect).")
        original = snapshot_config()
        apply_sleep({
            "enable": "true", "pmu_timer_wake": "true",
            "aov_mcu": "true" if args.aov_mcu else "false",
            "aov_keep_isp": "true" if args.aov_keep_isp else "false",
            "idle_timeout_s": str(args.idle),
            "wake_interval_s": str(args.wake_interval),
            "wake_check_s": str(args.aov_wake_check),
            "heartbeat": "true" if args.heartbeat_seconds > 0 else "false",
            "heartbeat_interval_s": str(args.heartbeat_seconds),
            "usb_on_motion": "true" if args.usb_on_motion else "false"})
        events.write("setup", wake_interval=args.wake_interval,
                     wake_check=args.aov_wake_check,
                     aov_mcu=args.aov_mcu,
                     aov_keep_isp=args.aov_keep_isp,
                     heartbeat_seconds=args.heartbeat_seconds,
                     usb_on_motion=args.usb_on_motion,
                     seconds=args.aov_seconds)
        handle.set_phase("await-suspend")
        if wait_until_state(DOWN_STATES, 180) is None:
            raise RuntimeError("camera did not enter suspend")
        deadline = time.time() + args.aov_seconds
        last = None
        while time.time() < deadline:
            snap = sleep_state(timeout=2)
            if snap is None:
                state = "unreachable"
            else:
                state = snap.get("state") or "unknown"
            phase = "suspend" if state in DOWN_STATES else \
                ("awake" if state in UP_STATES else state)
            if phase != last:
                ts = now_ms()
                handle.set_phase(phase)
                events.write("state", state=state, phase=phase, ts=ts)
                transitions.append((ts, state, phase))
                if phase == "awake" and len(aov_counts) < 40:
                    aov = {"aov_state": (snap or {}).get("aov_state"),
                           "last_wake_src": (snap or {}).get("last_wake_src"),
                           "heartbeat_wakes":
                               (snap or {}).get("heartbeat_wakes"),
                           "timeout_streak":
                               (snap or {}).get("timeout_streak")}
                    key = str(aov.get("aov_state"))
                    aov_counts[key] = aov_counts.get(key, 0) + 1
                    events.write("aov", ts=ts, **aov)
                    focus = focus_probe()
                    focus_key = "%s/%s" % (focus.get("present"),
                                           focus.get("version"))
                    focus_counts[focus_key] = \
                        focus_counts.get(focus_key, 0) + 1
                    events.write("focus", ts=ts, **focus)
                    try:
                        pads = aov_i2c_diag.quick_pads()
                        events.write("pads", ts=ts, **pads)
                        pads_log.append(pads)
                    except Exception as exc:                # noqa: BLE001
                        events.write("pads-error", ts=ts, error=str(exc))
                    if focus.get("present") is False and lost_at is None:
                        lost_at = ts
                        notes.append("STM8 пропал на пробуждении в %s "
                                     "(t=%.1f с от старта)." %
                                     (datetime.fromtimestamp(ts / 1000.0)
                                      .strftime("%H:%M:%S"),
                                      (ts - transitions[0][0]) / 1000.0))
                last = phase
            time.sleep(0.5)
        handle.set_phase("done")
        time.sleep(2)
    finally:
        handle.set_phase("restore")
        wait_reachable(180)
        restored = True
        if not args.keep_config:
            restored = restore_config(original)
            if not restored:
                notes.append("Не удалось полностью восстановить конфиг "
                             "[sleep]/[http_post]/аналитику.")
        summary = handle.stop()
        events.write("logger", **{key: summary.get(key) for key in
                                  ("duration_s", "rows", "samples",
                                   "errors", "missed_polls")})
        extras = {"notes": notes}
        tables = []
        rows = read_rows(summary["csv"]) if summary.get("csv") else []
        wake_times = [ts for ts, _state, phase in transitions
                      if phase == "awake"]
        intervals = [(second - first) / 1000.0
                     for first, second in zip(wake_times, wake_times[1:])]
        if intervals:
            ordered = sorted(intervals)
            notes.append("Интервалы пробуждений: медиана %.1f с, min %.1f, "
                         "max %.1f, n=%d (задано %d с)." %
                         (ordered[len(ordered) // 2], ordered[0],
                          ordered[-1], len(intervals),
                          args.wake_interval))
        else:
            notes.append("Пробуждений не зафиксировано за %.0f с." %
                         args.aov_seconds)
        if aov_counts:
            notes.append("aov_state на пробуждениях: %s." %
                         ", ".join("%s=%d" % item
                                   for item in sorted(aov_counts.items())))
        if focus_counts:
            notes.append("GET /api/focus на пробуждениях (present/version): "
                         "%s." % ", ".join("%s=%d" % item for item in
                                           sorted(focus_counts.items())))
        if pads_log:
            modes = {}
            for pads in pads_log:
                key = pads.get("iomux_mode")
                modes[key] = modes.get(key, 0) + 1
            notes.append("I2C3 iomux/pads на пробуждениях: %s." % ", ".join(
                "%s=%d" % item for item in sorted(
                    modes.items(), key=lambda item: str(item[0]))))
            partial = [pads for pads in pads_log
                       if pads.get("iomux_mode") == "partial"]
            if partial:
                notes.append("iomux сброшен частично: SCL(D3)=%s, SDA(D2)=%s "
                             "— пад I2C3 управляется GPIO-блоком." %
                             (partial[0].get("iomux_scl"),
                              partial[0].get("iomux_sda")))
        if lost_at is not None:
            notes.append("STM8 пропал во время прогона: motor.present=false "
                         "(ts=%d)." % lost_at)
        if args.aov_mcu and aov_counts.get("timeout"):
            notes.append("aov_state=timeout на полном резюме: MCU не ответил "
                         "серию mailbox-таймаутов, divinus должен снять "
                         "aov_mcu и вернуть интервал 60 с.")
        if args.aov_mcu and aov_counts.get("heartbeat"):
            wakes = (args.heartbeat_seconds // args.wake_interval
                     if args.wake_interval else 0)
            notes.append("Tier 2: полный резюм по aov_state=heartbeat "
                         "(heartbeat_interval_s=%d, wake_interval_s=%d, "
                         "N=%d тихих циклов)." %
                         (args.heartbeat_seconds, args.wake_interval, wakes))
        if args.aov_mcu and (aov_counts.get("quiet") or
                             aov_counts.get("motion")):
            notes.append("MCU ответил по mailbox (quiet/motion): "
                         "MCU-детекция работает на уровне handshake.")
        if args.aov_mcu and not args.heartbeat_seconds and \
                not aov_counts.get("quiet") and not aov_counts.get("motion"):
            notes.append("aov_state на резюме не quiet/motion: проверьте "
                         "handshake MCU по маркерам -h-* в UART.")
        table = ["| Интервал | Начало | Секунд | мВт·ч | мА·ч |",
                 "|---|---|---|---|---|"]
        energy = []
        for index, (first, second) in enumerate(
                zip(wake_times, wake_times[1:]), 1):
            item = integrate(rows, args.channel, first, second)
            energy.append(item["energy_mwh"])
            table.append("| %d | %s | %.1f | %.2f | %.2f |" %
                         (index,
                          datetime.fromtimestamp(first / 1000.0)
                          .strftime("%H:%M:%S"),
                          item["seconds"], item["energy_mwh"],
                          item["charge_mah"]))
        if energy:
            mean = sum(energy) / len(energy)
            notes.append("Энергия интервала: %.2f мВт·ч (n=%d)." %
                         (mean, len(energy)))
            if len(table) > 20:
                table = table[:10] + ["| … | … | … | … | … |"] + table[-10:]
            tables.append(("Интервалы пробуждения", table))
        durations = kernel_suspend_durations(notes)
        last_sleep = last_suspend_sleep(notes)
        if durations:
            lines, stats = suspend_table(durations)
            tables.append(("Resume-путь suspend entry→exit (dmesg, без сна)", lines))
            notes.append("dmesg: последние %d окон entry→exit (resume-путь), среднее "
                         "%.2f с." % (stats["count"], stats["mean_s"]))
        if last_sleep:
            tables.append(("Фактический сон последнего цикла (sysfs)",
                           sleep_table(last_sleep)))
            notes.append("Сон последнего цикла: %.2f с (resume-путь %.2f с)." %
                         (last_sleep["sleep_s"], last_sleep["resume_s"]))
        profile = kernel_pm_profile(notes)
        if profile:
            tables.append(("oko-pm: фазы suspend/resume (debug-ядро)",
                           profile_table(profile)))
            notes.append("oko-pm: снято %d строк профиля фаз." % len(profile))
        summary["full_resumes"] = len(wake_times)
        summary["heartbeat_interval_s"] = args.heartbeat_seconds
        summary["aov_state_counts"] = dict(aov_counts)
        suspend_rows = [row for row in rows
                        if row.get("phase") in ("suspend", "await-suspend")]
        floor = floor_stats(suspend_rows, args.channel)
        if floor:
            tables.append(("Полка по распределению тока", floor_table(
                floor, args.channel)))
        if tables:
            extras["tables"] = tables
        write_report(outdir / "report.md", "aov", args, summary, extras)
        result = {"summary": summary, "original": original,
                  "restored": restored, "aov_counts": aov_counts,
                  "focus_counts": focus_counts,
                  "pads_wakes": pads_log,
                  "stm8_start": start_probe,
                  "stm8_lost": lost_at is not None,
                  "stm8_lost_at": lost_at,
                  "wake_intervals_s": intervals, "durations": durations,
                  "last_sleep": last_sleep}
    return result


def scenario_i2c(args, outdir, events):
    """T1.1 control: STM8 I2C traffic with the suspend path never entered."""
    handle = start_logger(args, outdir, "i2c")
    notes = []
    client = None
    before = {}
    focus_before = {}
    failures = None
    try:
        client = ssh_client()
        before = stm8_probe(client)
        events.write("stm8-start", **before)
        if before.get("present") is False:
            raise RuntimeError("STM8 is already missing before the control run")
        focus_before = focus_snapshot()
        loop = (
            "n=0; fail=0; total=%d; step=%d;"
            "while [ $n -lt $total ]; do "
            "/oem/usr/bin/oko-motor sleep show >/dev/null 2>&1 || "
            "fail=$((fail+1));"
            "if [ $((n %% 2)) -eq 0 ]; then d=$step; else d=-$step; fi;"
            "/oem/usr/bin/oko-motor move zoom $d >/dev/null 2>&1 || "
            "fail=$((fail+1));"
            "n=$((n+1));"
            "if [ $((n %% 25)) -eq 0 ]; then echo \"PROGRESS $n $fail\"; fi;"
            "done; echo \"RESULT $n $fail\""
        ) % (args.i2c_cycles, args.i2c_step)
        handle.set_phase("i2c")
        code, out, err = ssh_run(
            client, loop, timeout=max(180, args.i2c_cycles * 3))
        events.write("i2c-loop", code=code, error=err.strip()[-500:])
        for line in out.splitlines():
            if line.startswith("PROGRESS"):
                events.write("progress", line=line.strip())
            elif line.startswith("RESULT"):
                parts = line.split()
                if len(parts) >= 3:
                    failures = int(parts[2])
        if failures is None:
            raise RuntimeError("I2C loop produced no RESULT line: %s" %
                               (out + err).strip()[-500:])
        notes.append("I2C-циклов: %d, ошибок команд: %d (без suspend)." %
                     (args.i2c_cycles, failures))
    finally:
        handle.set_phase("done")
        time.sleep(1)
        after = {}
        try:
            if client is None:
                client = ssh_client()
            after = stm8_probe(client)
        except Exception as exc:                            # noqa: BLE001
            notes.append("Финальная проба STM8 не выполнена: %s" % exc)
        finally:
            if client is not None:
                try:
                    client.close()
                except Exception:                           # noqa: BLE001
                    pass
        if focus_before:
            restore_focus(focus_before, notes)
        summary = handle.stop()
        events.write("stm8-end", **after)
        summary_note = "STM8 после %d циклов: present=%s." % (
            args.i2c_cycles, after.get("present"))
        notes.append(summary_note)
        write_report(outdir / "report.md", "i2c", args, summary,
                     {"notes": notes})
        result = {"summary": summary, "failures": failures,
                  "stm8_start": before, "stm8_end": after,
                  "stm8_lost": after.get("present") is False}
    return result


def scenario_stm8(args, outdir, events):
    handle = start_logger(args, outdir, "preflight")
    notes = []
    client = None
    divinus_stopped = False
    stop_attempted = False
    test_ok = False
    crosscheck = []
    try:
        client = ssh_client()
        code, out, err = ssh_run(
            client, "ls -l /userdata/stm8/tests/hardware_test.sh "
                    "/userdata/stm8/util/motorctl")
        events.write("preflight", code=code, output=out.strip(),
                     error=err.strip())
        if code != 0:
            raise RuntimeError("hardware_test.sh/motorctl not found: %s" %
                               (out + err).strip())
        handle.set_phase("stm8-base")
        events.write("phase", phase="stm8-base")
        for _ in range(args.stm8_base_seconds):
            sample_onboard(events, crosscheck, "stm8-base")
            time.sleep(2)
        handle.set_phase("stm8-stop")
        stop_attempted = True
        divinus_stopped = False
        for _ in range(15):
            code, out, err = ssh_run(client, "pidof divinus")
            if code != 0 or not out.strip():
                divinus_stopped = True
                break
            ssh_run(client, "killall divinus")
            time.sleep(1)
        if not divinus_stopped:
            ssh_run(client, "killall -9 divinus")
            time.sleep(1)
            code, out, err = ssh_run(client, "pidof divinus")
            divinus_stopped = code != 0 or not out.strip()
        events.write("divinus-stop", stopped=divinus_stopped,
                     pid=out.strip())
        if not divinus_stopped:
            raise RuntimeError("divinus is still running")
        command = ("cd /userdata/stm8 && BUS=3 ADDR=0x60 FW_VERSION=%s "
                   "SLEEP_SOAK_S=%d ./tests/hardware_test.sh" %
                   (args.stm8_fw, args.stm8_soak))
        handle.set_phase("stm8-soak")
        events.write("phase", phase="stm8-soak", soak_s=args.stm8_soak)
        log_path = outdir / "stm8-test.log"
        sampler = ssh_client()
        try:
            transport = client.get_transport()
            channel = transport.open_session()
            channel.settimeout(3600)
            channel.exec_command(command)
            last_sample = 0
            with open(log_path, "w") as stream:
                while True:
                    if channel.recv_ready():
                        chunk = channel.recv(4096).decode(errors="replace")
                        stream.write(chunk)
                        stream.flush()
                    if time.time() - last_sample >= args.crosscheck_seconds:
                        sample_onboard(events, crosscheck, "stm8-soak", sampler)
                        last_sample = time.time()
                    if channel.exit_status_ready() and not channel.recv_ready():
                        while channel.recv_ready():
                            stream.write(channel.recv(4096).decode(
                                errors="replace"))
                        break
                    time.sleep(0.5)
            test_ok = channel.recv_exit_status() == 0
        finally:
            sampler.close()
        text = log_path.read_text(errors="replace")
        total_pass = re.search(r"^PASS:\s*(\d+)", text, re.MULTILINE)
        total_fail = re.search(r"^FAIL:\s*(\d+)", text, re.MULTILINE)
        if total_pass and total_fail:
            passes, fails = int(total_pass.group(1)), int(total_fail.group(1))
        else:
            passes = len(re.findall(r"^\s+PASS:", text, re.MULTILINE))
            fails = len(re.findall(r"^\s+FAIL:", text, re.MULTILINE))
        notes.append("T8: %d PASS / %d FAIL, soak %d с." %
                     (passes, fails, args.stm8_soak))
        if not test_ok:
            notes.append("hardware_test.sh завершился с ошибкой.")
        code, out, err = ssh_run(
            client, "dmesg | grep -iE 'SCL hold|sfc.*(err|time)' || true")
        notes.append("dmesg STM8/I2C: %s" %
                     (out.strip() or "ошибок нет"))
    finally:
        if stop_attempted and not args.no_restart:
            try:
                code, out, _err = ssh_run(client, "pidof divinus")
                if code != 0 or not out.strip():
                    ssh_run(client, "cd /userdata && HOME=/oem "
                                    "LD_LIBRARY_PATH=/oem/usr/lib:/oem/lib "
                                    "setsid /oem/usr/bin/divinus "
                                    "</dev/null >/dev/null 2>&1 &")
                if wait_reachable(120) is None:
                    notes.append("divinus не поднялся после T8.")
                else:
                    notes.append("divinus поднят после T8.")
            except Exception as exc:                        # noqa: BLE001
                notes.append("Не удалось поднять divinus: %s" % exc)
        if client is not None:
            client.close()
        summary = handle.stop()
        events.write("logger", **{key: summary.get(key) for key in
                                  ("duration_s", "rows", "samples",
                                   "errors", "missed_polls")})
        tables = []
        if crosscheck:
            esp_rows = read_rows(summary["csv"]) if summary.get("csv") else []
            esp_rows = [row for row in esp_rows
                        if row.get("phase") == "stm8-soak"]
            tables.append(("Сверка ESP32 CH3 с бортовым INA3221 "
                           "(фаза stm8-soak)",
                           crosscheck_table(crosscheck, esp_rows,
                                            args.channel)))
        write_report(outdir / "report.md", "stm8", args, summary,
                     {"notes": notes, "tables": tables})
        result = {"summary": summary, "crosscheck": crosscheck}
    return result


IVS_KEYS = ["enable", "mode", "width", "height", "sensitivity",
            "md_interval", "od_interval", "od_percent", "min_area"]


def ivs_snapshot():
    status, dec = dut("/api/ivs")
    return dec if status == 200 else {}


def restore_ivs(snapshot, notes):
    if not snapshot:
        return
    values = {}
    for key in IVS_KEYS:
        value = snapshot.get(key)
        if value is None:
            continue
        values[key] = ("true" if value else "false") \
            if isinstance(value, bool) else str(value)
    status, dec = post_retry("/api/ivs", values)
    if status != 200:
        notes.append("Не удалось восстановить IVS: HTTP %s %s" %
                     (status, dec))


def focus_snapshot():
    status, dec = dut("/api/focus")
    if status != 200:
        return {}
    motor = dec.get("motor") or {}
    return {"position": (motor.get("focus") or {}).get("position"),
            "zoom_position": (motor.get("zoom") or {}).get("position"),
            "motor": motor.get("enabled"),
            "enable": dec.get("enable")}


def restore_focus(snapshot, notes):
    if not snapshot:
        return
    for scale, key in (("focus", "position"), ("zoom", "zoom_position")):
        position = snapshot.get(key)
        if position is None:
            continue
        status, dec = post_retry(
            "/api/focus", {"motor": "1", "scale": scale,
                           "position": str(position)})
        if status != 200:
            notes.append("Не удалось вернуть позицию %s %s: HTTP %s" %
                         (scale, position, status))
        time.sleep(1.2)
    if not snapshot.get("motor"):
        post_retry("/api/focus", {"motor": "0"})
    if not snapshot.get("enable"):
        post_retry("/api/focus", {"enable": "0"})


def recent_ivs_event(min_area, max_age_s=120):
    dec = {}
    for _attempt in range(3):
        status, dec = dut("/api/ivs", timeout=5)
        if status == 200:
            break
        time.sleep(0.5)
    events = (dec.get("events") or []) if status == 200 else []
    if not events:
        return None
    last = events[-1]
    try:
        if int(last.get("area", 0)) < min_area:
            return None
        stamp = datetime.strptime(last.get("time", ""),
                                  "%Y-%m-%dT%H:%M:%SZ").replace(
                                      tzinfo=timezone.utc).timestamp()
        if abs(time.time() - stamp) > max_age_s:
            return None
    except (TypeError, ValueError):
        return None
    return last


def ivs_event_since(since_ms, min_area):
    status, dec = dut("/api/ivs", timeout=5)
    events = (dec.get("events") or []) if status == 200 else []
    for event in reversed(events):
        try:
            if int(event.get("area", 0)) < min_area:
                continue
            stamp = datetime.strptime(event.get("time", ""),
                                      "%Y-%m-%dT%H:%M:%SZ").replace(
                                          tzinfo=timezone.utc).timestamp()
        except (TypeError, ValueError):
            continue
        if int(stamp * 1000) >= since_ms:
            return event
    return None


def latest_ivs_event(before, min_area):
    status, dec = dut("/api/ivs")
    events = (dec.get("events") or []) if status == 200 else []
    if not events:
        return None
    last = events[-1]
    if before and last.get("time") == before.get("time") and \
            last.get("frame") == before.get("frame"):
        return None
    try:
        area = int(last.get("area", 0))
    except (TypeError, ValueError):
        return None
    return last if area >= min_area else None


DOWN_STATES = ("unreachable", "suspending", "suspended")
UP_STATES = ("probe", "awake")


def wait_until_state(states, timeout, poll=0.5):
    deadline = time.time() + timeout
    while time.time() < deadline:
        state = live_state(timeout=3)
        if state in states:
            return state
        time.sleep(poll)
    return None


def scenario_motion(args, outdir, events):
    """Sleep with IVS enabled: measure the real wake/awake duty cycle."""
    handle = start_logger(args, outdir, "setup")
    original = {}
    ivs_before = {}
    focus_before = {}
    notes = []
    cycles = []
    motion_event = None
    try:
        original = snapshot_config()
        ivs_before = ivs_snapshot()
        focus_before = focus_snapshot()
        status, dec = post_retry("/api/ivs", {
            "enable": "true", "min_area": str(args.motion_min_area),
            "sensitivity": str(args.motion_sensitivity)})
        if status != 200 or dec.get("error"):
            raise RuntimeError("IVS did not enable: HTTP %s %s" %
                               (status, dec))
        apply_sleep({
            "enable": "true", "pmu_timer_wake": "true",
            "aov_mcu": "false", "aov_keep_isp": "false",
            "idle_timeout_s": str(args.idle),
            "wake_interval_s": str(args.wake_interval),
            "wake_check_s": str(args.motion_wake_check),
            "heartbeat": "false"})
        events.write("setup", idle=args.idle,
                     wake_interval=args.wake_interval,
                     wake_check=args.motion_wake_check,
                     min_area=args.motion_min_area,
                     cycles=args.motion_cycles,
                     ivs_enable=(ivs_snapshot() or {}).get("enable"))
        for index in range(1, args.motion_cycles + 1):
            handle.set_phase("suspend-%d" % index)
            if wait_until_state(DOWN_STATES, args.motion_timeout) is None:
                raise RuntimeError("no suspend in cycle %d" % index)
            down = now_ms()
            state = wait_until_state(UP_STATES, args.motion_timeout)
            if state is None:
                raise RuntimeError("no wake in cycle %d" % index)
            up = now_ms()
            handle.set_phase("awake-%d" % index)
            events.write("cycle-up", cycle=index, state=state, ts=up,
                         since_suspend_s=round((up - down) / 1000.0, 1))
            ivs_state = ivs_snapshot() or {}
            events.write("ivs-at-wake", cycle=index,
                         enable=ivs_state.get("enable"),
                         events=len(ivs_state.get("events") or []),
                         min_area=ivs_state.get("min_area"))
            wake_event = recent_ivs_event(args.motion_min_area)
            if wake_event and motion_event is None:
                motion_event = wake_event
                events.write("motion-detected", **wake_event,
                             cycle=index, trigger="wake")
                notes.append("IVS сработал на пробуждении цикла %d: "
                             "area %s‰, cause %s." %
                             (index, wake_event.get("area"),
                              wake_event.get("cause")))
            if index == 1:
                time.sleep(2)
                wake_event = ivs_event_since(up - 5000,
                                             args.motion_min_area)
                if wake_event is None:
                    wake_event = recent_ivs_event(args.motion_min_area)
                if wake_event and motion_event is None:
                    motion_event = wake_event
                    events.write("motion-detected", **wake_event,
                                 cycle=index, trigger="wake-before-move")
                before = {}
                ivs_now = ivs_snapshot()
                if ivs_now.get("events"):
                    before = ivs_now["events"][-1]
                start_position = focus_before.get("zoom_position") \
                    if args.motion_scale == "zoom" else \
                    focus_before.get("position")
                direction = 1
                if start_position is not None and \
                        start_position > args.motion_steps:
                    direction = -1
                steps = direction * args.motion_steps
                status, dec = post_retry("/api/focus", {
                    "motor": "1", "scale": args.motion_scale,
                    "speed": str(args.motion_speed), "move": str(steps)})
                events.write("motion", cycle=index,
                             source=args.motion_scale, status=status,
                             steps=steps, speed=args.motion_speed,
                             position=start_position)
                if status != 200 or dec.get("error"):
                    notes.append("Мотор %s не отработал: HTTP %s %s" %
                                 (args.motion_scale, status, dec))
                moved_at = time.time()
                for _ in range(12):
                    found = latest_ivs_event(before, args.motion_min_area)
                    if found:
                        motion_event = found
                        events.write("motion-detected", **found, cycle=index)
                        notes.append("IVS: area %s‰, cause %s, bbox %s." %
                                     (found.get("area"), found.get("cause"),
                                      found.get("bbox")))
                        break
                    time.sleep(0.5)
                post_retry("/api/focus", {
                    "scale": args.motion_scale,
                    "speed": str(args.motion_speed), "move": str(-steps)})
                events.write("motion-end", cycle=index,
                             after_s=round(time.time() - moved_at, 1))
            if wait_until_state(DOWN_STATES, args.motion_timeout) is None:
                notes.append("Цикл %d не вернулся в сон за %.0f с." %
                             (index, args.motion_timeout))
                break
            after = now_ms()
            end_event = recent_ivs_event(args.motion_min_area, 60)
            if end_event and motion_event is None:
                motion_event = end_event
            cycles.append({"index": index, "down": down, "up": up,
                           "after": after, "state": state,
                           "motion": end_event})
            events.write("cycle-down", cycle=index, ts=after,
                         awake_s=round((after - up) / 1000.0, 1),
                         cycle_s=round((after - down) / 1000.0, 1))
        handle.set_phase("done")
        time.sleep(2)
        if not cycles:
            notes.append("Ни одного цикла не измерено.")
    finally:
        handle.set_phase("restore")
        wait_reachable(180)
        restore_focus(focus_before, notes)
        restore_ivs(ivs_before, notes)
        restored = True
        if not args.keep_config:
            restored = restore_config(original)
            if not restored:
                notes.append("Не удалось полностью восстановить конфиг "
                             "[sleep]/[http_post]/аналитику.")
        summary = handle.stop()
        events.write("logger", **{key: summary.get(key) for key in
                                  ("duration_s", "rows", "samples",
                                   "errors", "missed_polls")})
        extras = {"notes": notes}
        tables = []
        rows = read_rows(summary["csv"]) if summary.get("csv") else []
        table = ["| Цикл | Сон, с | Бодрств., с | Сон, мВт·ч | Бодрств., "
                 "мВт·ч | Цикл, мВт·ч | Среднее, мВт |",
                 "|---|---|---|---|---|---|---|"]
        cycle_energy = []
        for item in cycles:
            suspend = integrate(rows, args.channel, item["down"], item["up"])
            awake = integrate(rows, args.channel, item["up"], item["after"])
            whole = integrate(rows, args.channel, item["down"],
                              item["after"])
            cycle_energy.append(whole["energy_mwh"])
            table.append("| %d | %.1f | %.1f | %.2f | %.2f | %.2f | %.0f |" %
                         (item["index"], suspend["seconds"],
                          awake["seconds"], suspend["energy_mwh"],
                          awake["energy_mwh"], whole["energy_mwh"],
                          whole.get("mean_mw", 0)))
        if cycles:
            tables.append(("Циклы сна с IVS", table))
            awake_cycles = sum(1 for item in cycles
                               if item.get("state") == "awake")
            if awake_cycles == len(cycles):
                notes.append("IVS удерживает камеру в awake во всех %d "
                             "циклах: движение фиксируется на каждом "
                             "пробуждении." % len(cycles))
            if cycle_energy:
                mean = sum(cycle_energy) / len(cycle_energy)
                notes.append("Энергия цикла с IVS: %.2f мВт·ч (n=%d); "
                             "тихий цикл без IVS 2026-11-22 — 4.9 мВт·ч "
                             "за ≈13 с." % (mean, len(cycle_energy)))
        if motion_event:
            tables.append(("Событие IVS", [
                "| Время | Кадр | Площадь, ‰ | Причина | BBox |",
                "|---|---|---|---|---|",
                "| %s | %s | %s | %s | %s |" %
                (motion_event.get("time"), motion_event.get("frame"),
                 motion_event.get("area"), motion_event.get("cause"),
                 motion_event.get("bbox"))]))
        durations = kernel_suspend_durations(notes)
        if durations:
            suspend_table_lines, stats = suspend_table(durations)
            tables.append(("Resume-путь suspend entry→exit (dmesg, без сна)",
                           suspend_table_lines))
            notes.append("dmesg: последние %d окон entry→exit (resume-путь), среднее "
                         "%.2f с (кольцевой буфер)." %
                         (stats["count"], stats["mean_s"]))
        suspend_rows = [row for row in rows
                        if row.get("phase", "").startswith("suspend-")]
        floor = floor_stats(suspend_rows, args.channel)
        if floor:
            tables.append(("Полка по распределению тока", floor_table(
                floor, args.channel)))
        if tables:
            extras["tables"] = tables
        write_report(outdir / "report.md", "motion", args, summary, extras)
        result = {"summary": summary, "original": original,
                  "motion": motion_event, "cycles": cycles,
                  "cycle_energy_mwh": cycle_energy, "restored": restored,
                  "durations": durations}
    return result


def sample_onboard(events, rows, phase, client=None):
    own = client is None
    if own:
        client = ssh_client()
    try:
        for channel in (1, 2, 3):
            code, out, _err = ssh_run(
                client, "/oem/usr/bin/oko-ec --json --ina-channel %d ina226"
                % channel)
            for line in out.splitlines():
                line = line.strip()
                if not line.startswith("{"):
                    continue
                try:
                    item = json.loads(line).get("ina226") or {}
                except ValueError:
                    continue
                rows.append({"ts": now_ms(), "ch": channel,
                             "phase": phase,
                             "bus_mv": item.get("bus_mv"),
                             "cur_ma": item.get("current_ma"),
                             "power_mw": item.get("power_mw")})
                events.write("onboard", channel=channel, phase=phase,
                             bus_mv=item.get("bus_mv"),
                             cur_ma=item.get("current_ma"),
                             power_mw=item.get("power_mw"))
    finally:
        if own:
            client.close()


def crosscheck_table(rows, esp_rows, esp_channel):
    lines = ["| Канал | Шина, мВ | Ток, мА | Мощность, мВт |",
             "|---|---|---|---|"]
    onboard_cur = onboard_pwr = 0.0
    for channel in (1, 2, 3):
        items = [row for row in rows if row["ch"] == channel and
                 row.get("phase") == "stm8-soak"]
        if not items:
            continue
        cur = sum(row["cur_ma"] or 0 for row in items) / len(items)
        pwr = sum(row["power_mw"] or 0 for row in items) / len(items)
        bus = sum(row["bus_mv"] or 0 for row in items) / len(items)
        onboard_cur += cur
        onboard_pwr += pwr
        lines.append("| бортовой CH%d | %.0f | %.1f | %.1f |" %
                     (channel, bus, cur, pwr))
    esp = [row for row in esp_rows if row["ch"] == esp_channel]
    if esp:
        cur = sum(row["cur"] for row in esp) / len(esp)
        pwr = sum(row["pwr"] for row in esp) / len(esp)
        lines.append("| ESP32 CH%d (вход DUT) | — | %.1f | %.1f |" %
                     (esp_channel, cur, pwr))
        if onboard_cur:
            lines.append("| расхождение тока | — | %.1f %% | — |" %
                         ((cur - onboard_cur) / onboard_cur * 100.0))
        if onboard_pwr:
            lines.append("| расхождение мощности | — | — | %.1f %% |" %
                         ((pwr - onboard_pwr) / onboard_pwr * 100.0))
    lines.append("| сумма бортовых | — | %.1f | %.1f |" %
                 (onboard_cur, onboard_pwr))
    return lines


def post_checks(outdir, notes):
    tables = []
    for script in ("api_check.py", "ui_check.py"):
        path = ROOT / "bench" / script
        log_path = outdir / ("post-" + script + ".log")
        try:
            result = subprocess.run([sys.executable, str(path)],
                                    cwd=str(ROOT), capture_output=True,
                                    text=True, timeout=3600)
            log_path.write_text(result.stdout + result.stderr)
            notes.append("%s после soak: код %d (лог %s)." %
                         (script, result.returncode, log_path.name))
        except (OSError, subprocess.TimeoutExpired) as exc:
            notes.append("%s после soak не выполнен: %s" % (script, exc))
    try:
        client = ssh_client()
        try:
            code, dmesg_out, err = ssh_run(
                client, "dmesg | grep -iE 'sfc.*(err|time)|ubi.*err' || true")
            notes.append("dmesg KI-071: %s" %
                         (dmesg_out.strip() or "ошибок нет"))
            code, fd_out, err = ssh_run(
                client, "ls /proc/$(pidof divinus)/fd | wc -l")
            try:
                count = int(fd_out.strip().splitlines()[-1])
            except (ValueError, IndexError):
                count = None
            notes.append("fd divinus: %s; ожидание %d–%d измерено с "
                         "активным потоком/IVS, после восстановления "
                         "конфига они выключены." % (count, FD_MIN, FD_MAX))
            tables.append(("Пост-проверки", [
                "| Проверка | Значение |", "|---|---|",
                "| dmesg sfc/ubi | %s |" % (dmesg_out.strip() or "чисто"),
                "| fd divinus | %s |" % count]))
        finally:
            client.close()
    except Exception as exc:                                # noqa: BLE001
        notes.append("SSH-пост-проверки не выполнены: %s" % exc)
    return tables


# --------------------------------------------------------------------------
# Common helpers


def wait_service(timeout=60):
    deadline = time.time() + timeout
    while time.time() < deadline:
        status, body = request(SERVICE + "/health")
        if status == 200:
            try:
                if json.loads(body.decode()).get("ok"):
                    return True
            except ValueError:
                pass
        time.sleep(1)
    return False


def heartbeat_count(since_ms=None):
    status, body = request(SERVICE + "/api/heartbeats?limit=1000")
    if status != 200:
        return None
    try:
        items = json.loads(body.decode()).get("heartbeats", [])
    except ValueError:
        return None
    if since_ms is None:
        return len(items)
    return len([item for item in items
                if int(item.get("ts", 0)) >= since_ms])


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--scenario", required=True,
                        choices=["levels", "cycle", "motion", "aov", "soak",
                                 "peaks", "stm8", "i2c"])
    parser.add_argument("--channel", type=int, default=3,
                        help="INA3221 channel of the DUT input (default 3)")
    parser.add_argument("--hz", type=float, default=None,
                        help="poll rate (default 2, peaks 8)")
    parser.add_argument("--no-power-logger", action="store_true",
                        help="do not open a bridge INA session (DUT-only run)")
    parser.add_argument("--bridge", default=BRIDGE)
    parser.add_argument("--cookie-file", default=None,
                        help="reuse this bridge session for the logger")
    parser.add_argument("--out-dir", default=None)
    parser.add_argument("--keep-config", action="store_true",
                        help="do not restore [sleep]/[http_post]/analytics")
    parser.add_argument("--idle", type=int, default=30)
    parser.add_argument("--wake-interval", type=int, default=None,
                        help="PMU wake interval (default 10, aov 60)")
    parser.add_argument("--wake-check", type=int, default=5)
    parser.add_argument("--off-seconds", type=float, default=20)
    parser.add_argument("--settle", type=float, default=30)
    parser.add_argument("--idle-seconds", type=float, default=90)
    parser.add_argument("--suspend-seconds", type=float, default=60)
    parser.add_argument("--tail-seconds", type=float, default=10)
    parser.add_argument("--base-seconds", type=float, default=20)
    parser.add_argument("--yolo-seconds", type=float, default=20)
    parser.add_argument("--stream-seconds", type=float, default=20)
    parser.add_argument("--motor-steps", type=int, default=2400)
    parser.add_argument("--motor-speed", type=int, default=1000)
    parser.add_argument("--rtsp-url", default=None)
    parser.add_argument("--rtsp-mode", default="rtsp")
    parser.add_argument("--soak-minutes", type=float, default=60)
    parser.add_argument("--receiver", default=RECEIVER)
    parser.add_argument("--no-start-service", action="store_true")
    parser.add_argument("--service-timeout", type=float, default=60)
    parser.add_argument("--no-post-checks", dest="post_checks",
                        action="store_false")
    parser.add_argument("--stm8-soak", type=int, default=300)
    parser.add_argument("--stm8-fw", default="2.3.2",
                        help="expected STM8 firmware version (default 2.3.2)")
    parser.add_argument("--stm8-base-seconds", type=int, default=20)
    parser.add_argument("--crosscheck-seconds", type=float, default=60)
    parser.add_argument("--no-restart", action="store_true")
    parser.add_argument("--motion-min-area", type=int, default=30)
    parser.add_argument("--motion-cycles", type=int, default=3)
    parser.add_argument("--aov-mcu", action="store_true",
                        help="enable the AOV MCU wake proxy")
    parser.add_argument("--aov-keep-isp", action="store_true",
                        help="keep CSI/ISP running for the MCU")
    parser.add_argument("--aov-seconds", type=float, default=180)
    parser.add_argument("--aov-wake-check", type=int, default=10)
    parser.add_argument("--heartbeat-seconds", type=int, default=0,
                        help="[sleep] heartbeat_interval_s for the AOV run "
                             "(0 keeps the heartbeat off)")
    parser.add_argument("--usb-on-motion", action="store_true",
                        help="[sleep] usb_on_motion: unbind the USB host "
                             "before mem, bind it only on a motion wake")
    parser.add_argument("--expect-present", action="store_true",
                        help="exit 3 when STM8 motor answers disappear")
    parser.add_argument("--motor-sleep", default="keep",
                        choices=["keep", "on", "off"],
                        help="set [motor] sleep_enable and restart divinus")
    parser.add_argument("--ladder-rebind", action="store_true", default=True,
                        help="unbind/rebind i2c3 in the ladder (default; "
                             "safe on ff460000.i2c)")
    parser.add_argument("--no-ladder-rebind", dest="ladder_rebind",
                        action="store_false",
                        help="skip the i2c3 unbind/rebind ladder step")
    parser.add_argument("--ladder-ec-reset", action="store_true",
                        help="allow EC reboot as the last ladder step")
    parser.add_argument("--ladder-bitbang", action="store_true", default=True,
                        help="9 SCL clocks between unbind and rebind (default)")
    parser.add_argument("--no-ladder-bitbang", dest="ladder_bitbang",
                        action="store_false",
                        help="skip the GPIO bit-bang bus recovery")
    parser.add_argument("--i2c-cycles", type=int, default=300)
    parser.add_argument("--i2c-step", type=int, default=5)
    parser.add_argument("--motion-sensitivity", type=int, default=3)
    parser.add_argument("--motion-wake-check", type=int, default=30)
    parser.add_argument("--motion-scale", default="zoom",
                        choices=["focus", "zoom"])
    parser.add_argument("--motion-steps", type=int, default=800)
    parser.add_argument("--motion-speed", type=int, default=800)
    parser.add_argument("--motion-timeout", type=float, default=240)
    parser.add_argument("--cycle-timeout", type=float, default=300)
    parser.add_argument("--boot-timeout", type=float, default=180)
    args = parser.parse_args()
    if args.hz is None:
        args.hz = 8.0 if args.scenario == "peaks" else 2.0
    if args.wake_interval is None:
        args.wake_interval = 60 if args.scenario == "aov" else 10

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    outdir = Path(args.out_dir) if args.out_dir else \
        OUT_ROOT / ("%s-%s" % (stamp, args.scenario))
    outdir.mkdir(parents=True, exist_ok=True)
    if not args.cookie_file:
        args.cookie_file = str(outdir / "bridge.cookie")
    events = Events(outdir / "events.log")
    log("artifact dir: %s" % outdir)
    started = time.time()
    motor_original = None
    try:
        if args.motor_sleep != "keep":
            log("setting [motor] sleep_enable=%s and restarting divinus"
                % args.motor_sleep)
            motor_original = motor_sleep_config(args.motor_sleep)
            events.write("motor-sleep", action=args.motor_sleep,
                         original_sha=len(motor_original or ""))
        try:
            if args.scenario == "levels":
                result = scenario_levels(args, outdir, events)
            elif args.scenario == "cycle":
                result = scenario_cycle(args, outdir, events)
            elif args.scenario == "motion":
                result = scenario_motion(args, outdir, events)
            elif args.scenario == "aov":
                result = scenario_aov(args, outdir, events)
            elif args.scenario == "peaks":
                result = scenario_peaks(args, outdir, events)
            elif args.scenario == "soak":
                result = scenario_soak(args, outdir, events)
            elif args.scenario == "i2c":
                result = scenario_i2c(args, outdir, events)
            else:
                result = scenario_stm8(args, outdir, events)
        finally:
            if motor_original is not None:
                restored = motor_sleep_restore(motor_original)
                events.write("motor-sleep-restore", ok=restored)
                if not restored:
                    log("warning: could not restore /userdata/divinus.yaml")
        dump_dmesg(outdir, "before-final-probe")
        final = {}
        try:
            final = stm8_probe()
        except Exception as exc:                            # noqa: BLE001
            events.write("stm8-final-error", error=str(exc))
        result["stm8_final"] = final
        lost = bool(result.get("stm8_lost")) or final.get("present") is False
        gate_notes = []
        if final.get("present") is False:
            log("STM8 missing after the run; running recovery ladder")
            result["stm8_recovery"] = stm8_recovery(
                outdir, events, gate_notes,
                reason="после завершения сценария %s" % args.scenario,
                allow_rebind=args.ladder_rebind,
                allow_ec_reset=args.ladder_ec_reset,
                allow_bitbang=args.ladder_bitbang)
            time.sleep(2)
            final = stm8_probe(attempts=5)
            result["stm8_final"] = final
            if final.get("present"):
                log("recovery ladder restored the STM8 bus")
            else:
                log("recovery ladder did not restore the bus")
        if gate_notes:
            result["gate_notes"] = gate_notes
        result["stm8_lost"] = lost
        result["gate_failed"] = bool(args.expect_present and lost)
        result.update({"scenario": args.scenario, "elapsed_s":
                       round(time.time() - started, 1),
                       "args": vars(args), "out_dir": str(outdir)})
        (outdir / "summary.json").write_text(
            json.dumps(result, indent=1, ensure_ascii=False))
        log("summary: %s" % (outdir / "summary.json"))
        if lost:
            log("STM8 motor is not answering (0x60 missing)")
        return 3 if result["gate_failed"] else 0
    finally:
        events.close()


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, OSError, KeyboardInterrupt) as exc:
        print("error: %s" % exc, file=sys.stderr)
        sys.exit(1)