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

# Bytes offsets and sizes of the SPI NAND partitions.  They repeat the
# mtdparts string from output/image/.env.txt:
#   mtdparts=spi-nand0:256K(env),256K@256K(idblock),512K(uboot),6M(boot),40M(oem),40M(userdata),168M(rootfs)
# U-Boot here has CONFIG_CMD_MTDPARTS off, so `mtd` works on the whole
# spi-nand device and the offset/size must be passed explicitly.
PARTITIONS = {
    "boot": (0x00100000, 0x00600000),
    "oem": (0x00700000, 0x02800000),
    "userdata": (0x02F00000, 0x02800000),
    "rootfs": (0x05700000, 0x0A800000),
}

SIZE_RE = re.compile(r"Bytes transferred = (\d+)")

TFTP_READY_CMDS = [
    f"setenv ipaddr {netboot.DUT_IP}",
    f"setenv netmask {netboot.NETMASK}",
    f"setenv serverip {netboot.SERVER_IP}",
    f"setenv gatewayip {netboot.GATEWAY}",
    f"ping {netboot.SERVER_IP}",
]


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


def enter_uboot(session, prompt_timeout):
    """Reach the U-Boot prompt, rebooting a running Linux if needed."""
    if at_uboot_prompt(session):
        log("U-Boot prompt is already active")
        uboot_sync(session)
        return
    if netboot.login(session, timeout=10):
        log("linux shell ok, rebooting into U-Boot")
        session.write(b"reboot\r")
        time.sleep(2)
    else:
        log("no linux shell, trying to interrupt autoboot")
    if not netboot.wait_prompt(session, prompt_timeout):
        raise RuntimeError("U-Boot prompt not found: "
                           "power-cycle the DUT if it sits in Maskrom/RKUART")
    uboot_sync(session)
    log("U-Boot prompt ok")


def flash_component(session, device, name, image, address, timeout):
    offset, part_size = PARTITIONS[name]
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


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", default=netboot.default_port(),
                        help="console: telnet://host:port or a serial device")
    parser.add_argument("--address", default="0x00e00000",
                        help="RAM load address (U-Boot ramdisk_addr_r default)")
    parser.add_argument("--components", default="boot,oem,rootfs",
                        help="comma separated: boot,oem,rootfs (default), userdata")
    parser.add_argument("--userdata", action="store_true",
                        help="also rewrite the userdata partition (wipes it)")
    parser.add_argument("--prompt-timeout", type=float, default=120.0,
                        help="seconds to wait for the U-Boot prompt")
    parser.add_argument("--timeout", type=float, default=900.0,
                        help="per-command timeout, seconds")
    parser.add_argument("--no-reset", action="store_true",
                        help="stay in U-Boot instead of resetting")
    parser.add_argument("--out", default=os.environ.get("OKO_BENCH_OUT", str(netboot.DEFAULT_OUT)),
                        help="artifact directory (default: output/bench)")
    args = parser.parse_args()

    components = [item.strip() for item in args.components.split(",") if item.strip()]
    if args.userdata and "userdata" not in components:
        components.append("userdata")
    unknown = [item for item in components if item not in PARTITIONS]
    if unknown:
        parser.error(f"unknown components: {', '.join(unknown)}")

    images = {}
    for name in components:
        path = IMAGE_DIR / f"{name}.img"
        if not path.is_file():
            parser.error(f"missing {path}; run ./build.sh firmware")
        images[name] = path
        log(f"{name}: {path} ({path.stat().st_size} bytes, sha256 {sha256(path)[:16]}…)")

    session = netboot.Session(args.port, Path(args.out).expanduser())
    try:
        log(f"log: {session.path}")
        enter_uboot(session, args.prompt_timeout)

        out = uboot_cmd(session, "mtd list", timeout=30)
        match = re.search(r"^\*\s+(\S+)", out, re.MULTILINE)
        if not match:
            raise RuntimeError("no MTD device in 'mtd list'")
        device = match.group(1)
        log(f"mtd device: {device}")

        for command in TFTP_READY_CMDS:
            uboot_cmd(session, command, timeout=30)

        for name in components:
            flash_component(session, device, name, images[name], args.address, args.timeout)

        # The env may still point at an NFS root from a netboot session.
        uboot_cmd(session, f"setenv sys_bootargs '{nfs_env.NAND_BOOTARGS}'", timeout=30)
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