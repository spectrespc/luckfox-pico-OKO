#!/usr/bin/env python3
"""Soak the 2BPP box overlays and watch divinus fd/RSS/kernel counters (KI-063).

Two overlays paint through the same vendor region calls: YOLO boxes
(`/api/yolo?boxes=`) and IVS motion rectangles (`/api/ivs?boxes=`, or the
`ivs.boxes` configuration key while the HTTP switch is blocked).  Enabling and
disabling them repeatedly is the suspected hang trigger, so the script drives
the A/B cycles and samples the process from the outside: fd count, VmRSS,
kernel error counters from `dmesg`, and HTTP liveness.

Modes:
    observe  sample only (baseline health run)
    steady   enable boxes on --target, hold them for --seconds, then sample
    cycle    toggle boxes through the HTTP API --count times
    restart  graceful SIGHUP restart --count times, sample after each
    config   set a `boxes` key in /userdata/divinus.yaml, optionally restart

Run it through the wrapper so the venv is used:

    bench/run.sh rgn_boxes_check.py --mode observe --seconds 1800
    bench/run.sh rgn_boxes_check.py --mode cycle --target yolo --count 200
    bench/run.sh rgn_boxes_check.py --mode config --target ivs --value true
    bench/run.sh rgn_boxes_check.py --mode steady --target ivs --seconds 1800

Exit codes: 0 -- within limits, 1 -- regression or hang, 2 -- setup failure.
"""
import argparse
import base64
import json
import os
import sys
import time
import urllib.error
import urllib.request

import paramiko

OUTPUT = os.environ.get("OKO_BENCH_OUT",
                        os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                     os.pardir, "output", "bench"))
HOST = os.environ.get("OKO_DUT_HOST", "192.168.52.33")
USER = os.environ.get("OKO_DUT_USER", "root")
PASSWORD = os.environ.get("OKO_DUT_PASSWORD", "luckfox")
HTTP_USER = os.environ.get("OKO_RTSP_USER", "admin")
HTTP_PASSWORD = os.environ.get("OKO_RTSP_PASS", "oko-change-me")
HTTP_TIMEOUT = 4

# Kernel counters that must not move during a healthy run.  Each pattern is
# counted separately so the report shows which class fired.
KERNEL_PATTERNS = {
    "sfc": "rockchip-sfc",
    "ubifs": "UBIFS error",
    "mpp_fd": "dup mClientFd|CHECK_GE",
    "oops": "Call trace|Unable to handle|Internal error|Kernel panic|Oops",
    "rga": "rga2_reg",
}

SAMPLE_CMD = (
    'p=$(pidof divinus); '
    'if [ -z "$p" ]; then echo ALIVE=0; else echo ALIVE=1; '
    'for pid in $p; do echo PID=$pid; '
    'echo FD=$(ls /proc/$pid/fd 2>/dev/null | wc -l); '
    'echo RSS_KB=$(awk \'/^VmRSS:/{print $2}\' /proc/$pid/status 2>/dev/null); '
    'echo THREADS=$(ls /proc/$pid/task 2>/dev/null | wc -l); '
    'done; fi; '
    'echo UPTIME=$(cut -d. -f1 /proc/uptime); '
    + "".join('echo K_%s=$(dmesg | grep -Eic \'%s\'); ' % (key, pattern)
              for key, pattern in KERNEL_PATTERNS.items())
)


class Dut:
    """SSH access to the DUT.  One connection, reconnected on demand."""

    def __init__(self, host, user, password):
        self.host = host
        self.user = user
        self.password = password
        self.client = None

    def connect(self):
        if self.client:
            try:
                self.client.close()
            except Exception:                       # noqa: BLE001
                pass
        client = paramiko.SSHClient()
        client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        client.connect(self.host, username=self.user, password=self.password,
                       timeout=8, banner_timeout=8)
        self.client = client
        return client

    def run(self, command, timeout=15):
        """Return (exit status or -1, combined output)."""
        for attempt in (0, 1):
            try:
                if not self.client:
                    self.connect()
                _, stdout, stderr = self.client.exec_command(command,
                                                            timeout=timeout)
                out = stdout.read().decode(errors="replace")
                err = stderr.read().decode(errors="replace")
                status = stdout.channel.recv_exit_status()
                return status, out + err
            except Exception:                       # noqa: BLE001
                self.client = None
                if attempt:
                    return -1, "ssh failed"
        return -1, "ssh failed"

    def read_file(self, path):
        status, out = self.run("cat %s" % path)
        return out if status == 0 else None

    def write_file(self, path, text, tmp="/tmp/oko-rgn-boxes.yaml"):
        try:
            if not self.client:
                self.connect()
            stdin, stdout, stderr = self.client.exec_command(
                "cat > %s" % tmp, timeout=15)
            stdin.write(text)
            stdin.channel.shutdown_write()
            status = stdout.channel.recv_exit_status()
            if status:
                return False, stderr.read().decode(errors="replace")
            status, out = self.run("cp %s %s" % (tmp, path))
            return status == 0, out
        except Exception as err:                    # noqa: BLE001
            self.client = None
            return False, str(err)

    def close(self):
        if self.client:
            try:
                self.client.close()
            except Exception:                       # noqa: BLE001
                pass
            self.client = None


def http(path, method="GET", timeout=HTTP_TIMEOUT):
    """Return (status, body).  status 0 means no answer (hang or refusal)."""
    req = urllib.request.Request("http://%s%s" % (HOST, path), method=method)
    token = base64.b64encode(
        ("%s:%s" % (HTTP_USER, HTTP_PASSWORD)).encode()).decode()
    req.add_header("Authorization", "Basic " + token)
    if method == "POST":
        req.data = b""
        req.add_header("Content-Type", "application/x-www-form-urlencoded")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.status, resp.read().decode(errors="replace")
    except urllib.error.HTTPError as err:
        return err.code, err.read().decode(errors="replace")
    except Exception as err:                        # noqa: BLE001
        return 0, str(err)


def parse_kv(text):
    values = {}
    for line in text.splitlines():
        if "=" in line:
            key, _, value = line.partition("=")
            values[key.strip()] = value.strip()
    return values


def sample(dut, target):
    """One measurement round: process metrics, kernel counters, HTTP state."""
    status, out = dut.run(SAMPLE_CMD)
    values = parse_kv(out)
    record = {
        "t": round(time.time(), 1),
        "alive": values.get("ALIVE") == "1",
        "fd": int(values.get("FD", -1)),
        "rss_kb": int(values.get("RSS_KB", -1)),
        "threads": int(values.get("THREADS", -1)),
        "uptime": int(values.get("UPTIME", -1)),
        "ssh_status": status,
    }
    for key in KERNEL_PATTERNS:
        record["k_" + key] = int(values.get("K_" + key, -1))
    path = "/api/%s" % target
    code, body = http(path)
    record["http"] = code
    record["boxes"] = None
    record["enable"] = None
    try:
        payload = json.loads(body)
        record["boxes"] = payload.get("boxes")
        record["enable"] = payload.get("enable")
        if payload.get("note"):
            record["note"] = payload["note"]
    except Exception:                               # noqa: BLE001
        if code and body:
            record["body"] = body[:200]
    return record


def set_yaml_boxes(dut, target, value):
    """Rewrite `<target>: boxes:` in /userdata/divinus.yaml, keep a backup."""
    path = "/userdata/divinus.yaml"
    text = dut.read_file(path)
    if text is None:
        return False, "cannot read %s" % path
    lines = text.splitlines(keepends=True)
    section = None
    changed = 0
    result = []
    for line in lines:
        stripped = line.strip()
        # A top-level `key:` line starts a new section.
        if line[:1] and not line[0].isspace() and stripped \
                and not stripped.startswith("#") and ":" in stripped:
            section = stripped.split(":", 1)[0]
        if section == target and stripped.startswith("boxes:"):
            indent = line[:len(line) - len(line.lstrip())]
            result.append("%sboxes: %s\n" % (indent,
                                             "true" if value else "false"))
            changed += 1
            continue
        result.append(line)
    if not changed:
        return False, "no boxes key in the %s section" % target
    backup = path + ".oko-rgn-boxes.bak"
    status, out = dut.run("cp %s %s" % (path, backup))
    if status:
        return False, "cannot back up the configuration: %s" % out.strip()
    ok, out = dut.write_file(path, "".join(result))
    return ok, "rewrote %d line(s)" % changed


def api_set_boxes(target, value):
    return http("/api/%s?boxes=%d" % (target, 1 if value else 0),
                method="POST")


def restart_app(dut):
    """Graceful restart (SIGHUP) and wait for the HTTP API to answer again."""
    code, _ = http("/api/cmd?restart", method="POST", timeout=6)
    for _ in range(30):
        time.sleep(1)
        if http("/api/status", timeout=3)[0] == 200:
            return True
    return code == 0


def summarize(samples, args):
    """Limits from the plan: fd baseline + 8, VmRSS +-2 MiB, no kernel hits."""
    if not samples:
        return {"ok": False, "reason": "no samples"}
    first = samples[0]
    last = samples[-1]
    dead = [s for s in samples if not s["alive"]]
    http_fail = [s for s in samples if s["http"] != 200]
    kernel = {}
    for key in KERNEL_PATTERNS:
        values = [s["k_" + key] for s in samples]
        kernel[key] = max(values) - min(values)
    fd_values = [s["fd"] for s in samples if s["fd"] >= 0]
    rss_values = [s["rss_kb"] for s in samples if s["rss_kb"] >= 0]
    summary = {
        "samples": len(samples),
        "seconds": round(last["t"] - first["t"], 1),
        "fd": {"first": first["fd"], "last": last["fd"],
               "min": min(fd_values) if fd_values else -1,
               "max": max(fd_values) if fd_values else -1},
        "rss_kb": {"first": first["rss_kb"], "last": last["rss_kb"],
                   "min": min(rss_values) if rss_values else -1,
                   "max": max(rss_values) if rss_values else -1},
        "kernel_delta": kernel,
        "dead_samples": len(dead),
        "http_errors": len(http_fail),
        "boxes_seen": sorted({str(s["boxes"]) for s in samples}),
        "ok": True,
        "reasons": [],
    }
    if dead:
        summary["ok"] = False
        summary["reasons"].append("divinus disappeared in %d sample(s)" % len(dead))
    if http_fail:
        summary["ok"] = False
        summary["reasons"].append("HTTP failed in %d sample(s)" % len(http_fail))
    # In restart mode each sample follows a fresh process: fd must stay flat,
    # while VmRSS alternates with the lazily loaded NPU model (12.7 or 19.9 MiB)
    # and is reported but not used as a failure criterion.
    restart_mode = getattr(args, "mode", "") == "restart"
    fd_spread = max(fd_values) - min(fd_values) if fd_values else 0
    rss_spread = max(rss_values) - min(rss_values) if rss_values else 0
    if fd_values and fd_spread > args.fd_limit:
        summary["ok"] = False
        summary["reasons"].append("fd drift %d, limit %d" % (
            fd_spread, args.fd_limit))
    if rss_values and not restart_mode and rss_spread > args.rss_limit:
        summary["ok"] = False
        summary["reasons"].append("VmRSS drift %d KiB, limit %d" % (
            rss_spread, args.rss_limit))
    for key, delta in kernel.items():
        if delta > 0:
            summary["ok"] = False
            summary["reasons"].append("kernel counter %s +%d" % (key, delta))
    return summary


def report(mode, args, samples, summary, extra):
    stamp = time.strftime("%Y%m%d-%H%M%S")
    directory = os.path.join(OUTPUT, "rgn")
    os.makedirs(directory, exist_ok=True)
    path = os.path.join(directory, "%s-%s-%s.json" % (mode, args.target, stamp))
    with open(path, "w", encoding="utf-8") as out:
        json.dump({"mode": mode, "target": args.target,
                   "argv": sys.argv[1:], "summary": summary,
                   "extra": extra, "samples": samples}, out, indent=1)
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--mode", required=True,
                        choices=["observe", "steady", "cycle", "restart",
                                 "config"])
    parser.add_argument("--target", default="yolo", choices=["yolo", "ivs"])
    parser.add_argument("--count", type=int, default=200,
                        help="cycle mode: number of off->on A/B rounds")
    parser.add_argument("--pause", type=float, default=1.0,
                        help="cycle mode: seconds between switch and sample")
    parser.add_argument("--seconds", type=int, default=1800,
                        help="observe/steady mode: run duration")
    parser.add_argument("--interval", type=float, default=10.0,
                        help="seconds between samples")
    parser.add_argument("--settle", type=float, default=12.0,
                        help="restart mode: settle time before sampling")
    parser.add_argument("--value", default="true", choices=["true", "false"],
                        help="config mode: value for the boxes key")
    parser.add_argument("--no-restart", action="store_true",
                        help="config mode: do not SIGHUP after the edit")
    parser.add_argument("--restore", action="store_true",
                        help="restore the .oko-rgn-boxes.bak file and restart")
    parser.add_argument("--fd-limit", type=int, default=8)
    parser.add_argument("--rss-limit", type=int, default=2048,
                        help="allowed VmRSS span, KiB")
    parser.add_argument("--host", default=HOST)
    parser.add_argument("--password", default=PASSWORD)
    args = parser.parse_args()

    dut = Dut(args.host, USER, args.password)
    samples = []
    extra = {}
    try:
        status, out = dut.run("pidof divinus")
        if status or not out.strip():
            print("divinus is not running: %s" % out.strip(), file=sys.stderr)
            return 2

        if args.mode == "config":
            if args.restore:
                backup = "/userdata/divinus.yaml.oko-rgn-boxes.bak"
                status, out = dut.run("cp %s /userdata/divinus.yaml" % backup)
                ok = status == 0
                message = "restored %s" % backup if ok else out.strip()
            else:
                ok, message = set_yaml_boxes(dut, args.target,
                                             args.value == "true")
            extra["config"] = message
            print("%s: %s" % ("ok" if ok else "failed", message))
            if not ok:
                return 2
            if not args.no_restart:
                extra["restart"] = restart_app(dut)
                print("restart: %s" % extra["restart"])
            return 0

        if args.mode == "steady":
            if args.target == "ivs":
                ok, message = set_yaml_boxes(dut, "ivs", True)
                extra["config"] = message
                print("ivs.boxes=true: %s" % message)
                if not ok:
                    return 2
                extra["restart"] = restart_app(dut)
                print("restart: %s" % extra["restart"])
            else:
                code, body = api_set_boxes("yolo", True)
                extra["enable_http"] = code
                print("yolo boxes=1: HTTP %d %s" % (code, body[:120]))
                if code != 200:
                    return 2
            samples.append(sample(dut, args.target))

        if args.mode == "cycle":
            for index in range(args.count):
                for value in (1, 0):
                    code, body = api_set_boxes(args.target, value)
                    try:
                        applied = json.loads(body).get("applied", True)
                    except Exception:                  # noqa: BLE001
                        applied = True
                    if code == 200 and not applied:
                        print("%s boxes=%d was not applied live: the target "
                              "uses boxes_mode=restart" % (args.target, value),
                              file=sys.stderr)
                        extra["not_live"] = True
                        samples.append(sample(dut, args.target))
                        summary = summarize(samples, args)
                        path = report("cycle", args, samples, summary, extra)
                        print(json.dumps(summary, indent=1))
                        print("report: %s" % path)
                        return 2
                    if code != 200:
                        extra.setdefault("refused", []).append(
                            {"cycle": index, "value": value, "http": code,
                             "body": body[:200]})
                        print("boxes=%d refused: HTTP %d %s"
                              % (value, code, body[:120]))
                        samples.append(sample(dut, args.target))
                        extra["aborted_at"] = index
                        summary = summarize(samples, args)
                        path = report("cycle", args, samples, summary, extra)
                        print(json.dumps(summary, indent=1))
                        print("report: %s" % path)
                        return 1
                if index % 10 == 0:
                    print("cycle %d/%d" % (index + 1, args.count), flush=True)
                if not samples or time.time() - samples[-1]["t"] >= args.interval:
                    record = sample(dut, args.target)
                    samples.append(record)
                    if record["http"] != 200:
                        print("lost the API at cycle %d: %s"
                              % (index, record), file=sys.stderr)
                        break
                time.sleep(args.pause)

        if args.mode == "restart":
            # Graceful SIGHUP restarts with the overlay attached: the region
            # must be destroyed before the MPP channels go away, otherwise the
            # exit path oopses (KI-063).
            for index in range(args.count):
                if not restart_app(dut):
                    extra["dead_after_restart"] = index + 1
                    print("restart %d: the API did not come back"
                          % (index + 1), file=sys.stderr)
                    samples.append(sample(dut, args.target))
                    break
                time.sleep(args.settle)
                record = sample(dut, args.target)
                samples.append(record)
                print("restart %d/%d fd=%s rss=%s boxes=%s" % (
                    index + 1, args.count, record["fd"], record["rss_kb"],
                    record["boxes"]), flush=True)
                if record["http"] != 200:
                    print("restart %d: API answered %s"
                          % (index + 1, record["http"]), file=sys.stderr)
                    break
                time.sleep(args.pause)

        if args.mode == "observe":
            deadline = time.time() + args.seconds
            while time.time() < deadline:
                record = sample(dut, args.target)
                samples.append(record)
                print("%s fd=%s rss=%s http=%s boxes=%s" % (
                    time.strftime("%H:%M:%S"), record["fd"],
                    record["rss_kb"], record["http"], record["boxes"]),
                    flush=True)
                if not record["alive"] or record["http"] == 0:
                    print("divinus stopped answering; stopping the observer",
                          file=sys.stderr)
                    break
                time.sleep(min(args.interval,
                               max(0.5, deadline - time.time())))

        summary = summarize(samples, args)
        path = report(args.mode, args, samples, summary, extra)
        print(json.dumps(summary, indent=1))
        print("report: %s" % path)
        return 0 if summary["ok"] else 1
    finally:
        dut.close()


if __name__ == "__main__":
    sys.exit(main())