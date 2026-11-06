#!/usr/bin/env python3
"""Convert an Intel HEX file to a flat binary.

By default the output starts at the lowest address in the file and gaps are
filled with 0xFF (the erased state of STM8 flash).  Use --base/--size to pin
the window, e.g. the application payload of app.ihx:

    python3 tools/ihx2bin.py build/app.ihx build/app-payload.bin \
        --base 0x8A10 --size 0x15F0
"""
import argparse
import sys

from mkapp import read_hex


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input")
    parser.add_argument("output")
    parser.add_argument("--base", type=lambda v: int(v, 0))
    parser.add_argument("--size", type=lambda v: int(v, 0))
    parser.add_argument("--fill", type=lambda v: int(v, 0), default=0xFF)
    args = parser.parse_args(argv)

    mem = read_hex(args.input)
    if not mem:
        raise SystemExit("%s: empty hex file" % args.input)

    base = args.base if args.base is not None else min(mem)
    size = args.size if args.size is not None else max(mem) - base + 1
    if size <= 0:
        raise SystemExit("invalid base/size")

    out = bytearray([args.fill & 0xFF] * size)
    for address, value in mem.items():
        offset = address - base
        if 0 <= offset < size:
            out[offset] = value
    with open(args.output, "wb") as file:
        file.write(out)
    print("%s: %d bytes from 0x%04X" % (args.output, size, base))
    return 0


if __name__ == "__main__":
    sys.exit(main())