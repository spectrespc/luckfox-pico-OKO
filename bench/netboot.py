#!/usr/bin/env python3
"""Drive U-Boot on the DUT over UART: catch, reboot or send one command.

Modes:
    catch    spam Ctrl-C until the U-Boot prompt appears, then run the netboot
             commands (ipaddr/serverip/sys_bootargs, tftp boot.img, boot_fit)
    reboot   log in to the running system, reboot it and catch U-Boot itself
    cmd      send a single command to an already reachable U-Boot prompt

The serial port defaults to the last /dev/ttyACM* device (override with --port
or OKO_SERIAL_PORT).  The U-Boot network parameters match TESTBENCH.md §5 and
can be overridden with OKO_DUT_HOST / OKO_TFTP_HOST / OKO_GATEWAY / OKO_NFSROOT.

Examples:
    bench/run.sh netboot.py catch          # start, then reset the DUT by hand
    bench/run.sh netboot.py reboot         # reboot from the running system
    bench/run.sh netboot.py cmd 'printenv ipaddr'
"""
import argparse
import glob
import os
import sys
import time
from pathlib import Path

import serial

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUT = ROOT / "output" / "bench"

DUT_IP = os.environ.get("OKO_DUT_HOST", "192.168.52.150")
SERVER_IP = os.environ.get("OKO_TFTP_HOST", "192.168.52.200")
GATEWAY = os.environ.get("OKO_GATEWAY", "192.168.52.254")
NETMASK = "255.255.255.0"
NFSROOT = os.environ.get("OKO_NFSROOT", f"{SERVER_IP}:/srv/nfs/rootfs")
PASSWORD = os.environ.get("OKO_DUT_PASSWORD", "luckfox")

SYS_BOOTARGS = (
    "root=/dev/nfs rw "
    f"nfsroot={NFSROOT},vers=3,proto=tcp,nolock "
    f"ip={DUT_IP}:{SERVER_IP}:{GATEWAY}:{NETMASK}:luckfox:eth0:off "
    "rk_dma_heap_cma=66M"
)

NETBOOT_CMDS = [
    f"setenv ipaddr {DUT_IP}",
    f"setenv netmask {NETMASK}",
    f"setenv serverip {SERVER_IP}",
    f"setenv gatewayip {GATEWAY}",
    f"setenv sys_bootargs '{SYS_BOOTARGS}'",
    "printenv sys_bootargs",
    f"ping {SERVER_IP}",
    "tftp ${ramdisk_addr_r} boot.img",
    "boot_fit ${ramdisk_addr_r}",
]


def default_port():
    return os.environ.get("OKO_SERIAL_PORT") or (sorted(glob.glob("/dev/ttyACM*")) or [None])[-1]


class Session:
    """Serial session that mirrors every byte to a timestamped log file."""

    def __init__(self, port_name, out_dir):
        out_dir.mkdir(parents=True, exist_ok=True)
        stamp = time.strftime("%Y%m%d-%H%M%S")
        self.path = out_dir / f"netboot-{stamp}.log"
        self.file = open(self.path, "wb", buffering=0)
        self.tail = b""
        self.serial = serial.Serial(port_name, 115200, timeout=0)
        self.write(f"\n[opened {port_name} at {time.strftime('%H:%M:%S')}]\n".encode())

    def write(self, data):
        try:
            self.serial.write(data)
        except Exception as exc:  # device unplugged mid-run
            self.file.write(f"\n[write-error {exc}]\n".encode())

    def log(self, data):
        self.file.write(data)
        self.tail = (self.tail + data)[-65536:]

    def read(self, size=16384):
        try:
            data = self.serial.read(size)
        except Exception as exc:
            self.log(f"\n[read-error {exc}]\n".encode())
            return b""
        if data:
            self.log(data)
        return data

    def close(self, footer=""):
        if footer:
            self.log(footer.encode())
        try:
            self.serial.close()
        except Exception:
            pass
        self.file.close()


def wait_prompt(session, timeout):
    """Spam Ctrl-C until the U-Boot prompt '=>' shows up in the recent output."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        session.write(b"\x03")
        time.sleep(0.02)
        if session.read() and b"=>" in session.tail[-8192:]:
            return True
    return False


def login(session, timeout=15):
    session.serial.reset_input_buffer()
    session.write(b"\r\n")
    deadline = time.time() + 3
    buf = b""
    while time.time() < deadline:
        data = session.read()
        if data:
            buf += data
            if b"login:" in buf or b"#" in buf:
                break
    if b"login:" in buf:
        session.write(b"root\r")
        time.sleep(0.3)
        session.write(PASSWORD.encode() + b"\r")
        deadline = time.time() + timeout
        while time.time() < deadline:
            if session.read() and b"#" in session.tail[-4096:]:
                return True
        return False
    return b"#" in buf


def send_netboot_commands(session):
    session.write(b"\r")
    time.sleep(0.8)
    session.read()
    session.log(b"\n[SENDING NETBOOT COMMANDS]\n")
    for cmd in NETBOOT_CMDS:
        session.log(b"\n>>> " + cmd.encode() + b"\n")
        session.write(cmd.encode() + b"\r")
        limit = 8.0 if cmd.startswith("tftp") else 5.0
        deadline = time.time() + limit
        while time.time() < deadline:
            if not session.read():
                time.sleep(0.03)
    session.log(b"\n[COMMANDS SENT]\n")


def send_uboot_command(session, command, wait=15.0):
    """Send one U-Boot command after the prompt was caught and log the reply."""
    session.write(b"\r")
    time.sleep(0.5)
    session.read()
    session.log(b"\n[SENDING U-BOOT COMMAND]\n")
    session.log(b"\n>>> " + command.encode() + b"\n")
    session.write(command.encode() + b"\r")
    deadline = time.time() + wait
    while time.time() < deadline:
        if not session.read():
            time.sleep(0.05)
    session.log(b"\n[COMMAND SENT]\n")


def wait_boot(session, timeout=180):
    deadline = time.time() + timeout
    markers = (b"login:", b"Please press Enter", b"root@luckfox")
    while time.time() < deadline:
        if session.read():
            tail = session.tail[-4096:]
            if any(marker in tail for marker in markers):
                session.log(b"\n[DETECTED SHELL]\n")
                return True
        else:
            time.sleep(0.05)
    session.log(b"\n[BOOT TIMEOUT]\n")
    return False


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("mode", choices=["catch", "reboot", "cmd"])
    parser.add_argument("command", nargs="?", help="single U-Boot command for mode 'cmd'")
    parser.add_argument("--port", default=default_port(), help="UART device (default: last /dev/ttyACM*)")
    parser.add_argument("--out", default=os.environ.get("OKO_BENCH_OUT", str(DEFAULT_OUT)),
                        help="artifact directory (default: output/bench)")
    parser.add_argument("--prompt-timeout", type=float, default=120.0,
                        help="seconds to wait for the U-Boot prompt")
    parser.add_argument("--boot-timeout", type=float, default=180.0,
                        help="seconds to wait for the Linux login prompt")
    parser.add_argument("--uboot-command", default=None,
                        help="for catch/reboot: send this one U-Boot command instead of the netboot sequence")
    args = parser.parse_args()

    if args.mode == "cmd" and not args.command:
        parser.error("mode 'cmd' requires a command string")
    if not args.port:
        print("no /dev/ttyACM* found; connect the DUT or pass --port", file=sys.stderr)
        return 1

    session = Session(args.port, Path(args.out).expanduser())
    rc = 0
    try:
        print(f"log: {session.path}")

        if args.mode == "reboot":
            if not login(session):
                print("cannot log in to the running system; use 'catch' and reset the DUT", file=sys.stderr)
                return 2
            print("shell ok, rebooting")
            session.write(b"reboot\r\n")
            time.sleep(0.3)

        if args.mode == "cmd":
            if not wait_prompt(session, args.prompt_timeout):
                print("U-Boot prompt not found; is the DUT sitting in the bootloader?", file=sys.stderr)
                return 3
            session.write((args.command + "\r").encode())
            deadline = time.time() + 5
            out = b""
            while time.time() < deadline:
                data = session.read()
                if data:
                    out += data
                else:
                    time.sleep(0.03)
            print(out.decode(errors="replace"))
            return 0

        if not wait_prompt(session, args.prompt_timeout):
            print("U-Boot prompt not found; reset the DUT or use mode 'reboot'", file=sys.stderr)
            return 3
        if args.uboot_command:
            print(f"U-Boot prompt detected, sending: {args.uboot_command}")
            send_uboot_command(session, args.uboot_command)
            return 0
        print("U-Boot prompt detected, sending netboot commands")
        send_netboot_commands(session)
        if not wait_boot(session, args.boot_timeout):
            print("boot did not reach a login prompt; see the log", file=sys.stderr)
            rc = 4
        else:
            print("netboot reached the login prompt")
    finally:
        session.close(f"\n[{args.mode} end {time.strftime('%H:%M:%S')}]\n")
    return rc


if __name__ == "__main__":
    sys.exit(main())