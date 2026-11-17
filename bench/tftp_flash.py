#!/usr/bin/env python3
"""Flash the SPI NAND over TFTP from the U-Boot prompt.

This is the default flashing mode on the bench: the DUT USB port is occupied
by the hub and Maskrom is not reachable from the host, while TFTP works over
Ethernet.  The images from `output/image/` are loaded into RAM and written to
their MTD partitions with the U-Boot `mtd` command; the board is then reset
into the freshly written NAND.

Steps for every component (boot, oem, rootfs; userdata with --userdata):
    tftp ${ramdisk_addr_r} <image>
    mtd erase <partition>
    mtd write <partition> ${ramdisk_addr_r} <loaded size>

The NAND bootargs are restored in the U-Boot env before the reset, so the
board comes back from SPI NAND even if it was booting an NFS root.

The DUT must reach the U-Boot prompt: the script reboots a running Linux
itself, but a board sitting in Maskrom/RKUART has to be power-cycled by hand.
Host services (TFTP/NFS) come from services/ — `tools/native/setup_netboot.sh`.

Examples:
    bench/run.sh tftp_flash.py                    # boot, oem, rootfs
    bench/run.sh tftp_flash.py --components boot  # only the kernel/DTB
    bench/run.sh tftp_flash.py --userdata         # also wipe factory userdata
    bench/run.sh tftp_flash.py --no-reset         # stay in U-Boot for checks
"""
import argparse
import hashlib
import os
import re
import sys
import time
from pathlib import Path

import netboot
import nfs_env

ROOT = Path(__file__).resolve().parents[1]
IMAGE_DIR = ROOT / "output" / "image"

# Fallback layout of the product image; the actual table is read from
# output/image/.env.txt (mtdparts=...), so the -AOV profile with its larger
# idblock works without editing this file.  U-Boot here has
# CONFIG_CMD_MTDPARTS off, so `mtd` works on the whole spi-nand device and
# the offset/size must be passed explicitly.
DEFAULT_PARTITIONS = {
    "env": (0x00000000, 0x00040000),
    "idblock": (0x00040000, 0x00040000),
    "uboot": (0x00080000, 0x00080000),
    "boot": (0x00100000, 0x00600000),
    "oem": (0x00700000, 0x02800000),
    "userdata": (0x02F00000, 0x02800000),
    "rootfs": (0x05700000, 0x0A800000),
}

SIZE_UNITS = {"K": 1024, "M": 1024 * 1024, "G": 1024 * 1024 * 1024}


def parse_size(text):
    text = text.strip()
    if text[-1:].upper() in SIZE_UNITS:
        return int(text[:-1], 0) * SIZE_UNITS[text[-1:].upper()]
    return int(text, 0)


def load_partitions():
    """Read the mtdparts line from output/image/.env.txt.

    Format: mtdparts=spi-nand0:256K(env),512K@256K(idblock),...
    An explicit @offset is honoured; otherwise the partition follows the
    previous one.  Returns the fallback table when the file is unavailable.
    """
    path = IMAGE_DIR / ".env.txt"
    try:
        text = path.read_text()
    except OSError:
        return dict(DEFAULT_PARTITIONS)
    match = re.search(r"mtdparts=[^:]+:(\S+)", text)
    if not match:
        return dict(DEFAULT_PARTITIONS)
    parts = {}
    offset = 0
    for item in match.group(1).split(","):
        m = re.fullmatch(r"([^@(]+)(?:@([^(]+))?\(([^)]+)\)", item)
        if not m:
            raise RuntimeError(f"cannot parse mtdparts item: {item}")
        size = parse_size(m.group(1))
        if m.group(2):
            offset = parse_size(m.group(2))
        parts[m.group(3)] = (offset, size)
        offset += size
    return parts


SIZE_RE = re.compile(r"Bytes transferred = (\d+)")

TFTP_READY_CMDS = [
    f"setenv ipaddr {netboot.DUT_IP}",
    f"setenv netmask {netboot.NETMASK}",
    f"setenv serverip {netboot.SERVER_IP}",
    f"setenv gatewayip {netboot.GATEWAY}",
    f"ping {netboot.SERVER_IP}",
]


def args_out_default():
    return Path(os.environ.get("OKO_BENCH_OUT", str(netboot.DEFAULT_OUT)))


def log(message):
    print(message, flush=True)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def uboot_cmd(session, command, timeout=60.0, prompt=b"=>"):
    """Send one U-Boot command and collect the output up to the next prompt.

    The prompt is accepted only after a short quiet period: Ctrl-C leftovers
    from wait_prompt() print "<INTERRUPT>" and would otherwise be mistaken
    for the command output.
    """
    session.serial.reset_input_buffer()
    session.write(command.encode() + b"\r")
    deadline = time.time() + timeout
    buf = b""
    last_data = time.time()
    while time.time() < deadline:
        data = session.read()
        if data:
            buf += data
            last_data = time.time()
        elif buf.rstrip().endswith(prompt) and time.time() - last_data > 0.3:
            break
        else:
            time.sleep(0.05)
    text = buf.decode(errors="replace")
    log(f"$ {command}\n{text.strip()}")
    return text


def uboot_sync(session):
    """Drain pending input and leave the console at a clean prompt."""
    session.serial.reset_input_buffer()
    for _ in range(3):
        session.write(b"\r")
        time.sleep(0.2)
        session.read()
    session.serial.reset_input_buffer()


def at_uboot_prompt(session, seconds=1.5):
    """True when a bare newline brings the U-Boot prompt back."""
    session.serial.reset_input_buffer()
    session.write(b"\r")
    deadline = time.time() + seconds
    buf = b""
    while time.time() < deadline:
        data = session.read()
        if data:
            buf += data
        else:
            time.sleep(0.05)
    return b"=>" in buf


def reboot_over_ssh(args):
    """Reboot the running system over SSH when the console login is unusable.

    The console can be left at a Password prompt or full of kernel output, and
    a U-Boot prompt that was caught earlier can be lost to autoboot; a fresh
    reboot issued from Linux while this session keeps hammering Ctrl-C is the
    reliable way into U-Boot.
    """
    import paramiko

    host = args.dut_host or os.environ.get("OKO_DUT_HOST", "192.168.52.33")
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    try:
        client.connect(host, username=args.dut_user,
                       password=args.dut_password, timeout=10,
                       banner_timeout=10)
        client.exec_command("reboot", timeout=10)
        time.sleep(0.5)
    finally:
        client.close()


def enter_uboot(session, prompt_timeout, args):
    """Reach the U-Boot prompt, rebooting a running Linux if needed."""
    if at_uboot_prompt(session):
        log("U-Boot prompt is already active")
        uboot_sync(session)
        return
    if netboot.login(session, timeout=10):
        log("linux shell ok, rebooting into U-Boot")
        # Start hammering Ctrl-C immediately: the U-Boot autoboot window is
        # only a second or two and a longer pause lets it boot Linux again.
        session.write(b"reboot\r\n")
        time.sleep(0.3)
    else:
        log("no console shell; rebooting over SSH and catching U-Boot")
        try:
            reboot_over_ssh(args)
        except Exception as exc:                   # noqa: BLE001 - report it
            log(f"SSH reboot failed: {exc}")
    if not netboot.wait_prompt(session, prompt_timeout):
        raise RuntimeError("U-Boot prompt not found: "
                           "power-cycle the DUT if it sits in Maskrom/RKUART")
    uboot_sync(session)
    log("U-Boot prompt ok")


def flash_component(session, device, name, image, address, timeout, partitions):
    offset, part_size = partitions[name]
    log(f"--- {name}: {image.name} -> {device} @ 0x{offset:x} (0x{part_size:x})")
    out = uboot_cmd(session, f"tftp {address} {image.name}", timeout=timeout)
    match = SIZE_RE.search(out)
    if not match:
        raise RuntimeError(f"tftp of {image.name} failed")
    size = int(match.group(1))
    if size != image.stat().st_size:
        raise RuntimeError(f"{image.name}: tftp got {size} bytes, "
                           f"expected {image.stat().st_size}")
    if size > part_size:
        raise RuntimeError(f"{image.name} does not fit the {name} partition")
    out = uboot_cmd(session, f"mtd erase {device} 0x{offset:x} 0x{part_size:x}",
                    timeout=timeout)
    if any(word in out.lower() for word in ("error", "failed", "failure")):
        raise RuntimeError(f"erase of {name} failed")
    # mtd takes <addr> [<off> [<size>]]; the length is passed explicitly and
    # the driver pads the tail to the NAND page boundary.
    out = uboot_cmd(session,
                    f"mtd write {device} {address} 0x{offset:x} 0x{size:x}",
                    timeout=timeout)
    if any(word in out.lower() for word in ("error", "failed", "failure")):
        raise RuntimeError(f"write of {name} failed")
    log(f"{name}: {size} bytes written at 0x{offset:x}")


def backup_idblock(args):
    """Copy the live idblock partition to the host before it is overwritten.

    The U-Boot TFTP server is read-only, so the backup is taken over SSH from
    the running Linux.  The image is a raw /dev/mtdblock1 dump (256K on the
    product layout, 512K on -AOV), stored under --backup-dir.
    """
    import paramiko

    host = args.dut_host or os.environ.get("OKO_DUT_HOST", "192.168.52.33")
    dest_dir = Path(args.backup_dir).expanduser()
    dest_dir.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    dest = dest_dir / f"idblock-{stamp}.img"
    remote = "/tmp/oko-idblock-backup.img"

    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    try:
        client.connect(host, username=args.dut_user,
                       password=args.dut_password, timeout=10,
                       banner_timeout=10)
        _, stdout, stderr = client.exec_command(
            f"cat /dev/mtdblock1 > {remote} && wc -c <{remote}", timeout=30)
        out = stdout.read().decode(errors="replace").strip()
        err = stderr.read().decode(errors="replace").strip()
        if stdout.channel.recv_exit_status() != 0 or not out.isdigit() or int(out) == 0:
            raise RuntimeError(f"idblock backup failed: {err or out}")
        sftp = client.open_sftp()
        sftp.get(remote, str(dest))
        sftp.close()
        client.exec_command(f"rm -f {remote}", timeout=10)
    finally:
        client.close()

    size = dest.stat().st_size
    if size < 0x40000:
        raise RuntimeError(f"idblock backup is too small: {size} bytes")
    log(f"idblock backup: {dest} ({size} bytes, sha256 {sha256(dest)[:16]}…)")
    return dest


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", default=netboot.default_port(),
                        help="console: telnet://host:port or a serial device")
    parser.add_argument("--address", default="0x00e00000",
                        help="RAM load address (U-Boot ramdisk_addr_r default)")
    parser.add_argument("--components", default="boot,oem,rootfs",
                        help="comma separated: env,idblock,uboot,boot,oem,rootfs,userdata "
                             "(default: boot,oem,rootfs)")
    parser.add_argument("--userdata", action="store_true",
                        help="also rewrite the userdata partition (wipes it)")
    parser.add_argument("--backup-dir", default=str(Path(args_out_default()) / "backup"),
                        help="host directory for the pre-flash idblock backup")
    parser.add_argument("--dut-host", default=os.environ.get("OKO_DUT_HOST", "192.168.52.33"),
                        help="DUT SSH address for the idblock backup")
    parser.add_argument("--dut-user", default=os.environ.get("OKO_DUT_USER", "root"))
    parser.add_argument("--dut-password", default=os.environ.get("OKO_DUT_PASSWORD", "luckfox"))
    parser.add_argument("--skip-backup", action="store_true",
                        help="do NOT take the idblock backup (unsafe; Maskrom recovery only)")
    parser.add_argument("--prompt-timeout", type=float, default=120.0,
                        help="seconds to wait for the U-Boot prompt")
    parser.add_argument("--timeout", type=float, default=900.0,
                        help="per-command timeout, seconds")
    parser.add_argument("--no-reset", action="store_true",
                        help="stay in U-Boot instead of resetting")
    parser.add_argument("--out", default=os.environ.get("OKO_BENCH_OUT", str(netboot.DEFAULT_OUT)),
                        help="artifact directory (default: output/bench)")
    parser.add_argument("--power-cycle", action="store_true",
                        help="cold power-cycle the DUT through the ESP32 bridge "
                             "and catch U-Boot on the way up (needed after a "
                             "failed AOV suspend left a zombie HPMCU)")
    parser.add_argument("--off-seconds", type=float, default=5.0,
                        help="power-off time for --power-cycle")
    parser.add_argument("--bridge-cookie", default=None,
                        help="reuse this bridge session cookie file")
    args = parser.parse_args()

    partitions = load_partitions()
    components = [item.strip() for item in args.components.split(",") if item.strip()]
    if args.userdata and "userdata" not in components:
        components.append("userdata")
    unknown = [item for item in components if item not in partitions]
    if unknown:
        parser.error(f"unknown components: {', '.join(unknown)}")
    for name, (offset, size) in partitions.items():
        log(f"layout: {name:8s} @0x{offset:08x} size 0x{size:x}")

    images = {}
    for name in components:
        path = IMAGE_DIR / f"{name}.img"
        if not path.is_file():
            parser.error(f"missing {path}; run ./build.sh firmware")
        images[name] = path
        log(f"{name}: {path} ({path.stat().st_size} bytes, sha256 {sha256(path)[:16]}…)")

    if "idblock" in components:
        if args.skip_backup:
            log("idblock: backup SKIPPED (--skip-backup): Maskrom recovery only")
        else:
            backup_idblock(args)

    session = netboot.Session(args.port, Path(args.out).expanduser())
    try:
        log(f"log: {session.path}")
        if args.power_cycle:
            import power

            log("cold power cycle; catching U-Boot on the way up")
            power.run("cycle", bridge=os.environ.get("OKO_BRIDGE", "192.168.52.17"),
                      off_seconds=args.off_seconds,
                      cookie_file=args.bridge_cookie)
            if not netboot.wait_prompt(session, args.prompt_timeout):
                raise RuntimeError("U-Boot prompt not found after the power cycle")
            uboot_sync(session)
            log("U-Boot prompt ok")
        else:
            enter_uboot(session, args.prompt_timeout, args)

        out = uboot_cmd(session, "mtd list", timeout=30)
        match = re.search(r"^\*\s+(\S+)", out, re.MULTILINE)
        if not match:
            raise RuntimeError("no MTD device in 'mtd list'")
        device = match.group(1)
        log(f"mtd device: {device}")

        for command in TFTP_READY_CMDS:
            uboot_cmd(session, command, timeout=30)

        for name in components:
            flash_component(session, device, name, images[name], args.address,
                            args.timeout, partitions)

        # The env may still point at an NFS root from a netboot session.
        uboot_cmd(session, f"setenv sys_bootargs '{nfs_env.NAND_BOOTARGS}'", timeout=30)
        # The partition table is derived from the flashed image's .env.txt;
        # the -AOV layout moves uboot/boot/oem/userdata/rootfs by 768K.
        try:
            env_text = (IMAGE_DIR / ".env.txt").read_text()
        except OSError:
            env_text = ""
        match = re.search(r"^mtdparts=(\S+)", env_text, re.MULTILINE)
        if match:
            uboot_cmd(session, f"setenv mtdparts '{match.group(1)}'", timeout=30)
        uboot_cmd(session, "saveenv", timeout=60)

        if args.no_reset:
            log("done; staying in U-Boot")
        else:
            log("flash done, resetting the DUT")
            session.write(b"reset\r")
            time.sleep(3)
    finally:
        session.close("[tftp_flash done]")

    if not args.no_reset:
        log("flash finished; verify after boot: "
            "cat /proc/cmdline, cat /proc/mtd, uname -a")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, OSError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        sys.exit(1)