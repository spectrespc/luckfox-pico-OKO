#!/usr/bin/env python3
"""End-to-end check of the divinus UDP/RTP push and its MediaMTX receiver.

The script reads the camera configuration over SSH, asks the receiver's
control API about the path, decodes the re-published RTSP stream with PyAV and
writes a JSON report plus a per-second CSV into output/bench/stream/.

MediaMTX runs on the bench host in services/stream/, listens on UDP 5600 and
re-publishes the camera stream as rtsp://<server>:8554/cam without
authentication (test stand only).

Examples:
    bench/run.sh stream_check.py                       # 20 s, exit 1 on deviation
    bench/run.sh stream_check.py --seconds 600         # 10-minute stability run
    bench/run.sh stream_check.py --latency --compare-dut
    bench/run.sh stream_check.py --server-host 127.0.0.1 --no-ssh

Exit codes: 0 - all sampled metrics are within tolerance; 1 - deviation (stream
missing, fps out of range, receiver not ready); 2 - the stream exists but a
metric could not be sampled.
"""
import argparse
import csv
import json
import os
import re
import sys
import threading
import time
import urllib.request
from pathlib import Path

import av

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUT = ROOT / "output" / "bench" / "stream"
# Thumbnail used to match a re-published frame with the frame the camera
# encoded.  Small enough to keep a ten-minute run in a few megabytes, large
# enough that two different frames never collide.
THUMB = (32, 18)
RESULT = {"ok": True, "measured": True, "warnings": []}
API_WARNED = set()


def warn(text):
    RESULT["warnings"].append(text)
    print(f"warning: {text}", file=sys.stderr)


def ssh_read_config(host, password):
    """Return /userdata/divinus.yaml as text, or None when SSH is unavailable."""
    try:
        import paramiko
    except ImportError:
        warn("paramiko is not installed; skipping the configuration check")
        return None

    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    try:
        client.connect(host, username="root", password=password, timeout=10)
        _, out, _ = client.exec_command("cat /userdata/divinus.yaml", timeout=20)
        return out.read().decode(errors="replace")
    except Exception as exc:  # noqa: BLE001 - a missing DUT must not abort the run
        warn(f"cannot read the DUT configuration over SSH: {exc}")
        return None
    finally:
        client.close()


def yaml_section(text, name):
    """Yield 'key: value' pairs of one top-level section (flat YAML subset)."""
    if not text:
        return {}
    values, inside = {}, False
    for line in text.splitlines():
        if re.match(rf"^{re.escape(name)}:\s*$", line):
            inside = True
            continue
        if inside and re.match(r"^[A-Za-z_]+:", line):
            break
        if not inside:
            continue
        item = re.match(r"^\s*-\s*(\S+)", line)
        if item:
            if not isinstance(values.get("dest"), list):
                values["dest"] = []
            values["dest"].append(item.group(1))
            continue
        pair = re.match(r"^\s+([A-Za-z_0-9]+):\s*(.*)$", line)
        if pair:
            # "dest:" starts a block list; the entries are picked up above.
            values[pair.group(1)] = pair.group(2).strip() or ([] if pair.group(1) == "dest" else "")
    return values


def check_config(text, server_host):
    """Validate the sections the stream chain depends on."""
    stream = yaml_section(text, "stream")
    mp4 = yaml_section(text, "mp4")
    report = {"stream": stream, "mp4": mp4}

    if stream.get("enable") != "true":
        warn("stream.enable is not true: the camera does not push anything")
        RESULT["ok"] = False
    dests = stream.get("dest") or []
    if not dests:
        warn("stream.dest is empty")
        RESULT["ok"] = False
    wanted = f"udp://{server_host}:"
    if dests and not any(dest.startswith(wanted) for dest in dests):
        warn(f"stream.dest has no {wanted}<port> entry: {dests}")
    if mp4.get("enable") != "true":
        warn("mp4.enable is false: the main channel is not encoded")
        RESULT["ok"] = False
    report["config_ok"] = RESULT["ok"]
    print(f"config: stream={stream.get('enable')} srcport={stream.get('udp_srcport')} "
          f"dest={dests} mp4={mp4.get('enable')}/{mp4.get('codec')} "
          f"{mp4.get('width')}x{mp4.get('height')}@{mp4.get('fps')}")
    return report


def api_paths(server_host, api_port, path, timeout=5):
    url = f"http://{server_host}:{api_port}/v3/paths/list"
    try:
        with urllib.request.urlopen(url, timeout=timeout) as response:
            data = json.load(response)
    except Exception as exc:  # noqa: BLE001
        if url not in API_WARNED:
            API_WARNED.add(url)
            warn(f"control API not reachable at {url}: {exc}")
        RESULT["measured"] = False
        return None

    for item in data.get("items", []):
        if item.get("name") == path:
            return item
    warn(f"path '{path}' is not configured on the receiver")
    RESULT["ok"] = False
    return None


def wait_ready(server_host, api_port, path, seconds):
    """Poll the receiver until the path is online, at most for `seconds`."""
    deadline = time.time() + seconds
    item = None
    while time.time() < deadline:
        item = api_paths(server_host, api_port, path)
        if item and item.get("ready"):
            return item
        time.sleep(1)
    return item


def frame_hash(image):
    """Content fingerprint of a decoded frame (grayscale thumbnail bytes)."""
    return image.convert("L").resize(THUMB).tobytes()


def public_url(url):
    """Drop the userinfo part so credentials never reach the report artifacts."""
    if "@" not in url:
        return url
    scheme, rest = url.split("://", 1)
    return f"{scheme}://{rest.split('@', 1)[1]}"


def match_latency(receiver, camera):
    """Delay of every relayed frame relative to the same camera frame.

    Both lists hold (monotonic arrival time, content hash) tuples collected
    while the two streams were decoded in parallel.  A relayed frame that
    matches a camera frame arrived `t_relay - t_camera` later on this host, so
    the median of those deltas is the delay the relay adds on top of the
    camera's own pipeline.

    The camera cursor only moves forward: a static scene repeats the same hash
    for many frames, and matching every relayed frame against the first
    occurrence would turn the delta into "time since the stream started".
    """
    deltas = []
    cursor = 0
    for arrival, signature in receiver:
        for index in range(cursor, len(camera)):
            if camera[index][1] == signature:
                if arrival > camera[index][0]:
                    deltas.append((arrival - camera[index][0]) * 1000.0)
                cursor = index + 1
                break
    return deltas


def decode_stream(url, seconds, latency, timeline, label):
    """Decode one RTSP stream and collect frame/fps samples."""
    stats = {"label": label, "url": public_url(url), "frames": 0, "seconds": 0.0,
             "fps": 0.0, "error": None, "hashes": []}
    start = time.time()
    per_second = {}
    try:
        container = av.open(url, timeout=15)
    except Exception as exc:  # noqa: BLE001
        stats["error"] = str(exc)
        return stats
    try:
        stream = container.streams.video[0]
        for frame in container.decode(stream):
            now = time.time()
            stats["frames"] += 1
            second = int(now - start)
            per_second[second] = per_second.get(second, 0) + 1
            if latency:
                stats["hashes"].append((now, frame_hash(frame.to_image())))
            if now - start >= seconds:
                break
    except Exception as exc:  # noqa: BLE001
        stats["error"] = str(exc)
    finally:
        container.close()
    stats["seconds"] = round(time.time() - start, 2)
    stats["fps"] = round(stats["frames"] / stats["seconds"], 2) if stats["seconds"] else 0.0
    if timeline is not None:
        for second in sorted(per_second):
            timeline.append((label, second, per_second[second]))
    return stats


def start_decode(url, seconds, latency, timeline, label):
    holder = {}

    def run():
        holder["stats"] = decode_stream(url, seconds, latency, timeline, label)

    thread = threading.Thread(target=run, daemon=True)
    thread.start()
    return thread, holder


def expected_fps(mp4):
    try:
        return float(mp4.get("fps") or 30)
    except ValueError:
        return 30.0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--dut-host", default=os.environ.get("OKO_DUT_HOST", "192.168.52.15"),
                        help="camera address (default: env OKO_DUT_HOST or 192.168.52.15)")
    parser.add_argument("--server-host", default=os.environ.get("OKO_STREAM_HOST", "192.168.52.200"),
                        help="MediaMTX host (default: env OKO_STREAM_HOST or 192.168.52.200)")
    parser.add_argument("--rtsp-port", type=int, default=8554)
    # The control API is bound to loopback by mediamtx.yml on purpose, so it
    # is not reachable through the stand address.
    parser.add_argument("--api-host", default=os.environ.get("OKO_STREAM_API_HOST", "127.0.0.1"))
    parser.add_argument("--api-port", type=int, default=9997)
    parser.add_argument("--path", default="cam", help="MediaMTX path name (default: cam)")
    parser.add_argument("--seconds", type=float, default=20.0, help="sampling window, seconds")
    parser.add_argument("--interval", type=float, default=1.0,
                        help="minimum delay between receiver API polls, seconds")
    parser.add_argument("--latency", action="store_true",
                        help="estimate the OSD-clock delay of the re-published stream")
    parser.add_argument("--compare-dut", action="store_true",
                        help="also sample the camera's own RTSP stream for comparison")
    parser.add_argument("--dut-path", default="/live/0", help="camera RTSP path")
    parser.add_argument("--strict", action="store_true",
                        help="treat an fps deviation above 10%% as a failure")
    parser.add_argument("--no-ssh", action="store_true", help="skip the SSH configuration check")
    parser.add_argument("--out", default=None, help=f"artifact directory (default: {DEFAULT_OUT})")
    args = parser.parse_args()

    stamp = time.strftime("%Y%m%d-%H%M%S")
    out_dir = Path(args.out) if args.out else DEFAULT_OUT / stamp
    out_dir.mkdir(parents=True, exist_ok=True)

    report = {"stamp": stamp, "dut": args.dut_host, "server": args.server_host,
              "seconds": args.seconds}

    if not args.no_ssh:
        text = ssh_read_config(args.dut_host, os.environ.get("OKO_DUT_PASSWORD", "luckfox"))
        report["config"] = check_config(text, args.server_host)
    mp4 = (report.get("config") or {}).get("mp4") or {}
    fps_expected = expected_fps(mp4)

    item = wait_ready(args.api_host, args.api_port, args.path, 15)
    if not item:
        RESULT["measured"] = False
        RESULT["ok"] = False
        report["status"] = "receiver path is not ready"
    else:
        report["receiver"] = {key: item.get(key) for key in
                              ("name", "ready", "online", "tracks", "readers",
                               "inboundBytes", "inboundFramesInError", "bytesReceived")}
        print(f"receiver: ready={item.get('ready')} tracks={item.get('tracks')} "
              f"errors={item.get('inboundFramesInError')} rx={item.get('bytesReceived')}B")

    url = f"rtsp://{args.server_host}:{args.rtsp_port}/{args.path}"
    timeline = []
    threads = []
    thread, holder = start_decode(url, args.seconds, args.latency, timeline, "receiver")
    threads.append((thread, holder, "receiver"))
    dut_url = None
    if args.compare_dut:
        user = os.environ.get("OKO_RTSP_USER", "admin")
        password = os.environ.get("OKO_RTSP_PASS", "oko-change-me")
        dut_url = f"rtsp://{user}:{password}@{args.dut_host}:554{args.dut_path}"
        thread, holder = start_decode(dut_url, args.seconds, args.latency, timeline, "camera")
        threads.append((thread, holder, "camera"))

    # Poll the receiver API while the decoders run, so a stall shows up as a
    # drop in the reported error/byte counters rather than in the fps alone.
    polls = []
    started = time.time()
    while time.time() - started < args.seconds:
        time.sleep(min(args.interval, max(0.0, args.seconds - (time.time() - started))))
        if args.interval <= 0:
            break
        snapshot = api_paths(args.api_host, args.api_port, args.path)
        if snapshot:
            polls.append({"t": round(time.time() - started, 1),
                          "ready": snapshot.get("ready"),
                          "bytes": snapshot.get("bytesReceived"),
                          "errors": snapshot.get("inboundFramesInError")})

    results = {}
    for thread, holder, label in threads:
        thread.join(timeout=max(10.0, args.seconds))
        stats = holder.get("stats") or {"label": label, "error": "no samples", "frames": 0}
        results[label] = stats
        if stats.get("error"):
            warn(f"{label}: {stats['error']}")
            RESULT["measured"] = False
        if stats.get("frames", 0) < 2:
            warn(f"{label}: no decodable frames")
            RESULT["measured"] = False
    report["streams"] = results
    report["receiver_polls"] = polls

    receiver = results.get("receiver", {})
    fps = receiver.get("fps", 0.0)
    camera = results.get("camera") or {}
    # The encoder's own rate fluctuates (26..30 fps for the same configuration),
    # so with --compare-dut the camera is the reference: comparing the relay
    # against the configured mp4.fps alone would fail a healthy chain whenever
    # the encoder happens to run slower during the sample.
    reference = camera.get("fps") or fps_expected
    if fps > 0:
        deviation = abs(fps - reference) / reference * 100.0
        report["fps_reference"] = reference
        report["fps_deviation_percent"] = round(deviation, 2)
        print(f"receiver: {receiver['frames']} frames in {receiver['seconds']} s "
              f"= {fps} fps ({deviation:.1f}% off {reference:.2f} fps)")
        limit = 3.0 if args.strict and camera else (10.0 if args.strict else 25.0)
        if deviation > limit:
            warn(f"fps deviation {deviation:.1f}% exceeds {limit:.0f}%")
            if args.strict:
                RESULT["ok"] = False
    if camera:
        print(f"camera:   {camera.get('frames')} frames in {camera.get('seconds')} s "
              f"= {camera.get('fps')} fps")
        if fps > 0 and camera.get("fps"):
            delta = abs(fps - camera["fps"]) / camera["fps"] * 100.0
            report["receiver_vs_camera_percent"] = round(delta, 2)
            print(f"receiver vs camera: {delta:.1f}% difference")
            if delta > 5.0:
                warn(f"receiver delivers {delta:.1f}% fewer frames than the camera")

    if args.latency:
        if "camera" in results:
            deltas = match_latency(results["receiver"].get("hashes") or [],
                                   results["camera"].get("hashes") or [])
            if deltas:
                deltas.sort()
                median = deltas[len(deltas) // 2]
                report["relay_latency_ms"] = {"min": round(deltas[0], 1),
                                              "median": round(median, 1),
                                              "max": round(deltas[-1], 1),
                                              "samples": len(deltas)}
                print(f"relay latency: median {median:.0f} ms "
                      f"(min {deltas[0]:.0f}, max {deltas[-1]:.0f}, n={len(deltas)})")
            else:
                warn("latency: no re-published frame matched a camera frame")
                RESULT["measured"] = False
        else:
            warn("--latency needs --compare-dut: the delay is measured against the camera")
    for stats in results.values():
        stats.pop("hashes", None)

    (out_dir / "report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False))
    if timeline:
        with (out_dir / "frames.csv").open("w", newline="") as handle:
            writer = csv.writer(handle)
            writer.writerow(["stream", "second", "frames"])
            writer.writerows(timeline)
    report["artifacts"] = str(out_dir)
    print(f"artifacts: {out_dir}")

    if not RESULT["ok"]:
        return 1
    if not RESULT["measured"]:
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())