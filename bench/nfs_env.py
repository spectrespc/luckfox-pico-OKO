#!/usr/bin/env python3
"""Switch a stock (SPI NAND) DUT between the ubifs root and an NFS root.

The U-Boot env variable `sys_bootargs` is appended to `bootargs` on every
boot, so the root filesystem is selected with `fw_setenv` alone — no kernel
reflash.  The stock boot.img already carries CONFIG_IP_PNP/ROOT_NFS.

Modes:
    status   print `sys_bootargs` and the recognised mode (nand/nfs)
    on       set the NFS bootargs, reboot and verify /, /oem, /userdata
    off      set the NAND bootargs, reboot and verify root=ubi0 + ubifs

Addresses and the CMA size match TESTBENCH.md §5 and are the same
environment variables that bench/netboot.py uses, so there is a single
source of truth:

    OKO_NFS_IP     192.168.52.150   static DUT address in NFS mode
    OKO_TFTP_HOST  192.168.52.200   NFS/TFTP server
    OKO_GATEWAY    192.168.52.254
    OKO_NFSROOT    192.168.52.200:/srv/nfs/rootfs
    OKO_CMA_SIZE   66M              must match RK_BOOTARGS_CMA_SIZE

Examples:
    bench/run.sh nfs_env.py status
    bench/run.sh nfs_env.py on  --host 192.168.52.97
    bench/run.sh nfs_env.py off --host 192.168.52.150
    bench/run.sh nfs_env.py on  --no-reboot
"""
import argparse
import os
import re
import shlex
import sys
import time
from pathlib import Path

import paramiko

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUT = ROOT / "output" / "bench"
ENV_FILE = ROOT / "output" / "image" / ".env.txt"

NFS_IP = os.environ.get("OKO_NFS_IP", "192.168.52.150")
SERVER_IP = os.environ.get("OKO_TFTP_HOST", "192.168.52.200")
GATEWAY = os.environ.get("OKO_GATEWAY", "192.168.52.254")
NETMASK = "255.255.255.0"
NFSROOT = os.environ.get("OKO_NFSROOT", f"{SERVER_IP}:/srv/nfs/rootfs")
CMA = os.environ.get("OKO_CMA_SIZE", "66M")
PASSWORD = os.environ.get("OKO_DUT_PASSWORD", "luckfox")
USER = os.environ.get("OKO_DUT_USER", "root")

NAND_BOOTARGS = f"ubi.mtd=6 root=ubi0:rootfs rootfstype=ubifs rk_dma_heap_cma={CMA}"
NFS_BOOTARGS = (
    f"root=/dev/nfs rw nfsroot={NFSROOT},vers=3,proto=tcp,nolock "
    f"ip={NFS_IP}:{SERVER_IP}:{GATEWAY}:{NETMASK}:luckfox:eth0:off "
    f"rk_dma_heap_cma={CMA}"
)

MOUNT_RE = re.compile(r"^(\S+) on (\S+) type (\S+)")

log_file = None


def log(message):
    print(message)
    if log_file:
        log_file.write(message + "\n")
        log_file.flush()


def connect(host, timeout=10):
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(host, username=USER, password=PASSWORD,
                   timeout=timeout, banner_timeout=timeout)
    return client


def run(client, command, timeout=30):
    _, stdout, stderr = client.exec_command(command, timeout=timeout)
    out = stdout.read().decode(errors="replace") + stderr.read().decode(errors="replace")
    return stdout.channel.recv_exit_status(), out.strip()


def set_env(client, bootargs):
    status, out = run(client, f"fw_setenv sys_bootargs {shlex.quote(bootargs)}")
    if status:
        log(f"FAIL fw_setenv: {out}")
        return False
    status, out = run(client, "fw_printenv -n sys_bootargs")
    expected = " ".join(bootargs.split())
    actual = " ".join(out.split())
    if actual != expected:
        log(f"FAIL readback mismatch: {actual}")
        return False
    log(f"sys_bootargs set: {actual}")
    return True


def reboot_and_wait(hosts, timeout):
    """Reboot is issued by the caller; poll SSH on every candidate address."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        for host in hosts:
            try:
                client = connect(host, timeout=5)
            except Exception:
                continue
            log(f"SSH reachable at {host}")
            return client, host
        time.sleep(5)
    return None, None


def parse_mounts(client):
    _, out = run(client, "mount")
    mounts = {}
    for line in out.splitlines():
        match = MOUNT_RE.match(line)
        if match:
            mounts[match.group(2)] = (match.group(1), match.group(3))
    return mounts


def check(client, mode):
    """Return a list of failed checks for the expected boot mode."""
    failed = []
    _, cmdline = run(client, "cat /proc/cmdline")
    log(f"/proc/cmdline: {cmdline}")
    mounts = parse_mounts(client)

    if mode == "nfs":
        need = f"root=/dev/nfs"
        if need not in cmdline or f"ip={NFS_IP}:" not in cmdline:
            failed.append(f"cmdline misses '{need}' or 'ip={NFS_IP}:'")
        fstype = "nfs"
    else:
        if "root=ubi0:rootfs" not in cmdline:
            failed.append("cmdline misses 'root=ubi0:rootfs'")
        fstype = "ubifs"

    for point in ("/", "/oem", "/userdata"):
        source, fs = mounts.get(point, ("<missing>", "<missing>"))
        log(f"mount {point}: {source} type {fs}")
        if not fs.startswith(fstype):
            failed.append(f"{point} is {fs}, expected {fstype}")
        if mode == "nfs" and SERVER_IP not in source:
            failed.append(f"{point} source {source} is not {SERVER_IP}")

    return failed


def print_status(client):
    _, bootargs = run(client, "fw_printenv -n sys_bootargs")
    mode = "nfs" if ("root=/dev/nfs" in bootargs or "nfsroot=" in bootargs) else "nand"
    log(f"sys_bootargs: {bootargs}")
    log(f"mode: {mode}")
    if ENV_FILE.is_file():
        for line in ENV_FILE.read_text(errors="replace").splitlines():
            if line.startswith("sys_bootargs="):
                sample = " ".join(line.split("=", 1)[1].split())
                if sample != " ".join(NAND_BOOTARGS.split()):
                    log(f"WARN nand default in {ENV_FILE.name} differs: {sample}")
                break
    return 0


def main():
    global log_file
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("mode", choices=["status", "on", "off"])
    parser.add_argument("--host", default=os.environ.get("OKO_DUT_HOST", "192.168.52.150"),
                        help="current DUT address used to run fw_setenv")
    parser.add_argument("--no-reboot", action="store_true",
                        help="only write sys_bootargs, do not reboot or verify")
    parser.add_argument("--timeout", type=float, default=120.0,
                        help="seconds to wait for SSH after reboot")
    parser.add_argument("--out", default=os.environ.get("OKO_BENCH_OUT", str(DEFAULT_OUT)),
                        help="artifact directory (default: output/bench)")
    args = parser.parse_args()

    out_dir = Path(args.out).expanduser()
    out_dir.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    log_file = open(out_dir / f"nfs-env-{stamp}.log", "w")
    log(f"nfs_env.py {args.mode} host={args.host} at {stamp}")

    try:
        client = connect(args.host)
    except Exception as exc:
        log(f"FAIL cannot connect to {args.host}: {exc}")
        return 2

    if args.mode == "status":
        rc = print_status(client)
        client.close()
        return rc

    target = "nfs" if args.mode == "on" else "nand"
    bootargs = NFS_BOOTARGS if target == "nfs" else NAND_BOOTARGS
    if not set_env(client, bootargs):
        client.close()
        return 1

    if args.no_reboot:
        log("--no-reboot: sys_bootargs written, reboot skipped")
        client.close()
        return 0

    log(f"rebooting into {target} mode")
    try:
        run(client, "sh -c 'sync; (sleep 2; reboot) &'", timeout=5)
    except Exception:
        pass
    client.close()

    hosts = [NFS_IP, args.host] if target == "nfs" else [args.host, NFS_IP]
    hosts = list(dict.fromkeys(hosts))
    client, host = reboot_and_wait(hosts, args.timeout)
    if client is None:
        log(f"FAIL no SSH within {args.timeout:.0f}s on {', '.join(hosts)}")
        return 1

    failed = check(client, target)
    if target == "nfs":
        _, busybox = run(client, "busybox | head -1")
        log(f"busybox: {busybox}")
    client.close()

    if failed:
        for item in failed:
            log(f"FAIL {item}")
        log(f"RESULT: FAIL ({target})")
        return 1
    log(f"RESULT: PASS ({target} root on {host})")
    return 0


if __name__ == "__main__":
    sys.exit(main())