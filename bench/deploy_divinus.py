#!/usr/bin/env python3
"""Install the freshly built divinus binary into /oem on the DUT over SSH.

Fast iteration helper for the WebUI and streamer work: it copies
`project/app/divinus/source/divinus`, keeps the previous binary in
`/userdata/divinus.old` and sends SIGHUP (graceful exec, the PID stays).
It does not touch the flashed image; use `output/image/oem.img` and the
Maskrom procedure from TESTBENCH.md §7B for the release state.

    bench/run.sh deploy_divinus.py

Environment: OKO_DUT_HOST (default 192.168.52.97), OKO_DUT_PASSWORD
(default `luckfox`, the vendor root password).
"""
import os
import sys

import paramiko

HOST = os.environ.get("OKO_DUT_HOST", "192.168.52.97")
PASSWORD = os.environ.get("OKO_DUT_PASSWORD", "luckfox")
LOCAL = "project/app/divinus/source/divinus"
REMOTE_NEW = "/userdata/divinus.new"
REMOTE_BIN = "/oem/usr/bin/divinus"


def run(client, cmd):
    _, out, err = client.exec_command(cmd, timeout=60)
    text = (out.read() + err.read()).decode(errors="replace").strip()
    status = out.channel.recv_exit_status()
    print("$ %s\n%s" % (cmd, text))
    return status


def main():
    if not os.path.exists(LOCAL):
        print("missing %s: run make -C project/app/divinus first" % LOCAL)
        return 1

    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(HOST, username="root", password=PASSWORD, timeout=10)

    sftp = client.open_sftp()
    sftp.put(LOCAL, REMOTE_NEW)
    sftp.close()

    run(client, "cp %s /userdata/divinus.old" % REMOTE_BIN)
    run(client, "cp %s %s" % (REMOTE_NEW, REMOTE_BIN))
    run(client, "chmod 755 %s" % REMOTE_BIN)
    run(client, "ls -l %s" % REMOTE_BIN)
    run(client, "kill -HUP $(pidof divinus)")
    client.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())