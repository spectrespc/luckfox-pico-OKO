#!/usr/bin/env python3
"""Run shell commands on the DUT over SSH (paramiko).

Host, user and password come from --host/--user/--password or from
OKO_DUT_HOST / OKO_DUT_USER / OKO_DUT_PASSWORD.  The defaults are the
lab parameters and the public vendor password from TESTBENCH.md.

Example:
    bench/run.sh dut.py 'cat /proc/uptime' 'ls -l /userdata'
"""
import argparse
import os
import sys

import paramiko


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("commands", nargs="+", help="commands to execute on the DUT")
    parser.add_argument("--host", default=os.environ.get("OKO_DUT_HOST", "192.168.52.150"))
    parser.add_argument("--user", default=os.environ.get("OKO_DUT_USER", "root"))
    parser.add_argument("--password", default=os.environ.get("OKO_DUT_PASSWORD", "luckfox"))
    parser.add_argument("--timeout", type=float, default=30.0, help="per-command timeout, seconds")
    args = parser.parse_args()

    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(args.host, username=args.user, password=args.password,
                   timeout=10, banner_timeout=10)

    rc = 0
    for cmd in args.commands:
        _, stdout, stderr = client.exec_command(cmd, timeout=args.timeout)
        out = stdout.read().decode(errors="replace") + stderr.read().decode(errors="replace")
        status = stdout.channel.recv_exit_status()
        print(f"$ {cmd}\n{out.rstrip()}")
        if status:
            rc = status

    client.close()
    return rc


if __name__ == "__main__":
    sys.exit(main())