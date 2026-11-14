#!/usr/bin/env python3
"""KI-005: night YOLO accuracy, AIISP on/off x yolo.source vpss3/vi0.

The script drives the bench DUT over SSH + HTTP, collects the inference
history from `/api/yolo?detections=1` (added for this measurement) and scores
it against a ground-truth file:

    bench/run.sh yolo_night.py --gt output/bench/yolo_night/gt.json

Protocol (planned in .kilo/plans/*ki005-night-yolo-accuracy-plan.md):

  C1 vpss3 + AIISP off   C2 vpss3 + AIISP on
  C3 vi0   + AIISP on    C4 vi0   + AIISP off

Night runs C1..C4 (`--repeat` passes each); the day control runs C1 and C4
through the same pipeline and proves the scene is stable (the mean score of
the two must differ by at most 0.05).

`--phase ambient` runs C1..C4 without touching `/api/night`, for boards that
have no wired IR-cut/IR-LED: with `night_mode.enable=false` the config keeps
pins at 999, so `/api/night` reports the requested state while the hardware
does nothing.  Numbers from this mode are daytime numbers and must not be
presented as night accuracy.

Every configuration is written to `/userdata/divinus.yaml` and applied by a
graceful restart (`kill -HUP`), because `yolo.source=vi0` can only take effect
when the pipeline is created.  The previous file is kept as
`/userdata/divinus.yaml.oko-yolo-night.bak` and restored at the end unless
`--keep-config` is given.

Ground truth (`--gt`) is a JSON file with boxes in the 0..10000 normalisation
of the detector:

    {"objects": [{"name": "person", "cls": 0,
                  "x": 1000, "y": 2000, "w": 800, "h": 1500}]}

`--capture-gt` saves the current frame and a template so the boxes can be
annotated once; the camera and the objects must not move between day and
night.  Artifacts go to `output/bench/yolo_night/` (gitignored).

Environment: OKO_DUT_HOST (default 192.168.52.33), OKO_DUT_PASSWORD
(default `luckfox`), OKO_RTSP_USER / OKO_RTSP_PASS (HTTP Basic auth, vendor
defaults from TESTBENCH.md).

Exit codes: 0 measurement complete, 1 invalid/unsatisfied measurement,
2 setup or transport failure.
"""
import argparse
import base64
import csv
import json
import os
import re
import shlex
import statistics
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

import paramiko

HOST = os.environ.get("OKO_DUT_HOST", "192.168.52.33")
SSH_USER = os.environ.get("OKO_DUT_USER", "root")
SSH_PASSWORD = os.environ.get("OKO_DUT_PASSWORD", "luckfox")
HTTP_USER = os.environ.get("OKO_RTSP_USER", "admin")
HTTP_PASSWORD = os.environ.get("OKO_RTSP_PASS", "oko-change-me")

CONFIG_PATH = "/userdata/divinus.yaml"
BACKUP_PATH = CONFIG_PATH + ".oko-yolo-night.bak"
BINARY_LOCAL = "project/app/divinus/source/divinus"
BINARY_REMOTE = "/oem/usr/bin/divinus"

CONFIGS = {
    "C1": ("vpss3", "off"),
    "C2": ("vpss3", "on"),
    "C3": ("vi0", "on"),
    "C4": ("vi0", "off"),
}
DAY_CONFIGS = ("C1", "C4")


# --------------------------------------------------------------------------
# Transport
# --------------------------------------------------------------------------

def http(path, method="GET", timeout=15):
    req = urllib.request.Request("http://%s%s" % (HOST, path), method=method)
    token = base64.b64encode(
        ("%s:%s" % (HTTP_USER, HTTP_PASSWORD)).encode()).decode()
    req.add_header("Authorization", "Basic " + token)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.status, resp.read()
    except urllib.error.HTTPError as err:
        return err.code, err.read()
    except Exception as err:                       # noqa: BLE001 - report it
        return 0, str(err).encode()


def http_json(path, method="GET", timeout=15):
    status, body = http(path, method, timeout)
    try:
        return status, json.loads(body.decode(errors="replace"))
    except Exception:                              # noqa: BLE001
        return status, {}


def ssh_connect():
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(HOST, username=SSH_USER, password=SSH_PASSWORD, timeout=10)
    return client


def ssh_run(client, cmd, timeout=60):
    _, out, err = client.exec_command(cmd, timeout=timeout)
    text = (out.read() + err.read()).decode(errors="replace").strip()
    status = out.channel.recv_exit_status()
    if status:
        print("$ %s\n%s" % (cmd, text))
    return status, text


# --------------------------------------------------------------------------
# divinus.yaml (flat subset, same layout as files/divinus.yaml)
# --------------------------------------------------------------------------

HEADER_RE = re.compile(r"^(#[ ]?)?([A-Za-z_][A-Za-z0-9_]*)\s*:")


def section_bounds(lines, section):
    """Return (header index, body end) for a top-level section, or None."""
    for i, line in enumerate(lines):
        match = HEADER_RE.match(line)
        if match and match.group(2) == section:
            for j in range(i + 1, len(lines)):
                if HEADER_RE.match(lines[j]):
                    return i, j
            return i, len(lines)
    return None


def yaml_set(text, section, key, value):
    """Set a flat key of a top-level section, uncommenting it when needed."""
    lines = text.splitlines()
    bounds = section_bounds(lines, section)
    if bounds is None:
        while lines and not lines[-1].strip():
            lines.pop()
        lines += ["", "%s:" % section, "  %s: %s" % (key, value), ""]
        return "\n".join(lines) + "\n"

    start, end = bounds
    header = HEADER_RE.match(lines[start])
    if header.group(1):
        lines[start] = lines[start][len(header.group(1)):]

    key_re = re.compile(r"^\s*(#\s*)?%s\s*:" % re.escape(key))
    for i in range(start + 1, end):
        match = key_re.match(lines[i])
        if not match:
            continue
        old = lines[i]
        comment = ""
        hash_pos = old.find("#", match.end())
        if hash_pos >= 0:
            comment = "  # " + old[hash_pos + 1:].strip()
        lines[i] = "  %s: %s%s" % (key, value, comment)
        return "\n".join(lines) + "\n"

    lines.insert(start + 1, "  %s: %s" % (key, value))
    return "\n".join(lines) + "\n"


def yaml_get(text, section, key):
    """Return the raw value of a key, for diagnostics."""
    lines = text.splitlines()
    bounds = section_bounds(lines, section)
    if bounds is None:
        return None
    start, end = bounds
    key_re = re.compile(r"^\s*(#\s*)?%s\s*:\s*(.*)$" % re.escape(key))
    for i in range(start + 1, end):
        match = key_re.match(lines[i])
        if match:
            return match.group(2).split("#", 1)[0].strip()
    return None


def read_config(sftp):
    with sftp.open(CONFIG_PATH, "r") as handle:
        return handle.read().decode(errors="replace")


def write_config(sftp, text):
    with sftp.open(CONFIG_PATH, "w") as handle:
        handle.write(text)


def apply_config(client, sftp, current, changes):
    text = current
    for section, key, value in changes:
        text = yaml_set(text, section, key, value)
    write_config(sftp, text)
    return text


# --------------------------------------------------------------------------
# Service control and configuration verification
# --------------------------------------------------------------------------

def restart(client, timeout=90, expect_yolo=True):
    # pidof also lists leftovers stuck in D/Z state (an aborted media
    # teardown); signalling those does nothing and the real process keeps
    # running with the old config, so pick a live PID explicitly.
    status, pid = ssh_run(
        client,
        "for p in $(pidof divinus); do "
        "s=$(cut -d' ' -f3 /proc/$p/stat 2>/dev/null); "
        "case \"$s\" in R|S) echo $p; break;; esac; done")
    if status or not pid:
        raise RuntimeError("no live divinus process on the DUT")
    ssh_run(client, "kill -HUP %s" % pid.split()[0])
    deadline = time.time() + timeout
    while time.time() < deadline:
        time.sleep(1.0)
        code, dec = http_json("/api/yolo", timeout=5)
        if code == 200 and (not expect_yolo or dec.get("running") is True):
            return
    raise RuntimeError("divinus did not come back with the detector running")


def deploy(client, sftp):
    if not os.path.exists(BINARY_LOCAL):
        raise RuntimeError("missing %s: build divinus first" % BINARY_LOCAL)
    remote_new = "/userdata/divinus.new"
    sftp.put(BINARY_LOCAL, remote_new)
    ssh_run(client, "cp %s /userdata/divinus.old" % BINARY_REMOTE)
    status, _ = ssh_run(client, "cp %s %s && chmod 755 %s" %
                        (remote_new, BINARY_REMOTE, BINARY_REMOTE))
    if status:
        raise RuntimeError("cannot install %s on the DUT" % BINARY_REMOTE)


def preflight(client, sftp):
    """Refuse to rewrite the config when the detector cannot start anyway.

    The model is not part of the firmware image; without it yolo_start()
    fails and every configuration would waste a restart and a timeout.
    When the configured path is missing but exactly one model is installed,
    activate it: activation probes the runtime and saves the real path, which
    a reboot may otherwise roll back to the factory default.
    """
    text = read_config(sftp)
    model = yaml_get(text, "yolo", "model") or "/userdata/models/yolov5n.rknn"
    status, _ = ssh_run(client, "test -r %s" % shlex.quote(model))
    if status:
        code, listing = http_json("/api/models")
        if code != 200:
            raise RuntimeError("/api/models is not available: HTTP %s" % code)
        names = [m.get("name") for m in listing.get("models", [])
                 if m.get("name")]
        if len(names) != 1:
            raise RuntimeError(
                "the YOLO model is missing on the DUT: %s\n"
                "convert it with tools/rknn-toolkit2, upload it with "
                "POST /api/models and keep exactly one model installed" % model)
        name = names[0]
        code, dec = http_json("/api/models/activate?name=" + urllib.parse.quote(name),
                              method="POST", timeout=120)
        if code != 200 or not dec.get("activated"):
            raise RuntimeError("cannot activate %s: HTTP %s %r" %
                               (name, code, dec))
        print("activated model: %s" % name)
        text = read_config(sftp)
        model = yaml_get(text, "yolo", "model") or "/userdata/models/" + name
        status, _ = ssh_run(client, "test -r %s" % shlex.quote(model))
    if status:
        raise RuntimeError(
            "the YOLO model is missing on the DUT: %s\n"
            "convert it with tools/rknn-toolkit2 and install it under "
            "/userdata/models (KI-003)" % model)


def save_config():
    """Persist the in-memory config, so a later reboot does not roll it back."""
    code, dec = http_json("/api/cmd?save", timeout=30)
    if code != 200:
        raise RuntimeError("/api/cmd?save failed: HTTP %s %r" % (code, dec))


def verify_settings(source, aiisp):
    code, yolo = http_json("/api/yolo")
    if code != 200 or yolo.get("running") is not True:
        return "detector is not running: HTTP %s %r" % (code, yolo)
    if yolo.get("source") != source:
        return "yolo.source is %r, expected %r" % (yolo.get("source"), source)
    if yolo.get("enable") is not True:
        return "yolo.enable is not true"
    code, aiisp_dec = http_json("/api/aiisp")
    if code != 200 or aiisp_dec.get("enable") != aiisp:
        return "aiisp.enable is %r, expected %r" % (
            aiisp_dec.get("enable"), aiisp)
    for path in ("/api/ivs", "/api/mjpeg"):
        _code, dec = http_json(path)
        if dec.get("enable"):
            return "%s is enabled: it forces yolo.source=vi0" % path
    return None


def set_scene(day):
    """Switch IR-cut / IR LEDs / grayscale for the phase.

    Day: the IR-cut filter is engaged and the illuminator is off.
    Night: the filter is removed, the illuminator is on, the stream is grey.
    """
    code, state = http_json("/api/night")
    if code != 200:
        raise RuntimeError("/api/night is not available: HTTP %s" % code)
    if state.get("ir_cut_pin1") == 999 or state.get("irled_pin") == 999:
        raise RuntimeError(
            "IR-cut/IR-LED pins are not configured (999): no night hardware "
            "on this board; use --phase ambient and keep the numbers as "
            "daytime measurements")

    query = "enable=1&manual=1&grayscale=%d&ircut=%d&irled=%d" % (
        0 if day else 1, 1 if day else 0, 0 if day else 1)
    code, dec = http_json("/api/night?" + query)
    if code != 200:
        raise RuntimeError("/api/night failed: HTTP %s %r" % (code, dec))
    expected = {"grayscale": not day, "ircut": day, "irled": not day,
                "manual": True, "active": True}
    wrong = {key: dec.get(key) for key, value in expected.items()
             if dec.get(key) != value}
    if wrong:
        raise RuntimeError("night mode did not apply: %r (got %r)" %
                           (wrong, dec))
    print("scene: %s (ircut=%s irled=%s grayscale=%s)" %
          ("day" if day else "night", dec.get("ircut"), dec.get("irled"),
           dec.get("grayscale")))


# --------------------------------------------------------------------------
# Detection collection
# --------------------------------------------------------------------------

def collect(seconds, poll, expected_source, expected_aiisp):
    records, seen, since = [], set(), 0
    deadline = time.monotonic() + seconds
    errors = 0
    while time.monotonic() < deadline:
        code, dec = http_json("/api/yolo?detections=1&since=%d" % since,
                              timeout=10)
        if code != 200 or not isinstance(dec.get("detections"), list):
            errors += 1
            time.sleep(poll)
            continue
        for record in dec["detections"]:
            seq = record.get("seq")
            if seq in seen:
                continue
            seen.add(seq)
            records.append(record)
            if record.get("source") != expected_source:
                raise RuntimeError("record %s came from %r, expected %r" %
                                   (seq, record.get("source"),
                                    expected_source))
            if record.get("aiisp") != expected_aiisp:
                raise RuntimeError("record %s used aiisp=%r, expected %r" %
                                   (seq, record.get("aiisp"), expected_aiisp))
        last = dec.get("last")
        if isinstance(last, int) and last > since:
            since = last
        time.sleep(poll)
    return records, errors


def snapshot(path):
    code, body = http("/image.jpg", timeout=20)
    if code != 200 or not body.startswith(b"\xff\xd8"):
        return False
    with open(path, "wb") as handle:
        handle.write(body)
    return True


# --------------------------------------------------------------------------
# Metrics
# --------------------------------------------------------------------------

def iou(a, b):
    ax2, ay2 = a["x"] + a["w"], a["y"] + a["h"]
    bx2, by2 = b["x"] + b["w"], b["y"] + b["h"]
    ix = max(0, min(ax2, bx2) - max(a["x"], b["x"]))
    iy = max(0, min(ay2, by2) - max(a["y"], b["y"]))
    inter = ix * iy
    union = a["w"] * a["h"] + b["w"] * b["h"] - inter
    return inter / union if union > 0 else 0.0


def score_run(records, gt, iou_threshold):
    """Match detections to ground truth and return per-run metrics."""
    objects = []
    for item in gt:
        objects.append({"name": item.get("name", "class %s" % item["cls"]),
                        "cls": int(item["cls"]),
                        "box": {"x": int(item["x"]), "y": int(item["y"]),
                                "w": int(item["w"]), "h": int(item["h"])},
                        "hit_frames": 0, "best_score": None})

    frames = len(records)
    fp_frames = 0
    det_total = 0
    for record in records:
        dets = record.get("dets") or []
        det_total += len(dets)
        used = set()
        matched = 0
        for det in sorted(dets, key=lambda d: d.get("score", 0), reverse=True):
            best_iou, best_obj = 0.0, None
            for index, obj in enumerate(objects):
                if index in used or obj["cls"] != det.get("cls"):
                    continue
                value = iou(obj["box"], {"x": det["x"], "y": det["y"],
                                         "w": det["w"], "h": det["h"]})
                if value > best_iou:
                    best_iou, best_obj = value, index
            if best_obj is not None and best_iou >= iou_threshold:
                used.add(best_obj)
                obj = objects[best_obj]
                obj["hit_frames"] += 1
                score = float(det.get("score", 0.0))
                if obj["best_score"] is None or score > obj["best_score"]:
                    obj["best_score"] = score
                matched += 1
        if len(dets) > matched:
            fp_frames += 1

    for obj in objects:
        obj["detection_rate"] = (obj["hit_frames"] / frames) if frames else 0.0
    hits = [obj for obj in objects if obj["hit_frames"] > 0]
    scored = [obj["best_score"] for obj in objects
              if obj["best_score"] is not None]
    return {
        "frames": frames,
        "detections": det_total,
        "detections_per_frame": det_total / frames if frames else 0.0,
        "empty_frames": sum(1 for r in records if not r.get("dets")),
        "recall": len(hits) / len(objects) if objects else 0.0,
        "mean_detection_rate": statistics.mean(
            obj["detection_rate"] for obj in objects) if objects else 0.0,
        "mean_best_score": statistics.mean(scored) if scored else 0.0,
        "fp_frame": fp_frames / frames if frames else 0.0,
        "objects": [{"name": obj["name"], "cls": obj["cls"],
                     "detection_rate": obj["detection_rate"],
                     "best_score": obj["best_score"]} for obj in objects],
    }


def summarize(runs):
    groups = {}
    for run in runs:
        if "metrics" not in run:
            continue
        groups.setdefault((run["phase"], run["config"]), []).append(
            run["metrics"])
    summary = []
    for (phase, config), metrics in sorted(groups.items()):
        summary.append({
            "phase": phase,
            "config": config,
            "source": CONFIGS[config][0],
            "aiisp": CONFIGS[config][1],
            "runs": len(metrics),
            "recall": statistics.mean(m["recall"] for m in metrics),
            "detection_rate": statistics.mean(
                m["mean_detection_rate"] for m in metrics),
            "mean_best_score": statistics.mean(
                m["mean_best_score"] for m in metrics),
            "fp_frame": statistics.mean(m["fp_frame"] for m in metrics),
        })
    return summary


def write_csv(path, runs):
    fields = ["phase", "config", "source", "aiisp", "repeat", "seconds",
              "records", "recall", "detection_rate", "mean_best_score",
              "fp_frame",
              "detections_per_frame", "empty_frames"]
    with open(path, "w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        for run in runs:
            row = dict(run)
            row.update(run.get("metrics") or {})
            writer.writerow(row)


# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------

def load_gt(path):
    with open(path, "r") as handle:
        data = json.load(handle)
    objects = data["objects"] if isinstance(data, dict) else data
    if not objects:
        raise ValueError("ground truth has no objects")
    for item in objects:
        for key in ("cls", "x", "y", "w", "h"):
            if key not in item:
                raise ValueError("ground truth object misses %r: %r" %
                                 (key, item))
        if item["w"] <= 0 or item["h"] <= 0:
            raise ValueError("ground truth box has no area: %r" % item)
    return objects


def gt_template():
    return {
        "note": "boxes are normalised to the model input: x,y,w,h in 0..10000",
        "objects": [
            {"name": "example", "cls": 0,
             "x": 1000, "y": 2000, "w": 800, "h": 1500},
        ],
    }


def run_one(client, sftp, config, phase, repeat, args, gt):
    source, aiisp = CONFIGS[config]
    current = read_config(sftp)
    apply_config(client, sftp, current,
                 [("yolo", "enable", "true"),
                  ("yolo", "source", source),
                  ("yolo", "boxes", "false"),
                  ("yolo", "fps", str(args.fps)),
                  ("aiisp", "enable", aiisp),
                  ("ivs", "enable", "false"),
                  ("mjpeg", "enable", "false")])
    restart(client)
    reason = verify_settings(source, aiisp)
    if reason:
        raise RuntimeError("configuration %s is not active: %s" %
                           (config, reason))
    save_config()

    if phase == "ambient":
        print("scene: ambient (no /api/night control)")
    else:
        set_scene(phase == "day")
    print("settling %d s (%s %s/%s)" % (args.settle, phase, config, source))
    time.sleep(args.settle)

    label = "raw-%s-%s-r%d" % (phase, config, repeat)
    if args.snapshots:
        snapshot(os.path.join(args.out, "snap-%s-before.jpg" % label))

    started = time.strftime("%Y-%m-%dT%H:%M:%S")
    records, errors = collect(args.seconds, args.poll, source, aiisp)
    stopped = time.strftime("%Y-%m-%dT%H:%M:%S")

    with open(os.path.join(args.out, label + ".json"), "w") as handle:
        json.dump({"phase": phase, "config": config, "source": source,
                   "aiisp": aiisp, "started": started, "stopped": stopped,
                   "request_errors": errors, "records": records}, handle)

    if args.snapshots:
        snapshot(os.path.join(args.out, "snap-%s-after.jpg" % label))

    run = {"phase": phase, "config": config, "source": source,
           "aiisp": aiisp, "repeat": repeat, "seconds": args.seconds,
           "records": len(records), "request_errors": errors}
    if gt:
        run["metrics"] = score_run(records, gt, args.iou)
        run["detection_rate"] = run["metrics"]["mean_detection_rate"]
    print("%s %s r%d: %d records, %d request errors%s" %
          (phase, config, repeat, len(records), errors,
           "" if not gt else
           ", recall %.2f det %.2f score %.2f fp/frame %.2f" %
           (run["metrics"]["recall"], run["metrics"]["mean_detection_rate"],
            run["metrics"]["mean_best_score"], run["metrics"]["fp_frame"])))
    return run


def main():
    parser = argparse.ArgumentParser(
        description="KI-005 night YOLO accuracy matrix")
    parser.add_argument("--phase", choices=("day", "night", "ambient", "all"),
                        default="all",
                        help="ambient = C1..C4 without touching /api/night; "
                             "for boards without IR hardware")
    parser.add_argument("--configs", default="",
                        help="comma-separated subset of C1,C2,C3,C4")
    parser.add_argument("--seconds", type=float, default=60.0,
                        help="measurement window per run (default 60)")
    parser.add_argument("--repeat", type=int, default=3,
                        help="night repeats per configuration (default 3)")
    parser.add_argument("--fps", type=int, default=5,
                        help="yolo.fps for the measurement (default 5)")
    parser.add_argument("--poll", type=float, default=0.5,
                        help="detection polling period, seconds (default 0.5)")
    parser.add_argument("--settle", type=float, default=45.0,
                        help="settling time after a restart, seconds (45)")
    parser.add_argument("--iou", type=float, default=0.3,
                        help="IoU threshold for a detection match (0.3)")
    parser.add_argument("--gt", default="",
                        help="ground-truth JSON (boxes in 0..10000)")
    parser.add_argument("--out", default=os.path.join("output", "bench",
                                                      "yolo_night"))
    parser.add_argument("--snapshots", action="store_true",
                        help="save /image.jpg before and after every run")
    parser.add_argument("--capture-gt", action="store_true",
                        help="save gt-frame.jpg and a gt.json template")
    parser.add_argument("--deploy", action="store_true",
                        help="copy the freshly built divinus before the run")
    parser.add_argument("--keep-config", action="store_true",
                        help="do not restore the original divinus.yaml")
    parser.add_argument("--dry-run", action="store_true",
                        help="print the plan, do not touch the DUT")
    args = parser.parse_args()

    if args.configs:
        selected = [name.strip() for name in args.configs.split(",")
                    if name.strip()]
        unknown = [name for name in selected if name not in CONFIGS]
        if unknown:
            parser.error("unknown configurations: %s" % ", ".join(unknown))
    else:
        selected = list(CONFIGS)

    phases = ("day", "night") if args.phase == "all" else (args.phase,)
    plan = []
    for phase in phases:
        if phase == "ambient":
            names = [n for n in CONFIGS if n in selected]
            repeats = max(1, args.repeat)
        elif phase == "day":
            names = [n for n in DAY_CONFIGS if n in selected]
            repeats = 1
        else:
            names = [n for n in CONFIGS if n in selected]
            repeats = max(1, args.repeat)
        for config in names:
            for repeat in range(1, repeats + 1):
                plan.append((phase, config, repeat))

    if args.dry_run:
        for phase, config, repeat in plan:
            print("%-5s %s (%s/%s) repeat %d, %g s" %
                  (phase, config, CONFIGS[config][0], CONFIGS[config][1],
                   repeat, args.seconds))
        return 0

    gt = load_gt(args.gt) if args.gt else None
    if not gt:
        print("warning: no --gt, raw data only (metrics are not computed)",
              file=sys.stderr)
    os.makedirs(args.out, exist_ok=True)

    try:
        client = ssh_connect()
    except Exception as err:                       # noqa: BLE001
        print("error: cannot reach %s over SSH: %s" % (HOST, err),
              file=sys.stderr)
        return 2
    sftp = client.open_sftp()
    runs = []
    original = None
    config_changed = False
    try:
        original = read_config(sftp)
        try:
            sftp.stat(BACKUP_PATH)
        except IOError:
            with sftp.open(BACKUP_PATH, "w") as handle:
                handle.write(original)

        if args.deploy:
            deploy(client, sftp)

        if args.capture_gt:
            set_scene(True)
            time.sleep(args.settle)
            frame = os.path.join(args.out, "gt-frame.jpg")
            if not snapshot(frame):
                raise RuntimeError("cannot download /image.jpg")
            with open(os.path.join(args.out, "gt.json"), "w") as handle:
                json.dump(gt_template(), handle, indent=2)
            print("ground-truth frame: %s (annotate gt.json)" % frame)
            return 0

        preflight(client, sftp)

        for phase, config, repeat in plan:
            config_changed = True
            runs.append(run_one(client, sftp, config, phase, repeat, args,
                                gt))
    except (RuntimeError, IOError, OSError, ValueError) as err:
        print("error: %s" % err, file=sys.stderr)
        return 2
    finally:
        if original is not None and config_changed and not args.keep_config:
            try:
                write_config(sftp, original)
                restart(client, timeout=60, expect_yolo=False)
                save_config()
                print("configuration restored")
            except Exception as err:               # noqa: BLE001
                print("warning: cannot restore the configuration: %s" % err,
                      file=sys.stderr)
        sftp.close()
        client.close()

    summary = summarize(runs)
    report = {"generated": time.strftime("%Y-%m-%dT%H:%M:%S"),
              "host": HOST, "phase": args.phase, "seconds": args.seconds,
              "repeat": args.repeat, "fps": args.fps, "iou": args.iou,
              "gt": args.gt or None, "runs": runs, "summary": summary}

    day = {row["config"]: row for row in summary if row["phase"] == "day"}
    if "C1" in day and "C4" in day:
        delta = abs(day["C1"]["mean_best_score"] -
                    day["C4"]["mean_best_score"])
        report["day_control"] = {"score_delta": delta, "valid": delta <= 0.05}
        print("day control: C1 %.3f vs C4 %.3f, delta %.3f (%s)" %
              (day["C1"]["mean_best_score"], day["C4"]["mean_best_score"],
               delta, "valid" if delta <= 0.05 else "UNSTABLE"))

    # Without IR hardware the day control is not available; C1 vs C4 in the
    # ambient matrix still shows how stable the scene was between runs.
    ambient = {row["config"]: row for row in summary
               if row["phase"] == "ambient"}
    if "C1" in ambient and "C4" in ambient:
        delta = abs(ambient["C1"]["mean_best_score"] -
                    ambient["C4"]["mean_best_score"])
        report["ambient_control"] = {"score_delta": delta,
                                     "valid": delta <= 0.05}
        print("ambient control: C1 %.3f vs C4 %.3f, delta %.3f (%s)" %
              (ambient["C1"]["mean_best_score"],
               ambient["C4"]["mean_best_score"], delta,
               "valid" if delta <= 0.05 else "UNSTABLE"))

    with open(os.path.join(args.out, "report.json"), "w") as handle:
        json.dump(report, handle, indent=2)
    write_csv(os.path.join(args.out, "report.csv"), runs)
    print("report: %s" % os.path.join(args.out, "report.json"))

    if any(run["request_errors"] for run in runs):
        print("warning: some polling requests failed", file=sys.stderr)
    if report.get("day_control", {}).get("valid") is False:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())