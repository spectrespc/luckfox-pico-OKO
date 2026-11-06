#!/usr/bin/env python3
"""Merge Intel HEX files into one image (later files win on overlap)."""
import argparse
import sys

from mkapp import read_hex, write_hex


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output")
    parser.add_argument("inputs", nargs="+")
    args = parser.parse_args(argv)

    merged = {}
    for path in args.inputs:
        data = read_hex(path)
        merged.update(data)
        print("%s: %d bytes (0x%04X..0x%04X)"
              % (path, len(data), min(data), max(data)))
    write_hex(args.output, merged)
    print("%s: %d bytes" % (args.output, len(merged)))
    return 0


if __name__ == "__main__":
    sys.exit(main())