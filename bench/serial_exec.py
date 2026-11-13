#!/usr/bin/env python3
"""Log in to the DUT over UART and run shell commands.

The console defaults to the ESP32 telnet-UART bridge,
telnet://192.168.52.17:23 (override with --port or OKO_SERIAL_PORT).  A
local adapter also works: pass /dev/ttyACM0 or a plain device path.  The
default credentials are the public vendor ones from TESTBENCH.md; the
password can be overridden with OKO_DUT_PASSWORD.

Examples:
    bench/run.sh serial_exec.py 'cat /proc/uptime'
    bench/run.sh serial_exec.py --wait 5 'ps | grep divinus'
    bench/run.sh serial_exec.py --port /dev/ttyACM1 'uname -a'
"""
import argparse
import os
import sys
import time

import uart


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


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("commands", nargs="+", help="shell commands to run on the DUT")
    parser.add_argument("--port", default=uart.default_target(),
                        help="telnet://host:port or a serial device (default: ESP32 bridge, env OKO_SERIAL_PORT)")
    parser.add_argument("--password", default=os.environ.get("OKO_DUT_PASSWORD", "luckfox"),
                        help="root password (default: vendor value, env OKO_DUT_PASSWORD)")
    parser.add_argument("--wait", type=float, default=2.0, help="seconds to collect output per command")
    args = parser.parse_args()

    try:
        port = uart.open_uart(args.port, timeout=0.2)
    except OSError as exc:
        print(f"cannot open {args.port}: {exc}", file=sys.stderr)
        return 1
    port.reset_input_buffer()
    port.write(b"\r\n")
    buf = wait_for(port, b"login:", 3)
    if b"login:" in buf:
        port.write(b"root\r")
        wait_for(port, b"Password:", 5)
        port.write(args.password.encode() + b"\r")
        buf = wait_for(port, b"#", 10)
        if b"#" not in buf:
            print("login failed:", buf[-500:].decode(errors="replace"), file=sys.stderr)
            port.close()
            return 2
    elif b"#" not in buf:
        buf = wait_for(port, b"#", 3)
        if b"#" not in buf:
            print("no shell; tail:", buf[-300:].decode(errors="replace"), file=sys.stderr)
            port.close()
            return 1

    rc = 0
    try:
        for cmd in args.commands:
            port.write(cmd.encode() + b"\r\n")
            time.sleep(0.4)
            out = read_for(port, args.wait)
            print(f"$ {cmd}\n{out.decode(errors='replace')}")
    except OSError as exc:
        print(f"console error: {exc}", file=sys.stderr)
        rc = 1
    finally:
        port.close()
    return rc


if __name__ == "__main__":
    sys.exit(main())