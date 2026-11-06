#!/usr/bin/env python3
"""Check the focus-assistant API and print one AF statistics sample.

    bench/run.sh focus_check.py                 # read-only snapshot
    bench/run.sh focus_check.py --enable        # enable and poll 5 times
    bench/run.sh focus_check.py --roi 2500,2500,5000,5000
    bench/run.sh focus_check.py --map           # include the 225-cell map

Exit codes: 0 running, 2 API ok but no AF statistics (hardware gate), 1 error.
"""
import argparse
import csv
import json
import os
import statistics
import sys
import time

import api_check


def focus_request(path, method="GET", body=None):
    status, headers, payload = api_check.request(path, method, body,
        "application/x-www-form-urlencoded" if body else None)
    try:
        return status, json.loads(payload.decode())
    except Exception:                              # noqa: BLE001
        return status, {"error": "invalid JSON", "raw": payload[:160].decode(errors="replace")}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--enable", action="store_true")
    parser.add_argument("--disable", action="store_true")
    parser.add_argument("--roi", help="x,y,w,h in basis points")
    parser.add_argument("--mode", choices=["full", "roi"])
    parser.add_argument("--motor", choices=["on", "off"])
    parser.add_argument("--move", type=int, help="relative focus move in steps")
    parser.add_argument("--map", action="store_true")
    parser.add_argument("--polls", type=int, default=5)
    parser.add_argument("--seconds", type=float, default=0.0,
        help="sample for this many seconds and write a CSV")
    parser.add_argument("--interval", type=float, default=1.0,
        help="sampling interval for --seconds, seconds")
    parser.add_argument("--out", default="output/bench/focus",
        help="directory for the CSV and optional snapshot")
    parser.add_argument("--snapshot", action="store_true",
        help="save /image.jpg next to the CSV")
    parser.add_argument("--strict", action="store_true",
        help="exit 1 when the metric never changes (static-scene gate)")
    args = parser.parse_args()

    status, dec = focus_request("/api/focus?map=1")
    if status != 200 or not isinstance(dec, dict) or dec.get("error"):
        print("FAIL /api/focus: HTTP %s %r" % (status, dec), file=sys.stderr)
        return 1
    for key in ("enable", "available", "status", "mode", "roi", "metric",
                "metric_avg", "metric_smooth", "cells_total", "cells_used",
                "highlit_cells", "motor"):
        if key not in dec:
            print("FAIL /api/focus: missing key %s" % key, file=sys.stderr)
            return 1
    print("focus: status=%s available=%s mode=%s metric_avg=%s cells=%s/%s" % (
        dec["status"], dec["available"], dec["mode"], dec["metric_avg"],
        dec["cells_used"], dec["cells_total"]))

    parts = []
    if args.enable:
        parts.append("enable=1")
    if args.disable:
        parts.append("enable=0")
    if args.roi:
        values = args.roi.split(",")
        if len(values) != 4:
            parser.error("--roi needs x,y,w,h")
        parts.extend(["mode=roi"] + ["%s=%s" % (k, v) for k, v in
            zip(("rx", "ry", "rw", "rh"), values)])
    if args.mode:
        parts.append("mode=" + args.mode)
    if args.motor:
        parts.append("motor=" + ("1" if args.motor == "on" else "0"))
    if args.move is not None:
        parts.append("move=%d" % args.move)

    if parts:
        status, dec = focus_request("/api/focus?" + "&".join(parts), "POST")
        if status not in (200, 202) or dec.get("error"):
            print("FAIL POST /api/focus: HTTP %s %r" % (status, dec),
                  file=sys.stderr)
            return 1
        print("post: status=%s mode=%s roi=%s" % (dec.get("status"),
              dec.get("mode"), dec.get("roi")))

    if args.seconds > 0:
        return sample_loop(args)

    if not args.enable and not parts:
        return 0 if dec["status"] == "running" else 2

    for i in range(max(1, args.polls)):
        status, dec = focus_request("/api/focus?map=%d" % (1 if args.map else 0))
        if status != 200 or dec.get("error"):
            print("FAIL poll %d: HTTP %s %r" % (i, status, dec), file=sys.stderr)
            return 1
        print("poll %d: status=%s metric=%s avg=%s smooth=%s cells=%s highlit=%s frame=%s" % (
            i, dec["status"], dec["metric"], dec["metric_avg"],
            dec["metric_smooth"], dec["cells_used"], dec["highlit_cells"],
            dec["frame_id"]))
        if args.map and "map" in dec:
            print("  map: %d cells" % len(dec["map"].get("fv_h1", [])))
        if dec["status"] == "running" and dec["metric_avg"] > 0:
            return 0
        import time
        time.sleep(1)

    return 0 if dec["status"] == "running" else 2


FIELDS = ("ts", "frame_id", "status", "mode", "rx", "ry", "rw", "rh",
          "metric", "metric_avg", "metric_smooth", "cells_used", "cells_total",
          "highlit_cells", "af_hw_ver")


def sample_once():
    status, dec = focus_request("/api/focus")
    if status != 200 or not isinstance(dec, dict) or dec.get("error"):
        return None
    roi = dec.get("roi", {})
    return {
        "ts": round(time.time(), 3),
        "frame_id": dec.get("frame_id"),
        "status": dec.get("status"),
        "mode": dec.get("mode"),
        "rx": roi.get("x"), "ry": roi.get("y"),
        "rw": roi.get("w"), "rh": roi.get("h"),
        "metric": dec.get("metric"),
        "metric_avg": dec.get("metric_avg"),
        "metric_smooth": dec.get("metric_smooth"),
        "cells_used": dec.get("cells_used"),
        "cells_total": dec.get("cells_total"),
        "highlit_cells": dec.get("highlit_cells"),
        "af_hw_ver": dec.get("af_hw_ver"),
    }


def sample_loop(args):
    os.makedirs(args.out, exist_ok=True)
    stem = time.strftime("focus-%Y%m%d-%H%M%S")
    csv_path = os.path.join(args.out, stem + ".csv")
    rows = []
    deadline = time.time() + args.seconds

    while time.time() < deadline:
        row = sample_once()
        if row is None:
            print("FAIL /api/focus while sampling", file=sys.stderr)
            return 1
        rows.append(row)
        print("%(frame_id)s %(status)s avg=%(metric_avg)s cells=%(cells_used)s "
              "mode=%(mode)s roi=%(rx)s,%(ry)s,%(rw)s,%(rh)s" % row)
        time.sleep(max(0.05, args.interval))

    with open(csv_path, "w", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=FIELDS)
        writer.writeheader()
        writer.writerows(rows)

    values = [r["metric_avg"] for r in rows]
    print("samples: %d, CSV: %s" % (len(rows), csv_path))
    if values:
        print("metric_avg: min=%d max=%d mean=%.0f std=%.0f" % (
            min(values), max(values), statistics.mean(values),
            statistics.pstdev(values)))

    if args.snapshot:
        status, headers, payload = api_check.request("/image.jpg")
        if status == 200:
            path = os.path.join(args.out, stem + ".jpg")
            with open(path, "wb") as fh:
                fh.write(payload)
            print("snapshot: %s" % path)
        else:
            print("WARN: /image.jpg returned %s" % status, file=sys.stderr)

    if not values or max(values) == 0:
        print("FAIL: the AF metric is zero (A6 gate)", file=sys.stderr)
        return 1
    if args.strict and statistics.pstdev(values) == 0:
        print("FAIL: the AF metric never changed", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
