#!/usr/bin/env python3
"""Build the OKO STM8 application image from SDCC Intel HEX output.

The application must be linked at 0x8A10 (Makefile.stm8 passes
--code-loc 0x8A10).  This script:

  * parses the boot and application Intel HEX files;
  * enforces the hard size gate (boot < 0x8A00, app < 0xA000);
  * fills erased gaps with 0xFF and computes CRC32 over the application
    bytes starting at 0x8A10;
  * writes the 16-byte header at 0x8A00;
  * emits app.ihx (header + code), factory.ihx (boot + app) and app.bin.

Run `--check` to enforce the gate without writing files.
"""
import argparse
import os
import re
import sys

MAGIC = b"OKO8"
BOOT_BASE = 0x8000
BOOT_END = 0x8A00
HDR_BASE = 0x8A00
HDR_SIZE = 16
APP_BASE = 0x8A10
APP_END = 0xA000
APP_MAX_SIZE = APP_END - APP_BASE
BLOCK = 64
MAIN_H = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                      "src", "core", "main.h")


def read_hex(path):
    mem = {}
    with open(path, "r") as file:
        lines = list(file)
    for lineno, line in enumerate(lines, 1):
        line = line.strip()
        if not line:
            continue
        if not line.startswith(":"):
            raise SystemExit("%s:%d: not an Intel HEX record" % (path, lineno))
        try:
            raw = bytes.fromhex(line[1:])
        except ValueError:
            raise SystemExit("%s:%d: bad hex digits" % (path, lineno))
        if len(raw) < 5:
            raise SystemExit("%s:%d: short record" % (path, lineno))
        length, address, rectype = raw[0], (raw[1] << 8) | raw[2], raw[3]
        data = raw[4:4 + length]
        if len(data) != length:
            raise SystemExit("%s:%d: truncated record" % (path, lineno))
        if sum(raw) & 0xFF:
            raise SystemExit("%s:%d: checksum mismatch" % (path, lineno))
        if rectype == 0x00:
            for i, byte in enumerate(data):
                mem[address + i] = byte
        elif rectype == 0x01:
            break
        elif rectype == 0x04:
            # Extended linear address: this project stays below 64 KiB, so it
            # is ignored on purpose rather than silently mangled.
            continue
        else:
            continue
    return mem


def write_record(out, address, rectype, data):
    payload = bytearray([len(data), (address >> 8) & 0xFF, address & 0xFF,
                         rectype])
    payload.extend(data)
    checksum = (-sum(payload)) & 0xFF
    out.write(":" + payload.hex().upper() + "%02X\n" % checksum)


def write_hex(path, mem):
    addresses = sorted(mem)
    with open(path, "w", newline="\n") as out:
        if not addresses:
            raise SystemExit("%s: empty image" % path)
        high = None
        pos = 0
        while pos < len(addresses):
            start = addresses[pos]
            chunk = bytearray()
            while pos < len(addresses) and addresses[pos] == start + len(chunk) \
                    and len(chunk) < 16:
                chunk.append(mem[addresses[pos]])
                pos += 1
            upper = (start >> 16) & 0xFFFF
            if upper != high:
                write_record(out, 0, 0x04, bytes([(upper >> 8) & 0xFF,
                                                  upper & 0xFF]))
                high = upper
            write_record(out, start & 0xFFFF, 0x00, chunk)
        write_record(out, 0, 0x01, b"")


def crc32(data):
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xEDB88320 if crc & 1 else crc >> 1
    return crc ^ 0xFFFFFFFF


def parse_version(text):
    parts = text.split(".")
    if len(parts) != 3:
        raise SystemExit("version must be major.minor.patch")
    values = [int(p) for p in parts]
    if any(v < 0 or v > 255 for v in values):
        raise SystemExit("version components must fit in a byte")
    return values


def version_from_header(path):
    with open(path, "r", encoding="utf-8") as file:
        text = file.read()
    found = []
    for name in ("APP_VERSION_MAJOR", "APP_VERSION_MINOR", "APP_VERSION_PATCH"):
        m = re.search(r"#define\s+%s\s+(\d+)" % name, text)
        if not m:
            return None
        found.append(int(m.group(1)))
    return found


def span(mem):
    if not mem:
        return (0, 0)
    return (min(mem), max(mem))


def require_gate(boot, app):
    boot_lo, boot_hi = span(boot)
    app_lo, app_hi = span(app)
    if boot_lo < BOOT_BASE:
        raise SystemExit("bootloader starts below 0x%04X: 0x%04X"
                         % (BOOT_BASE, boot_lo))
    if boot_hi >= BOOT_END:
        raise SystemExit("bootloader overflows: last byte 0x%04X >= 0x%04X"
                         % (boot_hi, BOOT_END))
    if app_lo < APP_BASE:
        raise SystemExit("application has data below 0x%04X: 0x%04X"
                         % (APP_BASE, app_lo))
    if app_hi >= APP_END:
        raise SystemExit("application overflows: last byte 0x%04X >= 0x%04X"
                         % (app_hi, APP_END))
    length = app_hi - APP_BASE + 1
    if length > APP_MAX_SIZE:
        raise SystemExit("application is %d bytes, limit is %d"
                         % (length, APP_MAX_SIZE))
    return length


def build(args):
    boot = read_hex(args.boot)
    app = read_hex(args.app)
    length = require_gate(boot, app)

    payload = bytearray([0xFF] * length)
    for address, value in app.items():
        payload[address - APP_BASE] = value

    version = args.version
    if version:
        major, minor, patch = parse_version(version)
    else:
        parsed = version_from_header(args.main_h)
        if not parsed:
            raise SystemExit("cannot read APP_VERSION_* from %s; pass --version"
                             % args.main_h)
        major, minor, patch = parsed

    crc = crc32(payload)
    header = bytearray(MAGIC)
    header.extend(length.to_bytes(4, "big"))
    header.extend(bytes([major, minor, patch, 0]))
    header.extend(crc.to_bytes(4, "big"))
    assert len(header) == HDR_SIZE

    appmem = dict()
    for i, byte in enumerate(header):
        appmem[HDR_BASE + i] = byte
    for i, byte in enumerate(payload):
        appmem[APP_BASE + i] = byte

    merged = dict(boot)
    merged.update(appmem)

    write_hex(args.out, appmem)
    if args.factory:
        write_hex(args.factory, merged)
    if args.bin:
        with open(args.bin, "wb") as out:
            out.write(header)
            out.write(payload)

    print("app: %d bytes (limit %d), CRC32 0x%08X, version %d.%d.%d"
          % (length, APP_MAX_SIZE, crc, major, minor, patch))
    print("boot: %d bytes (last 0x%04X, limit 0x%04X)"
          % (len(boot), span(boot)[1], BOOT_END - 1))
    return 0


def check(args):
    boot = read_hex(args.check[0])
    app = read_hex(args.check[1])
    length = require_gate(boot, app)
    print("OK: boot %d bytes, app %d bytes (limit %d)"
          % (len(boot), length, APP_MAX_SIZE))
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--boot", help="bootloader Intel HEX")
    parser.add_argument("--app", help="application Intel HEX (linked at 0x8A10)")
    parser.add_argument("--out", help="output app.ihx")
    parser.add_argument("--factory", help="output factory.ihx")
    parser.add_argument("--bin", help="output raw app.bin (header + code)")
    parser.add_argument("--version", help="major.minor.patch (default: main.h)")
    parser.add_argument("--main-h", default=MAIN_H,
                        help="header with APP_VERSION_*")
    parser.add_argument("--check", nargs=2, metavar=("BOOT", "APP"),
                        help="only enforce the flash size gate")
    args = parser.parse_args(argv)

    if args.check:
        return check(args)
    if not (args.boot and args.app and args.out):
        parser.error("--boot, --app and --out are required")
    return build(args)


if __name__ == "__main__":
    sys.exit(main())