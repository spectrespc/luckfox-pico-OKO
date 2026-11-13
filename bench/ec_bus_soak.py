#!/usr/bin/env python3
"""Soak the STM32 EC I2C bus while the DUT reboots.

Reproduces the "SCL hold by slave" window: the RV1106 master disappears in
the middle of an EC read, the STM32 slave-transmitter keeps SCL low until
the EC-side timer releases it.  Everything runs over SSH on the DUT; no
logic analyser is needed.

    bench/run.sh ec_bus_soak.py --mode reboot --iterations 20
    bench/run.sh ec_bus_soak.py --mode read-window --iterations 50
    bench/run.sh ec_bus_soak.py --mode detect --iterations 20
    bench/run.sh ec_bus_soak.py --mode hold-scl
    bench/run.sh ec_bus_soak.py --mode all --iterations 20

Modes:

    reboot       plain reboot, then count SCL-hold messages in the fresh dmesg
    read-window  start an EC read and reboot after --delay-ms; sweeps the
                 delay over 0..150 ms to hit the ADDR(read) window
    detect       keep i2cdetect running across the reboot
    hold-scl     arm CMD_DEBUG_HOLD_SCL (0x29, DEBUG_BUILD only), read once
                 (expected to time out), then read CMD_GET_DEBUG (0x28) and
                 check that the line was released and the next frame passes
    all          reboot + read-window + detect + hold-scl

Artifacts (JSON report and a human-readable log) go to output/bench/.
A run fails when dmesg contains "SCL hold by slave" or when the EC does not
answer after recovery.  The helper reconnects over SSH after each reboot;
the DUT address is taken from OKO_DUT_HOST or --host.

Note: hold-scl needs firmware built with -DEC_DEBUG_BUILD=ON.  In a normal
build 0x28/0x29 answer 0xFF (ST_ERR_UNSUPPORTED).
"""
import argparse
import json
import os
import re
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

import paramiko

ROOT = Path(__file__).resolve().parent.parent
OUT = Path(os.environ.get("OKO_BENCH_OUT", ROOT / "output" / "bench"))
DEFAULT_HOST = os.environ.get("OKO_DUT_HOST", "192.168.52.97")
DEFAULT_USER = os.environ.get("OKO_DUT_USER", "root")
DEFAULT_PASSWORD = os.environ.get("OKO_DUT_PASSWORD", "luckfox")
EC_ADDR = "0x42"

SCL_HOLD = "SCL hold by slave"
I2C_TIMEOUT = "timeout, ipd: 0x80"


class Dut:
    def __init__(self, host, user, password):
        self.host = host
        self.user = user
        self.password = password
        self.client = None

    def connect(self, timeout=10):
        self.close()
        client = paramiko.SSHClient()
        client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        client.connect(self.host, username=self.user, password=self.password,
                       timeout=timeout, banner_timeout=timeout)
        self.client = client

    def close(self):
        if self.client is not None:
            self.client.close()
            self.client = None

    def run(self, command, timeout=30):
        if self.client is None:
            self.connect()
        _, stdout, stderr = self.client.exec_command(command, timeout=timeout)
        out = stdout.read().decode(errors="replace") + stderr.read().decode(errors="replace")
        status = stdout.channel.recv_exit_status()
        return status, out

    def reboot(self, timeout=180):
        """Reboot and wait until SSH answers again (also waits out DHCP)."""
        try:
            self.run("sync; reboot -f >/dev/null 2>&1 &", timeout=5)
        except Exception:  # noqa: BLE001 - the connection dies with the SOC
            pass
        self.close()
        deadline = time.time() + timeout
        last = None
        while time.time() < deadline:
            time.sleep(1.0)
            try:
                self.connect(timeout=5)
                status, _ = self.run("echo ready", timeout=5)
                if status == 0 and self.client is not None:
                    return True
            except Exception as exc:  # noqa: BLE001 - keep polling
                last = exc
        raise RuntimeError("DUT did not come back over SSH: %s" % last)


def dmesg_counts(dut):
    _, out = dut.run("dmesg | grep -c '%s' || true" % SCL_HOLD)
    scl = int(re.sub(r"\D", "", out) or 0)
    _, out = dut.run("dmesg | grep -c '%s' || true" % I2C_TIMEOUT)
    timeout = int(re.sub(r"\D", "", out) or 0)
    return scl, timeout


def ec_info(dut):
    status, out = dut.run("/oem/usr/bin/oko-ec info", timeout=20)
    return status == 0 and "device_type" in out, out.strip()


def parse_i2c_dump(text):
    values = []
    for token in text.split():
        token = token.strip()
        if token.lower().startswith("0x"):
            try:
                values.append(int(token, 16))
            except ValueError:
                pass
    return values


def debug_snapshot(dut, bus):
    # The EC prepares the reply after STOPF, so the write and the read must
    # be separate transactions with a delay (same contract as oko-ec).
    cmd = "i2ctransfer -y %s w3@%s 0x01 0x00 0x28" % (bus, EC_ADDR)
    dut.run(cmd, timeout=20)
    time.sleep(0.1)
    _, out = dut.run("i2ctransfer -y %s r63@%s" % (bus, EC_ADDR), timeout=20)
    data = parse_i2c_dump(out)

    if len(data) < 3:
        return None

    payload = data[3:3 + 60]
    if len(payload) < 60:
        return {"raw": data, "truncated": True}

    def u32(i):
        return (payload[i] | (payload[i + 1] << 8) | (payload[i + 2] << 16) |
                (payload[i + 3] << 24))

    def u16(i):
        return payload[i] | (payload[i + 1] << 8)

    return {
        "addr_events": u32(0),
        "read_addr_events": u32(4),
        "txis_events": u32(8),
        "stop_fires": u32(12),
        "nack_fires": u32(16),
        "err_fires": u32(20),
        "timeout_fires": u32(24),
        "bus_hang_events": u32(28),
        "bus_hang_ms_max": u32(32),
        "irq_masked_us_max": u32(36),
        "paused_ms_max": u32(40),
        "isr_at_read_addr": u16(44),
        "cr1_at_read_addr": u16(46),
        "hang_isr": u16(48),
        "hang_cr1": u16(50),
        "bkp_state": u32(52),
        "bkp_flags": u32(56),
    }


def run_reboot(dut, bus, delay_ms):
    if delay_ms is None:
        dut.reboot()
    else:
        seconds = delay_ms / 1000.0
        # Start a read in the background, reboot while it is in flight.
        cmd = ("setsid sh -c '(i2ctransfer -y %s w3@%s 0x01 0x00 0x01 r16@%s "
               ">/dev/null 2>&1; true) & sleep %.3f; reboot -f' "
               ">/dev/null 2>&1 < /dev/null &"
               % (bus, EC_ADDR, EC_ADDR, seconds))
        dut.run(cmd, timeout=5)
        dut.close()
        deadline = time.time() + 180
        while time.time() < deadline:
            time.sleep(1.0)
            try:
                dut.connect(timeout=5)
                break
            except Exception:  # noqa: BLE001
                continue
        else:
            raise RuntimeError("DUT did not come back over SSH")

    scl, timeout = dmesg_counts(dut)
    ok, info = ec_info(dut)
    return {"scl_hold": scl, "i2c_timeouts": timeout, "ec_ok": ok, "ec_info": info}


def run_detect(dut, bus):
    cmd = ("setsid sh -c '(while true; do i2cdetect -y -r %s >/dev/null 2>&1; done) & "
           "sleep 0.05; reboot -f' >/dev/null 2>&1 < /dev/null &" % bus)
    dut.run(cmd, timeout=5)
    dut.close()
    deadline = time.time() + 180
    while time.time() < deadline:
        time.sleep(1.0)
        try:
            dut.connect(timeout=5)
            break
        except Exception:  # noqa: BLE001
            continue
    else:
        raise RuntimeError("DUT did not come back over SSH")

    scl, timeout = dmesg_counts(dut)
    ok, info = ec_info(dut)
    return {"scl_hold": scl, "i2c_timeouts": timeout, "ec_ok": ok, "ec_info": info}


def run_hold_scl(dut, bus):
    _, before = dut.run("dmesg | grep -c '%s' || true" % SCL_HOLD)
    before = int(re.sub(r"\D", "", before) or 0)

    arm = "i2ctransfer -y %s w4@%s 0x02 0x00 0x29 0x01" % (bus, EC_ADDR)
    status, out = dut.run(arm, timeout=20)

    # This read is expected to hit the stretched SCL and time out; the EC
    # must release the line without a reset.
    dut.run("i2ctransfer -y %s r63@%s 2>/dev/null || true" % (bus, EC_ADDR), timeout=20)
    time.sleep(0.05)

    # Disarm before probing again: while hold is armed every read is
    # answered with 0xFF filler, which would hide the recovery.
    dut.run("i2ctransfer -y %s w4@%s 0x02 0x00 0x29 0x00 || true" % (bus, EC_ADDR),
            timeout=20)
    time.sleep(0.1)

    ok, info = ec_info(dut)

    if not ok:
        time.sleep(0.2)
        ok, info = ec_info(dut)

    debug = debug_snapshot(dut, bus)

    _, after = dut.run("dmesg | grep -c '%s' || true" % SCL_HOLD)
    after = int(re.sub(r"\D", "", after) or 0)

    released = ok and (after == before)

    return {
        "arm_status": status,
        "arm_output": out.strip(),
        "scl_hold_delta": after - before,
        "ec_ok_after": ok,
        "ec_info": info,
        "debug": debug,
        "pass": bool(released and debug and
                     (debug.get("timeout_fires", 0) >= 1 or
                      debug.get("bus_hang_events", 0) >= 1) and
                     debug.get("bus_hang_ms_max", 0) <= 40),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--mode", choices=["reboot", "read-window", "detect", "hold-scl", "all"],
                        default="reboot")
    parser.add_argument("--iterations", type=int, default=10)
    parser.add_argument("--delay-ms", type=int, default=None,
                        help="read-window: fixed delay; default sweeps 0..150 ms")
    parser.add_argument("--bus", default=os.environ.get("OKO_EC_BUS", "3"),
                        help="i2c-tools bus number (default 3 = /dev/i2c-3)")
    parser.add_argument("--host", default=DEFAULT_HOST)
    parser.add_argument("--user", default=DEFAULT_USER)
    parser.add_argument("--password", default=DEFAULT_PASSWORD)
    args = parser.parse_args()

    OUT.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    report_path = OUT / ("ec_bus_soak-%s.json" % stamp)
    log_path = OUT / ("ec_bus_soak-%s.log" % stamp)

    dut = Dut(args.host, args.user, args.password)
    dut.connect()

    results = []
    failures = 0

    def record(mode, index, delay_ms, item):
        nonlocal failures
        item.update({"mode": mode, "iteration": index, "delay_ms": delay_ms})
        results.append(item)
        bad = item.get("scl_hold", 0) > 0 or not item.get("ec_ok", False)
        if item.get("pass") is False:
            bad = True
        if bad:
            failures += 1
        line = "%s #%d delay=%s scl_hold=%s timeouts=%s ec_ok=%s" % (
            mode, index, delay_ms, item.get("scl_hold"), item.get("i2c_timeouts"),
            item.get("ec_ok"))
        print(line)
        with log_path.open("a") as fh:
            fh.write(line + "\n")
        return item

    try:
        if args.mode in ("reboot", "all"):
            for i in range(args.iterations):
                record("reboot", i, None, run_reboot(dut, args.bus, None))

        if args.mode in ("read-window", "all"):
            for i in range(args.iterations):
                if args.delay_ms is not None:
                    delay = args.delay_ms
                else:
                    # Sweep 0..150 ms so at least some runs land inside the
                    # ADDR(read) / first-byte window.
                    delay = int(round(150.0 * i / max(1, args.iterations - 1)))
                record("read-window", i, delay, run_reboot(dut, args.bus, delay))

        if args.mode in ("detect", "all"):
            for i in range(args.iterations):
                record("detect", i, None, run_detect(dut, args.bus))

        if args.mode in ("hold-scl", "all"):
            item = run_hold_scl(dut, args.bus)
            item["scl_hold"] = item.get("scl_hold_delta", 0)
            item["i2c_timeouts"] = 0
            record("hold-scl", 0, None, item)
    finally:
        dut.close()

    summary = {
        "host": args.host,
        "bus": args.bus,
        "finished": datetime.now(timezone.utc).isoformat(),
        "iterations": len(results),
        "failures": failures,
        "results": results,
    }
    report_path.write_text(json.dumps(summary, indent=2, ensure_ascii=False))
    print("report: %s" % report_path)

    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())