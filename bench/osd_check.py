#!/usr/bin/env python3
"""Capture RTSP frames and diff the OSD regions pairwise.

For every pair of consecutive sampled frames the script reports the number of
changed pixels (luma difference > 30) and the maximum difference per region.
Full frames and per-region crops are saved as PNG in the artifact directory.

The default region boxes follow project/app/divinus/files/divinus.yaml
(regN_posy 16/64/112, each region 48 px high, 1200 px wide).  If the OSD layout
changes, update the boxes here and in that YAML.

Examples:
    bench/run.sh osd_check.py --ip 192.168.52.150 --seconds 17
    bench/run.sh osd_check.py --ip 192.168.52.150 --frames 20 --interval 1
"""
import argparse
import os
import sys
import time
from pathlib import Path

import av
from PIL import ImageChops

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUT = ROOT / "output" / "bench"
DEFAULT_REGIONS = (
    "time:1600,1024,1920,1080;"
    "telemetry:0,16,760,160;"
    "gps:1500,16,1920,64;"
    "msg:0,160,1200,224"
)
THRESHOLD = 30


def parse_regions(text):
    regions = []
    for item in text.split(";"):
        item = item.strip()
        if not item:
            continue
        name, _, box = item.partition(":")
        coords = [int(part) for part in box.split(",")]
        if len(coords) != 4:
            raise argparse.ArgumentTypeError(f"bad region box: {item}")
        regions.append((name or f"reg{len(regions)}", tuple(coords)))
    if not regions:
        raise argparse.ArgumentTypeError("no regions given")
    return regions


def capture(url, frames, interval, seconds):
    """Sample frames from one RTSP connection until frames or seconds run out."""
    samples = []
    start = time.time()
    last = -1e9
    container = av.open(url, timeout=10)
    try:
        stream = container.streams.video[0]
        for frame in container.decode(stream):
            now = time.time() - start
            if now - last < interval:
                continue
            samples.append((len(samples), frame.to_image().convert("RGB")))
            last = now
            if len(samples) >= frames or now >= seconds:
                break
    finally:
        container.close()
    return samples


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--ip", default=os.environ.get("OKO_DUT_HOST", "192.168.52.150"))
    parser.add_argument("--user", default=os.environ.get("OKO_RTSP_USER", "admin"))
    parser.add_argument("--password", default=os.environ.get("OKO_RTSP_PASS", "oko-change-me"))
    parser.add_argument("--regions", type=parse_regions, default=parse_regions(DEFAULT_REGIONS),
                        help=f"regions as name:x0,y0,x1,y1;... (default: {DEFAULT_REGIONS})")
    parser.add_argument("--frames", type=int, default=20, help="maximum sampled frames")
    parser.add_argument("--interval", type=float, default=1.0, help="seconds between samples")
    parser.add_argument("--seconds", type=float, default=20.0, help="capture time limit")
    parser.add_argument("--out", default=os.environ.get("OKO_BENCH_OUT", str(DEFAULT_OUT)))
    args = parser.parse_args()

    out = Path(args.out).expanduser()
    out.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    url = f"rtsp://{args.user}:{args.password}@{args.ip}:554/"

    print(f"rtsp: {args.ip}  out: {out}")
    samples = capture(url, args.frames, args.interval, args.seconds)
    if len(samples) < 2:
        print(f"captured only {len(samples)} frame(s); need at least 2", file=sys.stderr)
        return 1

    for idx, image in samples:
        image.save(out / f"osd-{stamp}-f{idx:02d}.png")
        for name, box in args.regions:
            image.crop(box).save(out / f"osd-{stamp}-f{idx:02d}-{name}.png")

    print(f"captured {len(samples)} frames, size {samples[0][1].size}")
    changed = {name: 0 for name, _ in args.regions}
    peak = {name: 0 for name, _ in args.regions}
    for (prev_idx, prev), (cur_idx, cur) in zip(samples, samples[1:]):
        for name, box in args.regions:
            diff = ImageChops.difference(prev.crop(box), cur.crop(box)).convert("L")
            try:
                pixels = list(diff.get_flattened_data())
            except AttributeError:  # Pillow < 13
                pixels = list(diff.getdata())
            count = sum(1 for value in pixels if value > THRESHOLD)
            maximum = max(pixels)
            if count:
                changed[name] += 1
            peak[name] = max(peak[name], maximum)
            print(f"pair f{prev_idx:02d}-f{cur_idx:02d} {name}: changed={count} max={maximum}")

    print("summary:")
    for name, _ in args.regions:
        print(f"  {name}: pairs_changed={changed[name]} max_diff={peak[name]}")
    print(f"frames and crops: {out}/osd-{stamp}-*.png")
    return 0


if __name__ == "__main__":
    sys.exit(main())