#!/usr/bin/env python3
"""Capture the DUT UART from the ESP32 read-only monitor and measure AOV cadence.

The HPMCU and the AP share UART2, so the monitor stream carries both consoles.
This tool records the stream for a fixed time, counts the AOV handshake markers
("-h-wait-", "-h-mbox-", "-h-timeout-") and prints inter-marker deltas, which is
how the quiet-cycle cadence (dense loop vs one cycle per wake_interval_s) is
measured without touching the single-client telnet console.

Examples:
    bench/run.sh aov_cadence.py --label keepisp1 --seconds 90
    bench/run.sh aov_cadence.py --label keepisp0 --seconds 90
    bench/run.sh aov_cadence.py --file output/bench/aov-test/quiet_keepisp1.x.log

The monitor must be enabled on the bridge first (POST /config with
{"telnet0_mon_enable":1} + bridge reboot); it defaults to 192.168.52.17:2324.
The monitor drops bytes on a busy console and says so inline; the summary
counts them (dropped_bytes/gap_notices) and sets complete=false, so a run that
lost data is not mistaken for a full measurement.
"""
import argparse
import bisect
import json
import re
import socket
import statistics
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUT = ROOT / "output" / "bench" / "aov-test"
MARKERS = ("-h-wait-", "-h-mbox-", "-h-timeout-")

# Each chunk starts with "[2026-11-17 21:58:57.388] RX: "; the bytes of the DUT
# follow until the next chunk prefix and may span several lines.  A busy
# console makes the monitor drop bytes, which it marks inline, and a marker can
# straddle a chunk boundary - both must be handled or the cadence is understated.
CHUNK_RE = re.compile(
    rb"\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\] RX: ", re.MULTILINE)
GAP_RE = re.compile(r"--- gap: (\d+) byte\(s\) dropped ---")


def parse_chunks(raw):
    """Yield (epoch_seconds, payload_bytes) for every monitor chunk."""
    matches = list(CHUNK_RE.finditer(raw))
    for index, match in enumerate(matches):
        text = match.group(1).decode()
        stamp = time.mktime(time.strptime(text, "%Y-%m-%d %H:%M:%S.%f"))
        end = matches[index + 1].start() if index + 1 < len(matches) else len(raw)
        yield stamp, raw[match.end():end]


def summarize(raw, label):
    # Join the payloads before counting: a marker split by a chunk boundary is
    # then found, and the chunk start offsets map each hit back to its stamp.
    pieces = []
    starts = []
    chunk_stamps = []
    dropped_bytes = 0
    gap_notices = 0
    offset = 0
    for stamp, payload in parse_chunks(raw):
        text = payload.decode(errors="replace")
        for gap in GAP_RE.finditer(text):
            dropped_bytes += int(gap.group(1))
            gap_notices += 1
        # A NUL keeps the two sides of a gap from forming a marker.
        text = GAP_RE.sub("\x00", text)
        if text:
            chunk_stamps.append(stamp)
        starts.append((offset, stamp))
        pieces.append(text)
        offset += len(text)
    joined = "".join(pieces)
    start_offsets = [item[0] for item in starts]

    marks = {}
    for name in MARKERS:
        times = []
        for hit in re.finditer(re.escape(name), joined):
            index = bisect.bisect_right(start_offsets, hit.start()) - 1
            times.append(starts[max(index, 0)][1])
        marks[name] = times

    waits = sorted(marks["-h-wait-"])
    deltas = [round(b - a, 3) for a, b in zip(waits, waits[1:])]
    span = round(waits[-1] - waits[0], 3) if len(waits) > 1 else 0.0
    chunk_gaps = [round(b - a, 3) for a, b in zip(chunk_stamps, chunk_stamps[1:])]
    chunk_gaps = [gap for gap in chunk_gaps if gap > 0]
    summary = {
        "label": label,
        "counts": {name: len(times) for name, times in marks.items()},
        "h_wait_span_s": span,
        "h_wait_per_s": round((len(waits) - 1) / span, 3) if span > 0 else 0.0,
        "delta_median_s": round(statistics.median(deltas), 3) if deltas else None,
        "delta_p90_s": (round(sorted(deltas)[int(0.9 * (len(deltas) - 1))], 3)
                        if deltas else None),
        "delta_resolution_s": (round(statistics.median(chunk_gaps), 3)
                               if chunk_gaps else None),
        "gaps_over_2s": [d for d in deltas if d > 2.0][:20],
        "dropped_bytes": dropped_bytes,
        "gap_notices": gap_notices,
        "complete": dropped_bytes == 0,
    }
    return summary


def capture(host, port, seconds, path):
    conn = socket.create_connection((host, port), timeout=10)
    conn.settimeout(0.5)
    deadline = time.time() + seconds
    chunks = []
    with open(path, "wb") as logfile:
        while time.time() < deadline:
            try:
                data = conn.recv(65536)
            except socket.timeout:
                continue
            if not data:
                break
            logfile.write(data)
            chunks.append(data)
    conn.close()
    return b"".join(chunks)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--host", default="192.168.52.17")
    parser.add_argument("--port", type=int, default=2324)
    parser.add_argument("--seconds", type=float, default=90.0)
    parser.add_argument("--label", default="quiet")
    parser.add_argument("--out-dir", default=str(DEFAULT_OUT))
    parser.add_argument("--file", default=None,
                        help="analyse an existing capture instead of recording")
    args = parser.parse_args()

    out_dir = Path(args.out_dir).expanduser()
    out_dir.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d-%H%M%S")

    if args.file:
        source = Path(args.file).expanduser()
        raw = source.read_bytes()
        summary_path = out_dir / f"summary_{args.label}.{stamp}.json"
    else:
        source = out_dir / f"quiet_{args.label}.{stamp}.log"
        raw = capture(args.host, args.port, args.seconds, source)
        summary_path = out_dir / f"summary_{args.label}.{stamp}.json"

    summary = summarize(raw, args.label)
    summary["capture"] = str(source)
    summary_path.write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))
    print(f"capture: {source}")
    print(f"summary: {summary_path}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        sys.exit(1)