#!/usr/bin/env python3
"""Check /api/firmware and /api/flash; optionally flash a controller.

    bench/run.sh firmware_check.py
    bench/run.sh firmware_check.py --flash --target stm8 \
        --file project/app/stm8-motor/build/app-2.0.1.ihx --confirm FLASH

The flash path is deliberately explicit: both --flash and --confirm FLASH are
required.  The version is taken from the file name (app-1.0.1.bin,
app-2.0.1.ihx); an unversioned name is rejected by the server.
"""
import argparse
import json
import os
import shutil
import sys
import tempfile
import time
import uuid

import api_check


def json_request(path, method="GET", body=None, ctype=None):
    status, headers, payload = api_check.request(path, method, body, ctype)
    try:
        return status, json.loads(payload.decode())
    except Exception:                              # noqa: BLE001
        return status, {"error": "invalid JSON",
                        "raw": payload[:200].decode(errors="replace")}


def multipart(fields, file_field, file_path):
    boundary = "----oko" + uuid.uuid4().hex
    parts = []
    for name, value in fields.items():
        parts.append(("--%s\r\nContent-Disposition: form-data; name=\"%s\"\r\n\r\n%s\r\n"
                      % (boundary, name, value)).encode())
    name = os.path.basename(file_path)
    with open(file_path, "rb") as fh:
        data = fh.read()
    parts.append(("--%s\r\nContent-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\n"
                  "Content-Type: application/octet-stream\r\n\r\n"
                  % (boundary, file_field, name)).encode())
    parts.append(data)
    parts.append(("\r\n--%s--\r\n" % boundary).encode())
    return b"".join(parts), "multipart/form-data; boundary=" + boundary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--flash", action="store_true")
    parser.add_argument("--target", choices=["stm32", "stm8"], default="stm8")
    parser.add_argument("--file")
    parser.add_argument("--confirm")
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--negative", action="store_true",
        help="run upload validation checks (400/blocked) without flashing")
    args = parser.parse_args()

    status, fw = json_request("/api/firmware")
    if status != 200 or fw.get("error"):
        print("FAIL /api/firmware: HTTP %s %r" % (status, fw), file=sys.stderr)
        return 1
    for side in ("stm32", "stm8"):
        item = fw.get(side, {})
        print("%s: present=%s state=%s version=%s boot=%s age=%s" % (
            side, item.get("present"), item.get("state"), item.get("version"),
            item.get("boot_version"), item.get("age")))

    status, flash = json_request("/api/flash")
    if status != 200 or flash.get("error"):
        print("FAIL /api/flash: HTTP %s %r" % (status, flash), file=sys.stderr)
        return 1
    print("flash: enable=%s busy=%s targets=%s" % (
        flash.get("enable"), flash.get("busy"),
        ",".join(t.get("name", "?") for t in flash.get("targets", []))))

    if args.negative:
        return negative_checks()
    if not args.flash:
        return 0
    if not args.file or args.confirm != "FLASH":
        parser.error("--flash needs --file and --confirm FLASH")
    body, ctype = multipart({"target": args.target, "confirm": "FLASH"},
                            "file", args.file)
    status, job = json_request("/api/flash", "POST", body, ctype)
    if status not in (200, 202) or job.get("error"):
        print("FAIL POST /api/flash: HTTP %s %r" % (status, job), file=sys.stderr)
        return 1

    deadline = time.time() + args.timeout
    while time.time() < deadline:
        status, dec = json_request("/api/flash")
        current = dec.get("job") or {}
        print("job: state=%s progress=%s message=%s" % (
            current.get("state"), current.get("progress"),
            current.get("message")))
        if current.get("state") in ("done", "error"):
            return 0 if current.get("state") == "done" else 1
        time.sleep(2)
    print("FAIL: flash job timed out", file=sys.stderr)
    return 1


def post_upload(fields, file_path, filename=None):
    body, ctype = multipart(fields, "file", file_path)
    if filename:
        # Repack with the requested name: multipart() uses basename.
        body = body.replace(os.path.basename(file_path).encode(),
                            filename.encode(), 1)
    status, dec = json_request("/api/flash", "POST", body, ctype)
    return status, dec


def negative_checks():
    failures = 0
    app = os.path.join("project", "app", "stm8-motor", "build", "app-2.0.1.ihx")
    if not os.path.exists(app):
        print("FAIL: missing %s (build the STM8 firmware first)" % app,
              file=sys.stderr)
        return 1
    tmp = tempfile.mkdtemp(prefix="oko-flash-neg-")
    # Every test name carries a version triple so the specific check under
    # test is reached; "plain" deliberately omits it.
    bad = os.path.join(tmp, "bad-2.0.1.ihx")
    big = os.path.join(tmp, "big-2.0.1.bin")
    slot = os.path.join(tmp, "slot-1.0.1.bin")
    wrong = os.path.join(tmp, "wrong-2.0.1.txt")
    plain = os.path.join(tmp, "app.bin")
    mismatch = os.path.join(tmp, "app-9.9.9.ihx")
    with open(bad, "wb") as fh:
        fh.write(b"not an OKO image\n")
    with open(big, "wb") as fh:
        fh.write(b"\0" * (300 * 1024))
    with open(slot, "wb") as fh:
        fh.write(b"\0" * (130 * 1024))
    with open(wrong, "wb") as fh:
        fh.write(b"x")
    with open(plain, "wb") as fh:
        fh.write(b"x")

    shutil.copyfile(app, mismatch)
    cases = [
        ("missing confirm", {"target": "stm8"}, app, 400),
        ("unknown target", {"target": "foo", "confirm": "FLASH"}, wrong, 400),
        ("no version in name", {"target": "stm32", "confirm": "FLASH"},
         plain, 400),
        ("wrong extension", {"target": "stm8", "confirm": "FLASH"}, wrong, 400),
        ("invalid STM8 image", {"target": "stm8", "confirm": "FLASH"}, bad, 400),
        ("oversized upload", {"target": "stm8", "confirm": "FLASH"}, big, 400),
        ("STM32 wrong extension", {"target": "stm32", "confirm": "FLASH"},
         bad, 400),
        ("STM32 image over slot", {"target": "stm32", "confirm": "FLASH"},
         slot, 400),
    ]
    cases.append(("STM8 name mismatch", {"target": "stm8",
                                         "confirm": "FLASH"},
                  mismatch, 400))
    for name, fields, path, expected in cases:
        status, dec = post_upload(fields, path)
        ok = status == expected
        print("%-24s HTTP %s (expected %s) %s" % (
            name, status, expected, "ok" if ok else "FAIL"))
        if not ok:
            print("  %r" % (dec,), file=sys.stderr)
            failures += 1

    for path in (bad, big, slot, wrong, plain, mismatch):
        if os.path.exists(path):
            os.unlink(path)
    os.rmdir(tmp)
    print("negative checks: %d failure(s)" % failures)
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
