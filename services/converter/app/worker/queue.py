"""Single-slot conversion queue with on-disk state.

Every job is a directory under ``$CONVERTER_DATA_DIR/jobs`` holding
``meta.json``, ``model.onnx``, optional ``calib/`` images, ``model.rknn`` and
``log.txt``.  Only one conversion child process may exist at a time: the
toolkit peaks at 2-3 GB and two concurrent quantizations would swap the host.

The queue is intentionally in-process (no broker, no docker socket) and is
rebuilt from ``meta.json`` files on container start; jobs that were queued or
running during a restart become ``interrupted`` with their files preserved.
"""
import json
import os
import shutil
import signal
import subprocess
import sys
import threading
import time
from datetime import datetime, timezone
from pathlib import Path

ACTIVE_STATUSES = ("queued", "running")
FINISHED_STATUSES = ("done", "failed", "canceled", "interrupted")
POLL_SECONDS = 0.2
LOG_TAIL_LINES = 200


def now_iso():
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def read_meta(job_dir):
    """Return the job meta or an empty dict when it is missing/corrupt."""
    try:
        return json.loads((Path(job_dir) / "meta.json").read_text())
    except (OSError, ValueError):
        return {}


def write_meta(job_dir, meta):
    """Atomically replace meta.json so readers never see a partial file."""
    job_dir = Path(job_dir)
    tmp = job_dir / "meta.json.part"
    tmp.write_text(json.dumps(meta, ensure_ascii=False, indent=1))
    tmp.replace(job_dir / "meta.json")


def update_meta(job_dir, **fields):
    meta = read_meta(job_dir)
    meta.update(fields)
    write_meta(job_dir, meta)
    return meta


def dir_size(path):
    total = 0
    for root, _dirs, files in os.walk(path):
        for name in files:
            try:
                total += os.path.getsize(os.path.join(root, name))
            except OSError:
                pass
    return total


def tail(path, lines=LOG_TAIL_LINES):
    """Last ``lines`` lines of a text log; binary-safe enough for toolkit logs."""
    try:
        with open(path, "rb") as handle:
            data = handle.read()
    except OSError:
        return ""
    text = data.decode("utf-8", "replace")
    return "\n".join(text.splitlines()[-lines:])


def _kill(proc):
    """SIGKILL the child process group, then reap it."""
    try:
        os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
    except (ProcessLookupError, PermissionError, OSError):
        try:
            proc.kill()
        except OSError:
            pass
    try:
        proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        pass


class JobQueue:
    def __init__(self, jobs_dir, worker_script, timeout_seconds=900,
                 retention_bytes=2 * 1024 ** 3, log=print):
        self.jobs_dir = Path(jobs_dir)
        self.worker_script = Path(worker_script)
        self.timeout_seconds = float(timeout_seconds)
        self.retention_bytes = int(retention_bytes)
        self.log = log
        self._lock = threading.Lock()
        self._queue = []
        self._current = None
        self._cancel = set()
        self._thread = None

    # -- lifecycle -----------------------------------------------------------

    def start(self):
        """Recover stale state, trim retention and start the worker thread."""
        self.jobs_dir.mkdir(parents=True, exist_ok=True)
        self._recover()
        self.enforce_retention()
        self._thread = threading.Thread(target=self._run, name="converter-worker",
                                        daemon=True)
        self._thread.start()
        self.log("queue: worker started (timeout %ss, retention %s bytes)"
                 % (int(self.timeout_seconds), self.retention_bytes))

    def _recover(self):
        for job_dir in sorted(self.jobs_dir.iterdir()):
            if not job_dir.is_dir():
                continue
            meta = read_meta(job_dir)
            status = meta.get("status")
            if status in ACTIVE_STATUSES:
                update_meta(
                    job_dir, status="interrupted",
                    error="container restarted while the job was %s" % status,
                    finished_at=now_iso())
                self.log("queue: %s %s -> interrupted" % (job_dir.name, status))
            # A killed child can leave these behind on the first failed run.
            for stale in ("model.rknn.part", "model.onnx.incoming"):
                try:
                    (job_dir / stale).unlink()
                except OSError:
                    pass

    # -- public operations ---------------------------------------------------

    def submit(self, job_id):
        with self._lock:
            self._queue.append(job_id)
        self.log("queue: %s queued (depth %d)" % (job_id, len(self._queue)))

    def cancel(self, job_id):
        """Cancel a queued/running job.  Returns the resulting status or None."""
        with self._lock:
            if job_id in self._queue:
                self._queue.remove(job_id)
                self._cancel.add(job_id)
                queued = True
            else:
                queued = False
            current = self._current
            running = bool(current and current["id"] == job_id)
            if running:
                current["cancel"] = True
        job_dir = self.jobs_dir / job_id
        if queued:
            update_meta(job_dir, status="canceled", finished_at=now_iso(),
                        error="canceled while queued")
            return "canceled"
        if running:
            # The worker observes the flag, kills the process group and
            # records the final status; report the intent now.
            return "canceling"
        return None

    def is_active(self, job_id):
        with self._lock:
            if job_id in self._queue:
                return True
            return bool(self._current and self._current["id"] == job_id)

    def counts(self):
        counts = {status: 0 for status in ACTIVE_STATUSES + FINISHED_STATUSES}
        for job_dir in self.jobs_dir.iterdir():
            if job_dir.is_dir():
                status = read_meta(job_dir).get("status")
                if status in counts:
                    counts[status] += 1
        return counts

    def current_job(self):
        with self._lock:
            return self._current["id"] if self._current else None

    # -- retention -----------------------------------------------------------

    def enforce_retention(self):
        """Evict the oldest finished jobs until the directory fits the limit."""
        entries = []
        total = 0
        for job_dir in self.jobs_dir.iterdir():
            if not job_dir.is_dir():
                continue
            meta = read_meta(job_dir)
            size = dir_size(job_dir)
            total += size
            status = meta.get("status")
            if status in ACTIVE_STATUSES:
                continue
            finished = meta.get("finished_at") or ""
            entries.append((finished, job_dir.stat().st_mtime, job_dir, size))
        entries.sort(key=lambda item: (item[0], item[1]))
        for finished, _mtime, job_dir, size in entries:
            if total <= self.retention_bytes:
                break
            shutil.rmtree(job_dir, ignore_errors=True)
            total -= size
            self.log("retention: removed %s (%d bytes)" % (job_dir.name, size))

    # -- worker --------------------------------------------------------------

    def _run(self):
        while True:
            job_id = self._take_next()
            if job_id is None:
                time.sleep(POLL_SECONDS)
                continue
            try:
                self._execute(job_id)
            except Exception as exc:  # never let one job kill the worker
                self.log("queue: %s worker error: %s" % (job_id, exc))
                update_meta(self.jobs_dir / job_id, status="failed",
                            error="internal worker error: %s" % exc,
                            finished_at=now_iso())

    def _take_next(self):
        with self._lock:
            while self._queue:
                job_id = self._queue.pop(0)
                if job_id in self._cancel:
                    self._cancel.discard(job_id)
                    continue
                return job_id
        return None

    def _execute(self, job_id):
        job_dir = self.jobs_dir / job_id
        meta = read_meta(job_dir)
        if meta.get("status") != "queued":
            return
        dtype = meta.get("dtype", "int8")
        command = [sys.executable, str(self.worker_script),
                   str(job_dir / "model.onnx"), str(job_dir / "model.rknn"),
                   "--dtype", dtype]
        dataset = job_dir / "calib" / "dataset.txt"
        if dtype == "int8":
            command += ["--dataset", str(dataset)]

        update_meta(job_dir, status="running", started_at=now_iso(),
                    error="")
        self.log("queue: %s running (%s)" % (job_id, dtype))

        canceled = False
        timed_out = False
        code = None
        with open(job_dir / "log.txt", "ab") as log_file:
            log_file.write(("\n=== %s %s ===\n" % (now_iso(), " ".join(command)))
                           .encode())
            log_file.flush()
            proc = subprocess.Popen(
                command, cwd=str(job_dir), stdout=log_file,
                stderr=subprocess.STDOUT, start_new_session=True)
            with self._lock:
                self._current = {"id": job_id, "proc": proc, "cancel": False}
            deadline = time.monotonic() + self.timeout_seconds
            try:
                while True:
                    try:
                        code = proc.wait(timeout=POLL_SECONDS)
                        break
                    except subprocess.TimeoutExpired:
                        pass
                    with self._lock:
                        current = self._current
                        canceled = bool(current and current["id"] == job_id
                                        and current["cancel"])
                    if canceled:
                        _kill(proc)
                        break
                    if time.monotonic() > deadline:
                        timed_out = True
                        _kill(proc)
                        break
            finally:
                with self._lock:
                    if self._current and self._current["id"] == job_id:
                        self._current = None

        # An output written by an interrupted run must not be offered: drop
        # the partial file, and on cancel also a model that finished exporting
        # microseconds before the kill.
        stale = ["model.rknn.part"]
        if canceled:
            stale.append("model.rknn")
        for name in stale:
            try:
                (job_dir / name).unlink()
            except OSError:
                pass

        rknn_file = job_dir / "model.rknn"
        fields = {"finished_at": now_iso(), "exit_code": code}
        if canceled:
            fields.update(status="canceled", error="canceled by operator")
        elif timed_out:
            fields.update(status="failed",
                          error="conversion exceeded %d s"
                                % int(self.timeout_seconds))
        elif code != 0:
            fields.update(status="failed",
                          error="conversion exited with code %s" % code)
        elif not rknn_file.is_file() or rknn_file.stat().st_size == 0:
            fields.update(status="failed",
                          error="conversion finished but model.rknn is missing")
        else:
            fields.update(status="done", rknn_bytes=rknn_file.stat().st_size)
        update_meta(job_dir, **fields)
        self.log("queue: %s %s (%s)" % (job_id, fields["status"],
                                        fields.get("error", "")
                                        or "%s bytes" % fields.get("rknn_bytes")))
        self.enforce_retention()