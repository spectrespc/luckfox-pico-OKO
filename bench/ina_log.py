#!/usr/bin/env python3
"""Poll the ESP32 bridge INA3221 and record the DUT 5 V input power.

The bridge at http://192.168.52.17 (ESP32-OKO-KVM, firmware 1.11.1 on the
bench) measures three 5 V branches with its own INA3221.  The power-off test
of 2026-11-22 showed that **CH3 is the DUT input** (it drops to ~0.08 mA when
`POST /ext/power {"on":false}` is asserted); CH2 powers the bridge itself and
CH1 is a small auxiliary load.  This script therefore logs all channels and
lets the caller pick the interesting one (the bench scenarios use 3).

`sensor` schema (T0, firmware 1.11.1):

    {"ina3221": {"ch1": {"bus_mv": 4776, "shunt_uv": 2120,
                         "cur_01ma": 2120, "pwr_01mw": 10125}, ...}}

`cur_01ma` is in 0.01 mA, `pwr_01mw` in 0.01 mW, so the INA3221 LSB is
0.4 mA with the 100 mOhm shunt.  One GET costs ~50 ms over a keep-alive
connection (~130 ms when reconnecting), so 2 Hz is comfortable.

The bridge needs a cookie session and keeps at most three of them alive for
24 h; the logger always logs out unless --keep-session is given.  A network
failure is written as an `err` row so the log has no silent gaps.

Examples:
    bench/run.sh ina_log.py --duration 30 --label idle
    bench/run.sh ina_log.py --hz 8 --channel 3 --duration 60
    bench/run.sh ina_log.py --until-marker output/bench/power/stop

Importable API (used by bench/sleep_power.py):

    handle = start_logging(hz=2, channels=(3,), out=".../raw.csv",
                           summary_json=".../summary.json", label="idle")
    handle.set_phase("suspend")
    summary = handle.stop()
"""
import argparse
import csv
import http.client
import http.cookiejar
import json
import os
import statistics
import sys
import threading
import time
import urllib.request
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT_DIR = ROOT / "output" / "bench" / "power"

BRIDGE = os.environ.get("OKO_BRIDGE", "192.168.52.17")
USER = os.environ.get("OKO_BRIDGE_USER", "admin")
PASSWORD = os.environ.get("OKO_BRIDGE_PASSWORD", "admin")

HEADER = ["ts_host_epoch_ms", "ts_bridge_uptime_ms", "phase", "ch",
          "bus_mv", "shunt_uv", "cur_ma", "power_mw", "err"]


def _session_value(jar):
    for cookie in jar:
        if cookie.name == "session" and cookie.value:
            return cookie.name, cookie.value
    return None, None


class Bridge:
    """Minimal keep-alive HTTP client for the bridge REST API."""

    def __init__(self, host=BRIDGE, user=USER, password=PASSWORD,
                 timeout=2.0, cookie_file=None):
        self.host = host
        self.user = user
        self.password = password
        self.timeout = timeout
        self.cookie_file = cookie_file
        self.cookie_name = "session"
        self.cookie = None
        self.conn = None
        self.jar = None
        self._login()

    def _open_jar(self):
        if self.cookie_file:
            jar = http.cookiejar.MozillaCookieJar(self.cookie_file)
            if os.path.exists(self.cookie_file):
                try:
                    jar.load(ignore_discard=True, ignore_expires=True)
                except (OSError, http.cookiejar.LoadError):
                    pass
        else:
            jar = http.cookiejar.CookieJar()
        return jar

    def _login(self, force=False):
        self.jar = self._open_jar()
        self.cookie_name, self.cookie = _session_value(self.jar)
        if self.cookie and not force:
            return
        if force:
            self.jar.clear()
            self.cookie = None
            self.cookie_name = "session"
        opener = urllib.request.build_opener(
            urllib.request.HTTPCookieProcessor(self.jar))
        body = json.dumps({"username": self.user,
                           "password": self.password}).encode()
        req = urllib.request.Request(
            "http://" + self.host + "/auth/login", data=body, method="POST",
            headers={"Content-Type": "application/json"})
        with opener.open(req, timeout=10) as resp:
            answer = json.loads(resp.read().decode() or "{}")
        if not answer.get("authenticated"):
            raise RuntimeError("bridge login failed: %s" % answer)
        self.cookie_name, self.cookie = _session_value(self.jar)
        if not self.cookie:
            raise RuntimeError("bridge login returned no session cookie")
        if self.cookie_file:
            try:
                self.jar.save(ignore_discard=True, ignore_expires=True)
                os.chmod(self.cookie_file, 0o600)
            except OSError as exc:
                print("cookie save failed: %s" % exc, file=sys.stderr)

    def _connect(self):
        self.conn = http.client.HTTPConnection(self.host, 80,
                                               timeout=self.timeout)

    def close(self):
        if self.conn is not None:
            try:
                self.conn.close()
            except OSError:
                pass
            self.conn = None

    def get(self, path):
        headers = {"Cookie": "%s=%s" % (self.cookie_name, self.cookie)}
        for attempt in (1, 2):
            try:
                if self.conn is None:
                    self._connect()
                self.conn.request("GET", path, headers=headers)
                resp = self.conn.getresponse()
                body = resp.read()
                if resp.status == 401 and attempt == 1:
                    self.close()
                    self._login(force=True)
                    headers = {"Cookie": "%s=%s" % (self.cookie_name,
                                                    self.cookie)}
                    continue
                if resp.status != 200:
                    raise RuntimeError("HTTP %d for %s" % (resp.status, path))
                return json.loads(body.decode(errors="replace"))
            except (OSError, http.client.HTTPException):
                self.close()
                if attempt == 2:
                    raise
            except ValueError as exc:
                raise RuntimeError("bad JSON for %s: %s" % (path, exc))
        raise RuntimeError("bridge request failed: %s" % path)

    def sensor(self):
        return self.get("/sensor")

    def logout(self):
        self.close()
        try:
            opener = urllib.request.build_opener()
            req = urllib.request.Request(
                "http://" + self.host + "/auth/logout", data=b"",
                method="POST",
                headers={"Cookie": "%s=%s" % (self.cookie_name, self.cookie),
                         "Content-Type": "application/json"})
            with opener.open(req, timeout=5) as resp:
                resp.read()
        except OSError as exc:
            print("logout failed: %s" % exc, file=sys.stderr)


class Handle:
    """Running logger handle: phase markers, stop and summary."""

    def __init__(self, logger):
        self._logger = logger

    def set_phase(self, name):
        self._logger.set_phase(name)

    def wait(self, timeout=None):
        self._logger.thread.join(timeout)

    @property
    def running(self):
        return self._logger.thread.is_alive()

    def stop(self):
        return self._logger.stop()

    @property
    def path(self):
        return self._logger.path


class _Logger:
    def __init__(self, hz, channels, out, summary_json, label, bridge,
                 duration, until_marker, keep_session):
        self.hz = float(hz)
        self.period = 1.0 / self.hz
        self.channels = list(channels)
        self.path = Path(out)
        self.summary_json = Path(summary_json) if summary_json else None
        self.label = label
        self.bridge = bridge
        self.duration = duration
        self.until_marker = Path(until_marker) if until_marker else None
        self.keep_session = keep_session
        self.stop_event = threading.Event()
        self.phase = label
        self._phase_lock = threading.Lock()
        self._phase_since = time.time()
        self.phase_seconds = {}
        self._summary = None
        self.thread = threading.Thread(target=self._loop, daemon=True)
        self.thread.start()

    def set_phase(self, name):
        with self._phase_lock:
            now = time.time()
            if self.phase:
                self.phase_seconds[self.phase] = \
                    self.phase_seconds.get(self.phase, 0.0) + \
                    (now - self._phase_since)
            self.phase = name
            self._phase_since = now

    def _finish_phase(self):
        with self._phase_lock:
            now = time.time()
            if self.phase:
                self.phase_seconds[self.phase] = \
                    self.phase_seconds.get(self.phase, 0.0) + \
                    (now - self._phase_since)
                self._phase_since = now

    def _loop(self):
        stats = {ch: [] for ch in self.channels}
        rows = samples = errors = missed = 0
        started = time.time()
        started_iso = datetime.fromtimestamp(started).isoformat(timespec="seconds")
        try:
            self.path.parent.mkdir(parents=True, exist_ok=True)
        except OSError as exc:
            print("cannot create %s: %s" % (self.path.parent, exc),
                  file=sys.stderr)
        uptime0 = None
        host0_ms = int(started * 1000)
        try:
            clock = self.bridge.get("/system/time")
            uptime0 = int(clock["uptime_ms"])
            host0_ms = int(clock.get("epoch_ms", host0_ms))
            offset_ms = host0_ms - int(started * 1000)
        except (OSError, RuntimeError, KeyError, ValueError, TypeError):
            offset_ms = None
        next_tick = time.monotonic()
        try:
            with open(self.path, "w", newline="") as stream:
                writer = csv.writer(stream)
                writer.writerow(HEADER)
                while not self.stop_event.is_set():
                    if self.duration is not None and \
                            time.time() - started >= self.duration:
                        break
                    if self.until_marker is not None and \
                            self.until_marker.exists():
                        break
                    now = time.monotonic()
                    if now < next_tick:
                        time.sleep(min(next_tick - now, 0.05))
                        continue
                    if now - next_tick > self.period:
                        missed += int((now - next_tick) / self.period)
                    next_tick = max(now, next_tick + self.period)
                    host_ms = int(time.time() * 1000)
                    if uptime0 is not None:
                        bridge_ms = uptime0 + (host_ms - host0_ms)
                    else:
                        bridge_ms = ""
                    with self._phase_lock:
                        phase = self.phase
                    try:
                        data = self.bridge.sensor()
                        ina = data.get("ina3221") or {}
                        produced = 0
                        for ch in self.channels:
                            item = ina.get("ch%d" % ch)
                            if not isinstance(item, dict):
                                continue
                            cur = item.get("cur_01ma")
                            pwr = item.get("pwr_01mw")
                            cur_ma = round(cur / 100.0, 2) \
                                if cur is not None else ""
                            pwr_mw = round(pwr / 100.0, 2) \
                                if pwr is not None else ""
                            writer.writerow([host_ms, bridge_ms, phase, ch,
                                             item.get("bus_mv"),
                                             item.get("shunt_uv"),
                                             cur_ma, pwr_mw, ""])
                            produced += 1
                            if cur_ma != "" and pwr_mw != "":
                                stats[ch].append((host_ms, cur_ma, pwr_mw))
                        if produced:
                            samples += 1
                            rows += produced
                        else:
                            writer.writerow(
                                [host_ms, bridge_ms, phase, "", "", "", "",
                                 "", "no ina3221 in /sensor"])
                            errors += 1
                    except (OSError, RuntimeError, ValueError, TypeError) as exc:
                        text = str(exc).replace("\n", " ")[:200]
                        writer.writerow([host_ms, bridge_ms, phase, "", "",
                                         "", "", "", text])
                        errors += 1
                    stream.flush()
        except OSError as exc:
            errors += 1
            print("cannot write %s: %s" % (self.path, exc), file=sys.stderr)
        finally:
            self._finish_phase()
        duration_s = round(time.time() - started, 1)
        summary = {
            "started_at": started_iso,
            "started_epoch_ms": int(started * 1000),
            "duration_s": duration_s,
            "hz": self.hz,
            "channels": self.channels,
            "rows": rows,
            "samples": samples,
            "errors": errors,
            "missed_polls": missed,
            "csv": str(self.path),
            "bridge": {"host": self.bridge.host,
                       "clock_offset_ms": offset_ms,
                       "uptime0_ms": uptime0},
            "phases_s": {key: round(value, 1)
                          for key, value in self.phase_seconds.items()},
            "stats": {},
        }
        for ch, points in stats.items():
            if not points:
                continue
            currents = [point[1] for point in points]
            powers = [point[2] for point in points]
            energy_mwh = 0.0
            for previous, current in zip(points, points[1:]):
                dt_h = max(0, current[0] - previous[0]) / 3600000.0
                energy_mwh += (previous[2] + current[2]) / 2.0 * dt_h
            summary["stats"]["ch%d" % ch] = {
                "n": len(points),
                "cur_ma": {"min": min(currents),
                           "mean": round(statistics.mean(currents), 2),
                           "p50": round(statistics.median(currents), 2),
                           "max": max(currents)},
                "power_mw": {"min": min(powers),
                             "mean": round(statistics.mean(powers), 2),
                             "p50": round(statistics.median(powers), 2),
                             "max": max(powers)},
                "energy_mwh": round(energy_mwh, 1),
            }
        self._summary = summary
        if self.summary_json is not None:
            try:
                self.summary_json.parent.mkdir(parents=True, exist_ok=True)
                self.summary_json.write_text(json.dumps(summary, indent=1))
            except OSError as exc:
                print("cannot write %s: %s" % (self.summary_json, exc),
                      file=sys.stderr)

    def stop(self):
        self.stop_event.set()
        self.thread.join(timeout=10)
        if not self.keep_session:
            self.bridge.logout()
        return self._summary or {}


def start_logging(hz=2.0, channels=(1, 2, 3), out=None, summary_json=None,
                  label="", bridge=BRIDGE, user=USER, password=PASSWORD,
                  cookie_file=None, keep_session=False, duration=None,
                  until_marker=None):
    """Start a background logger and return its Handle.

    `out` defaults to output/bench/power/<stamp>-raw.csv.  `channels` is a
    sequence of INA3221 channel numbers present in GET /sensor.  A session is
    always closed by Handle.stop() unless keep_session=True.
    """
    if out is None:
        stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
        out = OUT_DIR / ("%s-raw.csv" % stamp)
    if summary_json is None and out is not None:
        summary_json = Path(out).with_suffix(".summary.json")
    client = Bridge(bridge, user, password, cookie_file=cookie_file)
    logger = _Logger(hz, channels, out, summary_json, label, client, duration,
                     until_marker, keep_session)
    return Handle(logger)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--bridge", default=BRIDGE,
                        help="bridge host (env OKO_BRIDGE)")
    parser.add_argument("--hz", type=float, default=2.0,
                        help="poll rate in Hz (default 2)")
    parser.add_argument("--duration", type=float, default=None,
                        help="stop after S seconds")
    parser.add_argument("--until-marker", default=None,
                        help="stop when this file appears")
    parser.add_argument("--channel", default="1,2,3",
                        help="comma separated INA3221 channels (default 1,2,3)")
    parser.add_argument("--out", default=None,
                        help="CSV path (default output/bench/power/<stamp>-raw.csv)")
    parser.add_argument("--summary-json", default=None,
                        help="summary path (default <out>.summary.json)")
    parser.add_argument("--label", default="", help="initial phase label")
    parser.add_argument("--cookie-file", default=None,
                        help="reuse/save the bridge session cookie here")
    parser.add_argument("--keep-session", action="store_true",
                        help="do not log out at the end (debugging only)")
    args = parser.parse_args()

    channels = tuple(int(value) for value in args.channel.split(",") if value)
    handle = start_logging(hz=args.hz, channels=channels, out=args.out,
                           summary_json=args.summary_json, label=args.label,
                           bridge=args.bridge, cookie_file=args.cookie_file,
                           keep_session=args.keep_session,
                           duration=args.duration,
                           until_marker=args.until_marker)
    print("logging to", handle.path)
    try:
        handle.wait()
    except KeyboardInterrupt:
        pass
    summary = handle.stop()
    print(json.dumps({key: summary.get(key) for key in
                      ("duration_s", "rows", "samples", "errors",
                       "missed_polls", "phases_s")}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, OSError) as exc:
        print("error: %s" % exc, file=sys.stderr)
        sys.exit(1)