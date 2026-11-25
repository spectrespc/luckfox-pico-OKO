#!/usr/bin/env python3
"""Per-cycle fast-resume matrix for the AOV [sleep] strategies (KI-072).

Drives the camera through N sleep/resume cycles with a fixed test config and
records, for every resume: the kernel resume path (`last_suspend_time`), the
first HTTP/TCP answer, `/api/status` 200, a JPEG > 1000 B, the divinus thread
count, the dmesg counters of `mipi-csi2-hw ERR` / `mpp: ctx is no found`, the
listener on :80, `pidof` and the dmabuf/RSS/fd footprint.

Modes: `exec` (V0 baseline), `live` (V1), `stopstart` (V2), `aiq` (V3).  The
mode is written through `POST /api/sleep?fast_resume=...`; the running process
does not apply live settings, so divinus is restarted (SIGHUP) afterwards.

Config per run (overridable): enable=1, aov_mcu=0, aov_keep_isp=0,
usb_on_motion=1, idle_timeout_s=15, wake_interval_s=--interval,
wake_check_s=5, heartbeat=0.  `--aov` swaps aov_mcu/keep_isp to 1 for the
two-tier regression.

Examples:
    bench/run.sh aov_resume_matrix.py --mode exec --cycles 30 --interval 5
    bench/run.sh aov_resume_matrix.py --mode aiq --cycles 300 --interval 3 \
        --stop-on-mipi 0
    bench/run.sh aov_resume_matrix.py --mode aiq --aov --cycles 10

Artifacts: output/bench/aov-test/fast-resume-20261125/<mode>/{run.json,
report.md,dmesg-<stamp>.txt}.  The config of the DUT is restored in a
`finally` block unless --keep-config is given.

Timing definitions: the host cannot see the wake instant, so `wake_to_api_s`
and `wake_to_jpeg_s` add the kernel resume path (`last_suspend_time`, first
value) to the host-measured latency after the first successful TCP connect.
The raw values are kept in run.json.

Exit codes: 0 all cycles measured, 1 run aborted by a stop condition,
2 completed with failed cycles.
"""
import argparse
import base64
import json
import os
import socket
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUT = ROOT / "output" / "bench" / "aov-test" / "fast-resume-20261125"

HOST = os.environ.get("OKO_DUT_HOST", "192.168.52.33")
USER = os.environ.get("OKO_RTSP_USER", "admin")
PASSWORD = os.environ.get("OKO_DUT_PASSWORD", "oko-change-me")
SSH_USER = os.environ.get("OKO_DUT_SSH_USER", "root")
SSH_PASSWORD = os.environ.get("OKO_DUT_SSH_PASSWORD", "luckfox")
MODES = ("exec", "live", "stopstart", "aiq")
SUSPEND_TIME = "/sys/kernel/wakeup_reasons/last_suspend_time"

# One round trip over SSH: process footprint, kernel resume path and the two
# counters that identify the V1/V2 failure signatures.
PROBE_CMD = (
    "P=$(pidof divinus);"
    "printf 'PID:%s\\n' \"$P\";"
    "printf 'THREADS:%s\\n' \"$(ls /proc/$P/task 2>/dev/null | wc -l)\";"
    "printf 'FD:%s\\n' \"$(ls /proc/$P/fd 2>/dev/null | wc -l)\";"
    "printf 'RSS:%s\\n' \"$(awk '/VmRSS/{print $2}' /proc/$P/status 2>/dev/null)\";"
    "printf 'DMABUF:%s\\n' \"$(awk '/Dma-buf/{print $2}' /proc/meminfo)\";"
    "printf 'SUSPEND:%s\\n' \"$(cat " + SUSPEND_TIME + " 2>&1)\";"
    "printf 'MIPI:%s\\n' \"$(dmesg | grep -c 'mipi-csi2-hw ERR')\";"
    "printf 'CTX:%s\\n' \"$(dmesg | grep -c 'ctx is no found')\";"
    "printf 'SFC:%s\\n' \"$(dmesg | grep -cE 'rockchip-sfc.*(timeout|failed)')\";"
    "printf 'UBIERR:%s\\n' \"$(dmesg | grep -c 'ubi_io_.*error')\";"
    "printf 'LISTEN:%s\\n' \"$(netstat -ltn 2>/dev/null | grep -c ':80 ')\";"
    "printf 'UPTIME:%s\\n' \"$(cut -d' ' -f1 /proc/uptime)\""
)


def log(message):
    print(message)
    sys.stdout.flush()


def stamp():
    return datetime.now().strftime("%Y%m%d-%H%M%S")


# --------------------------------------------------------------------------
# DUT HTTP API


def http(path, timeout=2.0, method="GET"):
    url = "http://%s%s" % (HOST, path)
    req = urllib.request.Request(url, data=b"" if method == "POST" else None,
                                 method=method)
    token = base64.b64encode(("%s:%s" % (USER, PASSWORD)).encode()).decode()
    req.add_header("Authorization", "Basic " + token)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.status, resp.read()
    except urllib.error.HTTPError as err:
        return err.code, err.read()
    except Exception as err:                                # noqa: BLE001
        return 0, str(err).encode()


def api_json(path, timeout=2.0, method="GET"):
    status, payload = http(path, timeout=timeout, method=method)
    try:
        return status, json.loads(payload.decode())
    except Exception:                                       # noqa: BLE001
        return status, {}


def post_sleep(values, timeout=8.0):
    query = urllib.parse.urlencode(values)
    return api_json("/api/sleep?" + query, timeout=timeout, method="POST")


def sleep_config(timeout=2.0):
    status, dec = api_json("/api/sleep", timeout=timeout)
    return dec if status == 200 else None


def port_open(port, timeout=0.5):
    try:
        with socket.create_connection((HOST, port), timeout=timeout):
            return True
    except OSError:
        return False


def reachable():
    """Either listener answering means the AP is out of deep suspend."""
    return port_open(80, 0.4) or port_open(22, 0.4)


def wait_http(timeout=180.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        status, _ = http("/api/status", timeout=1.5)
        if status == 200:
            return True
        time.sleep(1.0)
    return False


# --------------------------------------------------------------------------
# SSH probe


def ssh_probe():
    """One SSH round trip, retried once; returns a dict with `error` on failure."""
    try:
        import paramiko
    except ImportError:
        return {"error": "paramiko is not installed (bench/setup.sh)"}
    last = "no attempt"
    for attempt in range(3):
        client = paramiko.SSHClient()
        client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        try:
            client.connect(HOST, username=SSH_USER, password=SSH_PASSWORD,
                           timeout=4, banner_timeout=6, auth_timeout=6)
            _stdin, stdout, stderr = client.exec_command(PROBE_CMD,
                                                         timeout=12)
            text = stdout.read().decode(errors="replace")
            err = stderr.read().decode(errors="replace")
            code = stdout.channel.recv_exit_status()
            if code != 0:
                raise RuntimeError((err or "ssh exit %d" % code).strip())
            return parse_probe(text)
        except Exception as exc:                            # noqa: BLE001
            last = str(exc)
        finally:
            try:
                client.close()
            except Exception:                               # noqa: BLE001
                pass
        if attempt < 2:
            time.sleep(1.0)
    return {"error": last}


def parse_probe(text):
    out = {"error": None}
    for line in text.splitlines():
        key, _, value = line.partition(":")
        if not _:
            continue
        value = value.strip()
        if key == "PID":
            out["pid"] = value or None
        elif key == "THREADS":
            out["threads"] = int(value) if value.isdigit() else None
        elif key == "FD":
            out["fd"] = int(value) if value.isdigit() else None
        elif key == "RSS":
            out["rss_kb"] = int(value) if value.isdigit() else None
        elif key == "DMABUF":
            out["dmabuf_kb"] = int(value) if value.isdigit() else None
        elif key == "SUSPEND":
            parts = value.split()
            if len(parts) == 2:
                try:
                    out["resume_s"] = round(float(parts[0]), 3)
                    out["sleep_s"] = round(float(parts[1]), 3)
                except ValueError:
                    pass
        elif key == "MIPI":
            out["mipi_total"] = int(value) if value.isdigit() else None
        elif key == "CTX":
            out["ctx_total"] = int(value) if value.isdigit() else None
        elif key == "SFC":
            out["sfc_total"] = int(value) if value.isdigit() else None
        elif key == "UBIERR":
            out["ubi_total"] = int(value) if value.isdigit() else None
        elif key == "LISTEN":
            out["listen80"] = int(value) if value.isdigit() else None
        elif key == "UPTIME":
            try:
                out["uptime_s"] = round(float(value), 1)
            except ValueError:
                pass
    return out


def ssh_command(command, timeout=20):
    import paramiko
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    try:
        client.connect(HOST, username=SSH_USER, password=SSH_PASSWORD,
                       timeout=6, banner_timeout=10, auth_timeout=10)
        _stdin, stdout, _stderr = client.exec_command(command, timeout=timeout)
        return stdout.channel.recv_exit_status()
    finally:
        client.close()


def restart_streamer():
    """SIGHUP (exec restart) as deploy_divinus.py does; API restart as fallback."""
    try:
        if ssh_command("kill -HUP $(pidof divinus)") == 0:
            return "sighup"
    except Exception as exc:                                # noqa: BLE001
        log("restart: SSH failed (%s); using /api/cmd?restart" % exc)
    status, _ = http("/api/cmd?restart", timeout=20, method="POST")
    return "api" if status == 200 else "failed"


# --------------------------------------------------------------------------
# Run configuration


def apply_config(args):
    values = {
        "enable": "true",
        "pmu_timer_wake": "true",
        "aov_mcu": "true" if args.aov else "false",
        "aov_keep_isp": "true" if args.aov else "false",
        "usb_on_motion": "false" if args.no_usb_on_motion else "true",
        "idle_timeout_s": str(args.idle),
        "wake_interval_s": str(args.interval),
        "wake_check_s": str(args.wake_check),
        "heartbeat": "false",
        "fast_resume": args.mode,
    }
    status, dec = post_sleep(values)
    if status != 200 or dec.get("error"):
        raise RuntimeError("sleep settings refused: HTTP %s %s" % (status, dec))
    how = restart_streamer()
    deadline = time.monotonic() + 180
    while time.monotonic() < deadline:
        dec = sleep_config(timeout=1.5)
        if (dec and dec.get("fast_resume") == args.mode and
                dec.get("enable") is True):
            log("config applied: fast_resume=%s (restart=%s)" %
                (args.mode, how))
            return values, dec
        time.sleep(1.0)
    raise RuntimeError("divinus did not come back with fast_resume=%s" %
                       args.mode)


SLEEP_KEYS = ("enable", "pmu_timer_wake", "aov_mcu", "aov_keep_isp",
              "usb_on_motion", "idle_timeout_s", "wake_interval_s",
              "wake_check_s", "heartbeat", "fast_resume")


def snapshot_config():
    dec = sleep_config(timeout=3.0) or {}
    out = {}
    for key in SLEEP_KEYS:
        if key not in dec:
            continue
        value = dec[key]
        out[key] = ("true" if value else "false") if isinstance(value, bool) \
            else str(value)
    heartbeat = dec.get("heartbeat")
    if isinstance(heartbeat, dict) and "enable" in heartbeat:
        out["heartbeat"] = "true" if heartbeat["enable"] else "false"
    return out


def restore_config(original):
    if not original:
        return True
    for _attempt in range(6):
        if not wait_http(30):
            continue
        status, dec = post_sleep(original)
        if status == 200 and not dec.get("error"):
            restart_streamer()
            return wait_http(180)
        time.sleep(2.0)
    log("restore: could not put the sleep config back: %s" % original)
    return False


# --------------------------------------------------------------------------
# Cycle loop


def measure_cycle(args, down_at):
    """Wait for the resume, then collect the per-cycle record."""
    wake_deadline = time.monotonic() + args.offline_timeout
    while time.monotonic() < wake_deadline:
        if reachable():
            break
        time.sleep(0.2)
    else:
        raise RuntimeError("no wake within %s s" % args.offline_timeout)

    t_connect = time.monotonic()
    record = {"down_s": round(t_connect - down_at, 3)}

    worker = threading.Thread(target=lambda: record.update({"ssh": ssh_probe()}))
    worker.start()

    # In exec mode the process is still starting when the first listener
    # accepts, so /api/status is polled instead of asked once.  In the
    # in-process modes the media is already up and the first answer comes
    # within a few hundred ms; a device that goes down again ends the poll.
    api_deadline = time.monotonic() + args.api_timeout
    status = 0
    while time.monotonic() < api_deadline:
        # The restarted process re-runs network_start() and the PHY/RMII link
        # flaps; a single failed connect is not a lost window, so the poll
        # only stops at the deadline or on 200.
        status, _payload = http("/api/status", timeout=1.0)
        if status == 200:
            break
        time.sleep(0.1)
    t_api = time.monotonic()
    record["api_status"] = status
    record["api_ms"] = round((t_api - t_connect) * 1000.0)
    record["t_api_after_connect_s"] = round(t_api - t_connect, 3)

    jstatus, jpeg = 0, b""
    for _attempt in range(3):
        jstatus, jpeg = http("/image.jpg", timeout=4.0)
        if jstatus == 200:
            break
        time.sleep(0.4)
    record["jpeg_status"] = jstatus
    record["jpeg_bytes"] = len(jpeg) if jstatus == 200 else 0
    record["jpeg_ms"] = round((time.monotonic() - t_api) * 1000.0)
    record["t_jpeg_after_connect_s"] = round(
        t_api - t_connect + record["jpeg_ms"] / 1000.0, 3)

    dec = sleep_config(timeout=1.5) or {}
    record["state"] = dec.get("state")

    worker.join(timeout=args.probe_timeout)
    if worker.is_alive():
        record["ssh"] = {"error": "probe timeout"}
    elif "ssh" not in record:
        record["ssh"] = {"error": "probe produced nothing"}

    probe = record["ssh"]
    if probe.get("resume_s") is not None:
        record["wake_to_api_s"] = round(
            probe["resume_s"] + record["t_api_after_connect_s"], 3)
        record["wake_to_jpeg_s"] = round(
            probe["resume_s"] + record["t_jpeg_after_connect_s"], 3)

    # Counters are cumulative in the dmesg ring; keep the cumulative value so
    # a wrapped ring is visible (a drop) instead of being silently lost.
    record["mipi_total"] = probe.get("mipi_total")
    record["ctx_total"] = probe.get("ctx_total")
    record["sfc_total"] = probe.get("sfc_total")
    record["ubi_total"] = probe.get("ubi_total")
    record["listen80"] = probe.get("listen80")
    record["pid"] = probe.get("pid")
    record["threads"] = probe.get("threads")
    record["fd"] = probe.get("fd")
    record["rss_kb"] = probe.get("rss_kb")
    record["dmabuf_kb"] = probe.get("dmabuf_kb")
    record["sleep_s"] = probe.get("sleep_s")
    record["resume_s"] = probe.get("resume_s")
    record["ssh_error"] = probe.get("error")

    # The probe window is short; if the third request failed the cycle is
    # still counted but flagged.
    record["window_lost"] = not (status == 200 and jstatus == 200)
    return record


def run_cycles(args, baseline=None, cycles=None):
    if cycles is None:
        cycles = []
    prev = {"mipi": (baseline or {}).get("mipi_total"),
            "ctx": (baseline or {}).get("ctx_total"),
            "sfc": (baseline or {}).get("sfc_total"),
            "ubi": (baseline or {}).get("ubi_total"),
            "pid": (baseline or {}).get("pid"),
            "dmabuf": (baseline or {}).get("dmabuf_kb")}
    n = 0
    while n < args.cycles:
        # Wait for the policy to enter mem: both listeners must be gone.
        down_deadline = time.monotonic() + args.idle + args.interval + 90
        down_at = None
        while time.monotonic() < down_deadline:
            if not reachable():
                # Two samples: the PHY link flaps during a restart.
                time.sleep(0.3)
                if not reachable():
                    down_at = time.monotonic()
                    break
            time.sleep(0.25)
        if down_at is None:
            raise RuntimeError("device stayed reachable: no suspend observed")

        record = measure_cycle(args, down_at)
        n += 1
        record["n"] = n

        mipi = record.get("mipi_total")
        ctx = record.get("ctx_total")
        record["mipi_delta"] = None if (mipi is None or prev["mipi"] is None) \
            else max(0, mipi - prev["mipi"]) if mipi >= prev["mipi"] else 0
        record["ctx_delta"] = None if (ctx is None or prev["ctx"] is None) \
            else max(0, ctx - prev["ctx"]) if ctx >= prev["ctx"] else 0
        sfc = record.get("sfc_total")
        ubi = record.get("ubi_total")
        record["sfc_delta"] = None if (sfc is None or prev["sfc"] is None) \
            else max(0, sfc - prev["sfc"]) if sfc >= prev["sfc"] else 0
        record["ubi_delta"] = None if (ubi is None or prev["ubi"] is None) \
            else max(0, ubi - prev["ubi"]) if ubi >= prev["ubi"] else 0
        record["pid_changed"] = (prev["pid"] is not None and
                                 record.get("pid") is not None and
                                 record["pid"] != prev["pid"])
        if mipi is not None:
            prev["mipi"] = mipi
        if ctx is not None:
            prev["ctx"] = ctx
        if sfc is not None:
            prev["sfc"] = sfc
        if ubi is not None:
            prev["ubi"] = ubi
        if record.get("pid"):
            prev["pid"] = record["pid"]
        record["pid_stable"] = not record["pid_changed"]

        log("cycle %d/%d: resume=%s api=%sms jpeg=%sms threads=%s "
            "mipi=+%s ctx=+%s sfc=+%s ubi=+%s listen=%s pid=%s%s" % (
                n, args.cycles, record.get("resume_s"),
                record.get("api_ms"), record.get("jpeg_ms"),
                record.get("threads"), record.get("mipi_delta"),
                record.get("ctx_delta"), record.get("sfc_delta"),
                record.get("ubi_delta"), record.get("listen80"),
                record.get("pid"),
                "" if not record.get("ssh_error")
                else " ssh_err=%s" % record["ssh_error"]))
        cycles.append(record)

        if args.stop_on_sfc is not None:
            sfc_sum = sum(r["sfc_delta"] for r in cycles
                          if r.get("sfc_delta"))
            if sfc_sum > args.stop_on_sfc:
                raise RuntimeError(
                    "NAND sfc error appeared (KI-071): %d new line(s)" %
                    sfc_sum)
        if args.stop_on_mipi is not None:
            mipi_sum = sum(r["mipi_delta"] for r in cycles
                           if r.get("mipi_delta"))
            if mipi_sum > args.stop_on_mipi:
                raise RuntimeError("mipi-csi2-hw ERR total %d > %d" %
                                   (mipi_sum, args.stop_on_mipi))
    return cycles


# --------------------------------------------------------------------------
# Summary and report


def percentile(values, fraction):
    if not values:
        return None
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(fraction * len(ordered)))]


def summary(cycles):
    def values(key):
        return [c[key] for c in cycles if c.get(key) is not None]

    def drift(key):
        seen = values(key)
        if len(seen) < 2 or not seen[0]:
            return None
        return round(100.0 * (seen[-1] - seen[0]) / seen[0], 2)

    resume = values("resume_s")
    api = values("wake_to_api_s")
    jpeg = values("wake_to_jpeg_s")
    mipi = sum(c["mipi_delta"] for c in cycles if c.get("mipi_delta"))
    ctx = sum(c["ctx_delta"] for c in cycles if c.get("ctx_delta"))
    sfc = sum(c["sfc_delta"] for c in cycles if c.get("sfc_delta"))
    ubi = sum(c["ubi_delta"] for c in cycles if c.get("ubi_delta"))
    threads = values("threads")
    return {
        "cycles": len(cycles),
        "api_ok": sum(1 for c in cycles if c.get("api_status") == 200),
        "jpeg_ok": sum(1 for c in cycles if c.get("jpeg_bytes", 0) > 1000),
        "ssh_ok": sum(1 for c in cycles if c.get("threads") is not None),
        "window_lost": sum(1 for c in cycles if c.get("window_lost")),
        "resume_s_median": percentile(resume, 0.5),
        "resume_s_p90": percentile(resume, 0.9),
        "wake_to_api_s_median": percentile(api, 0.5),
        "wake_to_api_s_p90": percentile(api, 0.9),
        "wake_to_jpeg_s_median": percentile(jpeg, 0.5),
        "wake_to_jpeg_s_p90": percentile(jpeg, 0.9),
        "threads_min": min(threads) if threads else None,
        "threads_median": percentile(threads, 0.5),
        "threads_max": max(threads) if threads else None,
        "pid_changes": sum(1 for c in cycles if c.get("pid_changed")),
        "mipi_total": mipi,
        "ctx_total": ctx,
        "sfc_total": sfc,
        "ubi_total": ubi,
        "listen_missing": sum(1 for c in cycles if c.get("listen80") == 0),
        "dmabuf_drift_pct": drift("dmabuf_kb"),
        "rss_drift_pct": drift("rss_kb"),
        "fd_drift_pct": drift("fd"),
    }


def write_report(path, args, meta, cycles, stats):
    lines = ["# Матрица resume: `%s`" % args.mode, "",
             "- Дата: %s" % meta["started"],
             "- DUT: `%s`, ядро: `%s`" % (HOST, meta.get("kernel", "?")),
             "- Циклов: %d из %d, интервал %d с, idle %d с, wake_check %d с" %
             (stats["cycles"], args.cycles, args.interval, args.idle,
              args.wake_check),
             "- AOV MCU: %s, USB on motion: %s" %
             ("да" if args.aov else "нет",
              "нет" if args.no_usb_on_motion else "да"),
             "- Артефакт: `%s`" % path.parent, "",
             "## Сводка", "",
             "| Показатель | Значение | Порог V3 |", "|---|---|---|"]
    rows = [
        ("циклов измерено", stats["cycles"], "—"),
        ("API 200", stats["api_ok"], "все"),
        ("JPEG > 1000 Б", stats["jpeg_ok"], "все"),
        ("SSH-проба снята", stats["ssh_ok"], "все"),
        ("окно измерения потеряно", stats["window_lost"], "0"),
        ("resume-путь, медиана/ p90, с",
         "%s / %s" % (stats["resume_s_median"], stats["resume_s_p90"]), "—"),
        ("wake→API, медиана/ p90, с",
         "%s / %s" % (stats["wake_to_api_s_median"],
                      stats["wake_to_api_s_p90"]), "≤ 1.0"),
        ("wake→JPEG, медиана/ p90, с",
         "%s / %s" % (stats["wake_to_jpeg_s_median"],
                      stats["wake_to_jpeg_s_p90"]), "≤ 1.5"),
        ("потоки divinus, мин/медиана/макс",
         "%s / %s / %s" % (stats["threads_min"], stats["threads_median"],
                           stats["threads_max"]), "±1"),
        ("смен PID", stats["pid_changes"],
         "0 (не exec)" if args.mode != "exec" else "каждый цикл (exec)"),
        ("mipi-csi2-hw ERR, суммарно", stats["mipi_total"], "0"),
        ("mpp ctx is no found, суммарно", stats["ctx_total"], "0"),
        ("NAND sfc/ubi ошибки, суммарно",
         "%s / %s" % (stats["sfc_total"], stats["ubi_total"]),
         "0 (KI-071)"),
        ("циклов без :80", stats["listen_missing"], "0"),
        ("drift dma-buf / RSS / fd, %",
         "%s / %s / %s" % (stats["dmabuf_drift_pct"], stats["rss_drift_pct"],
                           stats["fd_drift_pct"]), "≤ 2"),
    ]
    for name, value, limit in rows:
        lines.append("| %s | %s | %s |" % (name, value, limit))
    lines += ["", "## Циклы", ""]
    show = cycles if len(cycles) <= 60 else cycles[:30] + cycles[-30:]
    lines += ["| # | сон, с | resume, с | API, мс | JPEG, мс | потоки | "
              "mipi | ctx | :80 | PID |", "|---|---|---|---|---|---|---|---|---|---|"]
    for cycle in show:
        lines.append("| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |" % (
            cycle["n"], cycle.get("sleep_s"), cycle.get("resume_s"),
            cycle.get("api_ms"), cycle.get("jpeg_ms"), cycle.get("threads"),
            cycle.get("mipi_delta"), cycle.get("ctx_delta"),
            cycle.get("listen80"), cycle.get("pid")))
    if len(cycles) > len(show):
        lines.append("")
        lines.append("_Показаны первые и последние 30 циклов; полный набор — "
                     "в `run.json`._")
    lines.append("")
    path.write_text("\n".join(lines))


def dmesg_dump(outdir, tag):
    try:
        import paramiko
        client = paramiko.SSHClient()
        client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        client.connect(HOST, username=SSH_USER, password=SSH_PASSWORD,
                       timeout=8, banner_timeout=10, auth_timeout=10)
        try:
            _stdin, stdout, _stderr = client.exec_command("dmesg", timeout=30)
            text = stdout.read().decode(errors="replace")
        finally:
            client.close()
        (outdir / ("dmesg-%s.txt" % tag)).write_text(text)
    except Exception as exc:                                # noqa: BLE001
        log("dmesg dump failed: %s" % exc)


def kernel_release():
    try:
        import paramiko
        client = paramiko.SSHClient()
        client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        client.connect(HOST, username=SSH_USER, password=SSH_PASSWORD,
                       timeout=6, banner_timeout=8, auth_timeout=8)
        try:
            _stdin, stdout, _stderr = client.exec_command("uname -r", timeout=8)
            return stdout.read().decode().strip() or "?"
        finally:
            client.close()
    except Exception:                                       # noqa: BLE001
        return "?"


def main():
    global HOST

    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--mode", required=True, choices=MODES)
    parser.add_argument("--cycles", type=int, default=30)
    parser.add_argument("--interval", type=int, default=5,
                        help="wake_interval_s (спать между пробуждениями)")
    parser.add_argument("--idle", type=int, default=15)
    parser.add_argument("--wake-check", type=int, default=5)
    parser.add_argument("--aov", action="store_true",
                        help="aov_mcu=1 + aov_keep_isp=1 (двухуровневый сон)")
    parser.add_argument("--no-usb-on-motion", action="store_true",
                        help="держать USB-хост привязанным (usb_on_motion=0)")
    parser.add_argument("--host", default=HOST)
    parser.add_argument("--offline-timeout", type=float, default=180.0)
    parser.add_argument("--probe-timeout", type=float, default=12.0)
    parser.add_argument("--api-timeout", type=float, default=12.0,
                        help="сколько ждать /api/status после первого контакта")
    parser.add_argument("--stop-on-mipi", type=int, default=None,
                        help="прервать прогон, когда сумма mipi ERR превысит N")
    parser.add_argument("--stop-on-sfc", type=int, default=0,
                        help="прервать прогон при новых ошибках NAND (KI-071); "
                             "-1 отключает")
    parser.add_argument("--keep-config", action="store_true")
    parser.add_argument("--out-dir", default=str(DEFAULT_OUT))
    args = parser.parse_args()

    HOST = args.host

    outdir = Path(args.out_dir).expanduser() / args.mode
    outdir.mkdir(parents=True, exist_ok=True)
    started = datetime.now()
    meta = {"started": started.isoformat(timespec="seconds"),
            "mode": args.mode, "host": HOST,
            "kernel": kernel_release(),
            "args": vars(args)}
    original = {}
    cycles = []
    stopped = None
    try:
        if not wait_http(60):
            raise RuntimeError("DUT is not reachable before the run")
        original = snapshot_config()
        log("original [sleep]: %s" % original)
        baseline = ssh_probe()
        log("baseline: threads=%s mipi=%s ctx=%s pid=%s" % (
            baseline.get("threads"), baseline.get("mipi_total"),
            baseline.get("ctx_total"), baseline.get("pid")))
        meta["baseline"] = baseline
        apply_config(args)
        run_cycles(args, baseline, cycles)
    except KeyboardInterrupt:
        stopped = "interrupted"
    except Exception as exc:                                # noqa: BLE001
        stopped = str(exc)
        log("run stopped: %s" % stopped)
    finally:
        restored = True
        if original and not args.keep_config:
            restored = restore_config(original)
        meta["finished"] = datetime.now().isoformat(timespec="seconds")
        meta["restored"] = restored
        meta["stopped"] = stopped

    stats = summary(cycles)
    run = {"meta": meta, "cycles": cycles, "summary": stats}
    (outdir / "run.json").write_text(
        json.dumps(run, indent=2, ensure_ascii=False) + "\n")
    write_report(outdir / "report.md", args, meta, cycles, stats)
    dmesg_dump(outdir, stamp())

    log(json.dumps(stats, indent=2, ensure_ascii=False))
    log("artifacts: %s" % outdir)
    if stopped:
        return 1
    if stats["cycles"] < args.cycles or stats["window_lost"]:
        return 2
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as exc:
        print("error: %s" % exc, file=sys.stderr)
        sys.exit(1)