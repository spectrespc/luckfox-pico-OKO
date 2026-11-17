#!/usr/bin/env python3
"""Patch the AOV wakeup-parameter segment inside meta.img.

The MCU AOV application (rt-thread applications/battery-ipc/stream.c) reads
`struct wakeup_param_info` from the meta image at WAKEUP_PARAM_OFFSET (0x1c00)
and uses it to decide between CMD_AP_RESUME and CMD_AP_SUSPEND:

    ae_wakeup_mode 0 -> keep the ISP motion flag (AE decides)
                    1 -> force md_flag = 1 (treat as motion)
                    2 -> force md_flag = 0 (quiet)
    mcu_max_run_count -1 -> no limit; otherwise motion after N runs

make_meta leaves that segment filled with 0xff, which the MCU treats as
"no policy" (ae_wakeup_mode = -1 leaves the ISP flag untouched).  This helper
writes a sane structure so the AOV decision can be controlled from the
profile, e.g.:

    tools/native/patch_meta_aov.py --meta output/image/meta.img \
        --ae-wakeup-mode 2

Offsets come from the MCU header:
sysdrv/source/mcu/rt-thread/applications/battery-ipc/rk_meta.h
"""
import argparse
import struct
import sys

WAKEUP_PARAM_OFFSET = 0x1C00
STRUCT_FMT = "<IiIIiiiiI"      # head, len, wakeup_mode, ae_wakeup_mode,
                               # mcu_run_count, mcu_max_run_count,
                               # arm_run_count, arm_max_run_count, crc32
STRUCT_SIZE = struct.calcsize(STRUCT_FMT)
META_MAGIC = b"RKMT"


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--meta", default="output/image/meta.img")
    parser.add_argument("--ae-wakeup-mode", type=int, default=0,
                        choices=[0, 1, 2],
                        help="0 use AE, 1 force motion, 2 force quiet")
    parser.add_argument("--mcu-max-run-count", type=int, default=-1,
                        help="force motion after N MCU runs (-1 = no limit)")
    args = parser.parse_args()

    with open(args.meta, "r+b") as image:
        data = image.read(8)
        if data[:4] != META_MAGIC:
            print(f"{args.meta}: not an RK meta image", file=sys.stderr)
            return 1
        image.seek(WAKEUP_PARAM_OFFSET)
        blob = struct.pack(STRUCT_FMT,
                           0,                      # head
                           STRUCT_SIZE,            # len
                           0,                      # wakeup_mode
                           args.ae_wakeup_mode,
                           0,                      # mcu_run_count
                           args.mcu_max_run_count,
                           0,                      # arm_run_count
                           -1,                     # arm_max_run_count
                           0)                      # crc32
        image.write(blob)
    print(f"{args.meta}: wakeup_param ae_mode={args.ae_wakeup_mode} "
          f"max_run_count={args.mcu_max_run_count} "
          f"({STRUCT_SIZE} bytes @ 0x{WAKEUP_PARAM_OFFSET:04x})")
    return 0


if __name__ == "__main__":
    sys.exit(main())