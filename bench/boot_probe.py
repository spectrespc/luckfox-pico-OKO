#!/usr/bin/env python3
"""Start-up budget probe: divinus markers vs the vendor fastboot_client.

The divinus build writes one line per start-up stage to /dev/kmsg
("divinus-boot: <stage> dt=<us>"), so the kernel stamps every marker with
seconds since boot and the line also reaches the UART console and the SD
uart0.log.  This script reads the markers back over SSH, keeps the last
start-up run and prints the per-stage cost plus the main->first_frame total.

The vendor `fastboot_client` is built but never edited:
`make -C project/app/fastboot_client fastboot_client-build`.  It is copied
to /userdata/boot-probe and launched from the UART console with divinus
stopped; the script then collects its "[app]:" kernel markers, its stdout
log and the exit state, and restarts divinus afterwards.

    bench/run.sh boot_probe.py divinus [--restart]
    bench/run.sh boot_probe.py vendor [--seconds 20]
    bench/run.sh boot_probe.py marks

Artifacts: output/bench/boot/<timestamp>-<mode>.txt
Environment: OKO_DUT_HOST / OKO_DUT_PASSWORD, OKO_SERIAL_PORT.
"""
import argparse
import datetime
import os
import re
import sys
import time

import paramiko

import uart

HOST = os.environ.get("OKO_DUT_HOST", "192.168.52.33")
PASSWORD = os.environ.get("OKO_DUT_PASSWORD", "luckfox")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(os.environ.get("OKO_BENCH_OUT", os.path.join(ROOT, "output", "bench")), "boot")
DIVINUS = os.path.join(ROOT, "project/app/divinus/source/divinus")
VENDOR = os.path.join(ROOT, "project/app/fastboot_client/out/bin/fastboot_client")
REMOTE_DIR = "/userdata/boot-probe"
REMOTE_VENDOR = REMOTE_DIR + "/fastboot_client"
REMOTE_LOG = REMOTE_DIR + "/vendor.log"

DMESG_DIVINUS = re.compile(r"divinus-boot: (\S+) dt=(\d+)us")
DMESG_VENDOR = re.compile(r"\[\s*(\d+\.\d+)\]\s+\[app-cli\]:\s*(.+)")


class Dut:
    def __init__(self):
        self.client = paramiko.SSHClient()
        self.client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        self.client.connect(HOST, username="root", password=PASSWORD, timeout=10)

    def run(self, cmd, timeout=60):
        _, out, err = self.client.exec_command(cmd, timeout=timeout)
        text = (out.read() + err.read()).decode(errors="replace")
        status = out.channel.recv_exit_status()
        return status, text

    def put(self, local, remote):
        sftp = self.client.open_sftp()
        try:
            sftp.put(local, remote)
        finally:
            sftp.close()

    def dmesg(self):
        return self.run("dmesg", timeout=30)[1]

    def close(self):
        self.client.close()


def console_login(port, password):
    port.reset_input_buffer()
    port.write(b"\r\n")
    buf = read_for(port, 3)
    if b"login:" in buf:
        port.write(b"root\r")
        wait_for(port, b"Password:", 5)
        port.write(password.encode() + b"\r")
        buf = wait_for(port, b"#", 10)
    else:
        buf = wait_for(port, b"#", 3) if b"#" not in buf else buf
    if b"#" not in buf:
        raise RuntimeError("console login failed: %r" % buf[-200:])


def read_for(port, seconds):
    end = time.time() + seconds
    buf = b""
    while time.time() < end:
        data = port.read(4096)
        if data:
            buf += data
            end = time.time() + 0.5
    return buf


def wait_for(port, token, seconds):
    end = time.time() + seconds
    buf = b""
    while time.time() < end:
        data = port.read(4096)
        if data:
            buf += data
            if token in buf:
                return buf
    return buf


def console_command(command, wait=3.0):
    """Run one shell command through the UART console (single control client)."""
    port = uart.open_uart()
    try:
        console_login(port, PASSWORD)
        port.write(command.encode() + b"\r\n")
        return read_for(port, wait).decode(errors="replace")
    finally:
        port.close()


def parse_divinus(dmesg_text):
    stages = [(m.group(1), int(m.group(2))) for m in DMESG_DIVINUS.finditer(dmesg_text)]
    starts = [i for i, (name, _) in enumerate(stages) if name == "main"]
    if not starts:
        return []
    return stages[starts[-1]:]


def parse_vendor(dmesg_text):
    marks = [(m.group(1), m.group(2).strip())
             for m in DMESG_VENDOR.finditer(dmesg_text)]
    starts = [i for i, (_, name) in enumerate(marks) if name == "main"]
    if not starts:
        return []
    return marks[starts[-1]:]


def report(title, lines):
    os.makedirs(OUT, exist_ok=True)
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    path = os.path.join(OUT, "%s-%s.txt" % (stamp, title))
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")
    for line in lines:
        print(line)
    print("\nartifact: %s" % path)
    return path


def divinus_mode(restart):
    dut = Dut()
    try:
        if restart:
            status, out = dut.run("kill -HUP $(pidof divinus)")
            if status != 0:
                print("divinus is not running: %s" % out.strip())
                print(console_command("cd /userdata && /oem/usr/bin/divinus >/dev/null 2>&1 &"))
            time.sleep(6)
        stages = parse_divinus(dut.dmesg())
    finally:
        dut.close()

    if not stages:
        print("no divinus-boot markers in dmesg; is the instrumented build deployed?")
        return 1

    lines = ["divinus start-up budget (main -> first_frame)"]
    total = 0
    for name, delta in stages:
        total += delta
        lines.append("  %-14s %8d us" % (name, delta))
    lines.append("  %-14s %8d us  (%.3f s)" % ("TOTAL", total, total / 1e6))
    report("divinus", lines)
    return 0


def vendor_mode(seconds):
    if not os.path.exists(VENDOR):
        print("missing %s: run make -C project/app/fastboot_client "
              "fastboot_client-build" % VENDOR)
        return 1

    dut = Dut()
    dmesg, log, alive = "", "", False
    try:
        dut.run("killall divinus")
        time.sleep(2)
        dut.run("killall -9 divinus")
        dut.run("mkdir -p %s" % REMOTE_DIR)
        dut.put(VENDOR, REMOTE_VENDOR)
        dut.run("chmod 755 %s" % REMOTE_VENDOR)
        dut.run("rm -f %s" % REMOTE_LOG)

        launch = ("cd %s && ./fastboot_client > vendor.log 2>&1 &" % REMOTE_DIR)
        console_command(launch, wait=2.0)
        time.sleep(seconds)

        alive = dut.run("pidof fastboot_client")[0] == 0
        dmesg = dut.dmesg()
        log = dut.run("cat %s" % REMOTE_LOG)[1]
    finally:
        # Always leave the DUT in the product state: no probe binary, no
        # leftover log, and divinus running from /userdata again.
        dut.run("killall fastboot_client")
        dut.run("rm -rf %s" % REMOTE_DIR)
        dut.close()
        console_command("cd /userdata && /oem/usr/bin/divinus >/dev/null 2>&1 &",
                        wait=2.0)

    marks = parse_vendor(dmesg)
    lines = ["vendor fastboot_client probe (%s, %s)" %
             ("still running at read time" if alive else "exited", HOST)]
    if marks:
        base = float(marks[0][0])
        lines.append("kernel markers (relative to main):")
        for stamp, name in marks:
            lines.append("  +%8.3f ms  %s" % ((float(stamp) - base) * 1000.0, name))
        missing = [stage for stage in ("bind chn 1", "aiq start")
                   if stage not in [name for _, name in marks]]
        if missing:
            lines.append("stages not reached: %s" % ", ".join(missing))
            lines.append("  the vendor demo binds VI->VENC right after VI and "
                         "relies on a preconfigured VI dev; vi_dev_init() is "
                         "commented out (fastboot_client.c:781)")
    else:
        lines.append("kernel markers: none (the binary did not reach klog())")
    lines.append("stdout log:")
    lines.extend("  " + line for line in log.strip().splitlines() or ["  <empty>"])
    report("vendor", lines)
    return 0


def marks_mode():
    dut = Dut()
    try:
        dmesg = dut.dmesg()
    finally:
        dut.close()
    lines = [line for line in dmesg.splitlines()
             if "divinus-boot:" in line or "[app]:" in line]
    report("marks", lines or ["no boot markers"])
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="mode", required=True)
    p_div = sub.add_parser("divinus", help="read the divinus start-up budget")
    p_div.add_argument("--restart", action="store_true",
                       help="SIGHUP divinus (or start it) before reading")
    p_vendor = sub.add_parser("vendor", help="run the vendor fastboot_client probe")
    p_vendor.add_argument("--seconds", type=float, default=20.0,
                          help="how long to let the vendor binary run (default 20 s)")
    sub.add_parser("marks", help="dump every boot marker from dmesg")
    args = parser.parse_args()

    if args.mode == "divinus":
        return divinus_mode(args.restart)
    if args.mode == "vendor":
        return vendor_mode(args.seconds)
    return marks_mode()


if __name__ == "__main__":
    sys.exit(main())