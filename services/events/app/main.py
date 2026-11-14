"""OKO event receiver.

The camera PUTs JPEG frames (`/ingest/frame`) and one fragmented-MP4 clip
(`/ingest/clip`) under a shared X-Event-Id.  The service stores them under
`$EVENTS_DIR/<date>/<event_id>/`, transcodes the H.265 clip to an H.264 copy
for the browser, and serves a minimal event list.

Only stdlib + FastAPI/uvicorn are used; ffmpeg handles the video work.
"""
import json
import os
import queue
import shutil
import subprocess
import threading
import time
from datetime import datetime, timezone
from pathlib import Path

from fastapi import FastAPI, HTTPException, Request, Response
from fastapi.responses import FileResponse, HTMLResponse

EVENTS_DIR = Path(os.environ.get("EVENTS_DIR", "/data"))
MAX_GB = float(os.environ.get("EVENTS_MAX_GB", "2"))
TOKEN = os.environ.get("OKO_EVENTS_TOKEN", "").strip()
CLIP_MAX_BYTES = int(os.environ.get("EVENTS_CLIP_MAX_BYTES", str(64 * 1024 * 1024)))
FRAME_MAX_BYTES = int(os.environ.get("EVENTS_FRAME_MAX_BYTES", str(8 * 1024 * 1024)))

EVENTS_DIR.mkdir(parents=True, exist_ok=True)

app = FastAPI(title="OKO events", docs_url=None, redoc_url=None)

_meta_lock = threading.Lock()
_transcodes: "queue.Queue[Path]" = queue.Queue()


def _safe_id(value: str) -> str:
    if not value or len(value) > 64:
        raise HTTPException(status_code=400, detail="bad event id")
    allowed = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-"
    if any(ch not in allowed for ch in value) or ".." in value:
        raise HTTPException(status_code=400, detail="bad event id")
    return value


def _check_token(request: Request) -> None:
    if TOKEN and request.headers.get("x-oko-token", "") != TOKEN:
        raise HTTPException(status_code=401, detail="bad token")


def _event_dir(event_id: str, create: bool = True) -> Path:
    """Returns the directory of an event, reusing one created before midnight.

    The date comes from the first request of the event, not from the current
    wall clock, so frames and the clip of an event that crosses 00:00 UTC stay
    in the same directory.
    """
    event_id = _safe_id(event_id)
    with _meta_lock:
        for day in sorted(EVENTS_DIR.iterdir(), reverse=True):
            candidate = day / event_id
            if day.is_dir() and candidate.is_dir():
                return candidate
        day = datetime.now(timezone.utc).strftime("%Y-%m-%d")
        path = EVENTS_DIR / day / event_id
        if create:
            path.mkdir(parents=True, exist_ok=True)
        return path


def _find_event(event_id: str) -> Path:
    event_id = _safe_id(event_id)
    for day in sorted(EVENTS_DIR.iterdir(), reverse=True):
        candidate = day / event_id
        if candidate.is_dir():
            return candidate
    raise HTTPException(status_code=404, detail="event not found")


def _read_meta(path: Path) -> dict:
    meta_file = path / "meta.json"
    try:
        return json.loads(meta_file.read_text())
    except (OSError, ValueError):
        return {}


def _update_meta(path: Path, **fields) -> dict:
    with _meta_lock:
        # Retention can remove a date directory while a transcode for it is
        # still queued; that is not an error, the event is simply gone.
        try:
            meta = _read_meta(path)
            meta.update(fields)
            tmp = path / "meta.json.part"
            tmp.write_text(json.dumps(meta, ensure_ascii=False, indent=1))
            tmp.replace(path / "meta.json")
            return meta
        except OSError:
            return {}


def _parse_ts(value: str | None) -> int:
    try:
        return int(value or "0")
    except ValueError:
        return 0


def _transcode_worker() -> None:
    while True:
        event_dir = _transcodes.get()
        if event_dir is None:
            return
        try:
            clip = event_dir / "clip.mp4"
            h264 = event_dir / "clip-h264.mp4"
            preview = event_dir / "clip.jpg"
            status = "done"
            error = ""
            if not clip.exists():
                continue
            try:
                subprocess.run(
                    ["ffmpeg", "-y", "-v", "error", "-i", str(clip),
                     "-c:v", "libx264", "-preset", "veryfast",
                     "-pix_fmt", "yuv420p", "-movflags", "+faststart", "-an",
                     str(h264)],
                    check=True, timeout=600)
                subprocess.run(
                    ["ffmpeg", "-y", "-v", "error", "-ss", "0.3", "-i",
                     str(h264), "-frames:v", "1", "-q:v", "3", str(preview)],
                    check=True, timeout=120)
            except (subprocess.CalledProcessError,
                    subprocess.TimeoutExpired) as exc:
                status = "failed"
                error = str(exc)[:300]
            _update_meta(event_dir, transcode=status, transcode_error=error)
            print(f"transcode {event_dir.name}: {status} {error}", flush=True)
        except Exception as exc:                       # noqa: BLE001
            # One bad event (for example, removed by retention) must not kill
            # the worker and stall every later clip.
            print(f"transcode {event_dir.name}: skipped {exc}", flush=True)


def _retention() -> None:
    """Evicts the oldest *events*, not whole dates.

    A single active date used to be removed as a unit when the archive hit the
    quota, which also deleted the event that had just arrived.  Events younger
    than PROTECT_SECONDS are never touched, so retention cannot pull the input
    from under an upload or an ffmpeg worker.
    """
    PROTECT_SECONDS = 600
    now = time.time()
    events = []
    total = 0
    for day in EVENTS_DIR.iterdir():
        if not day.is_dir():
            continue
        for event_dir in day.iterdir():
            if not event_dir.is_dir():
                continue
            try:
                size = sum(f.stat().st_size for f in event_dir.rglob("*")
                           if f.is_file())
                mtime = event_dir.stat().st_mtime
            except OSError:
                continue
            events.append((mtime, event_dir, size))
            total += size
    limit = int(MAX_GB * 1024 ** 3)
    for mtime, event_dir, size in sorted(events):
        if total <= limit:
            break
        if now - mtime < PROTECT_SECONDS:
            continue
        shutil.rmtree(event_dir, ignore_errors=True)
        total -= size
        print(f"retention: removed {event_dir.name} ({size} bytes)",
              flush=True)
    for day in EVENTS_DIR.iterdir():
        if day.is_dir():
            try:
                day.rmdir()          # only empty directories disappear
            except OSError:
                pass


def _requeue_pending() -> None:
    """Recovers clips left pending by a restart, without reprocessing history.

    Only recent events explicitly marked pending are queued; finished and
    permanently failed transcodes are not retried on every start.
    """
    cutoff = time.time() - 2 * 86400
    queued = 0
    for day in EVENTS_DIR.iterdir():
        if not day.is_dir():
            continue
        for event_dir in day.iterdir():
            if not event_dir.is_dir():
                continue
            if not (event_dir / "clip.mp4").exists():
                continue
            if (event_dir / "clip-h264.mp4").exists():
                continue
            try:
                if event_dir.stat().st_mtime < cutoff:
                    continue
            except OSError:
                continue
            meta = _read_meta(event_dir)
            if meta.get("transcode") not in (None, "pending"):
                continue
            _transcodes.put(event_dir)
            queued += 1
    if queued:
        print(f"requeued {queued} pending transcodes", flush=True)


@app.on_event("startup")
def _startup() -> None:
    for _ in range(4):
        threading.Thread(target=_transcode_worker, daemon=True).start()
    threading.Thread(target=_file_retention_loop, daemon=True).start()
    threading.Thread(target=_requeue_pending, daemon=True).start()


def _file_retention_loop() -> None:
    while True:
        time.sleep(300)
        try:
            _retention()
        except OSError:
            pass


@app.get("/health")
def health() -> dict:
    return {"ok": True, "events_dir": str(EVENTS_DIR)}


@app.put("/ingest/frame")
async def ingest_frame(request: Request) -> dict:
    _check_token(request)
    raw_id = request.headers.get("x-event-id", "")
    if not raw_id:
        # The streamer also has a periodic single-image poster; it carries no
        # event headers and is not part of the event archive.
        await request.body()
        return {"ignored": True, "reason": "no X-Event-Id"}
    event_id = _safe_id(raw_id)
    length = request.headers.get("content-length")
    try:
        if length and int(length) > FRAME_MAX_BYTES:
            raise HTTPException(status_code=413, detail="frame too large")
    except ValueError:
        raise HTTPException(status_code=400, detail="bad content-length")
    body = await request.body()
    if len(body) > FRAME_MAX_BYTES:
        raise HTTPException(status_code=413, detail="frame too large")
    if not body or not body.startswith(b"\xff\xd8"):
        raise HTTPException(status_code=400, detail="body is not a JPEG")
    if request.headers.get("x-frame-index", "0") == "0":
        print(f"{time.time():.3f} frame {event_id} first bytes={len(body)}",
              flush=True)

    event_dir = _event_dir(event_id)
    try:
        index = int(request.headers.get("x-frame-index", "0"))
    except ValueError:
        index = 0
    name = f"frame-{index:03d}.jpg"
    (event_dir / name).write_bytes(body)

    frames = len(list(event_dir.glob("frame-*.jpg")))
    camera_ts = _parse_ts(request.headers.get("x-frame-ts"))
    meta = _read_meta(event_dir) or {
        "event_id": event_id,
        "received_at": datetime.now(timezone.utc).isoformat(),
    }
    meta.update({
        "cause": request.headers.get("x-event-cause", meta.get("cause", "")),
        "model": request.headers.get("x-model", meta.get("model", "")),
        "camera_ts": camera_ts or meta.get("camera_ts", 0),
        "frame_total": int(request.headers.get("x-frame-total", frames) or frames),
        "frames": frames,
    })
    _update_meta(event_dir, **meta)
    return {"stored": name, "event_id": event_id, "frames": frames}


@app.put("/ingest/clip")
async def ingest_clip(request: Request) -> dict:
    _check_token(request)
    event_id = _safe_id(request.headers.get("x-event-id", ""))
    length = request.headers.get("content-length")
    if length and int(length) > CLIP_MAX_BYTES:
        raise HTTPException(status_code=413, detail="clip too large")
    body = await request.body()
    if len(body) < 16:
        raise HTTPException(status_code=400, detail="empty clip")

    print(f"{time.time():.3f} clip {event_id} bytes={len(body)}", flush=True)
    event_dir = _event_dir(event_id)
    (event_dir / "clip.mp4").write_bytes(body)
    meta = _read_meta(event_dir) or {
        "event_id": event_id,
        "received_at": datetime.now(timezone.utc).isoformat(),
    }
    meta.update({
        "cause": request.headers.get("x-cause", meta.get("cause", "")),
        "model": request.headers.get("x-model", meta.get("model", "")),
        "duration_ms": _parse_ts(request.headers.get("x-duration-ms")),
        "camera_ts": _parse_ts(request.headers.get("x-ts")) or meta.get("camera_ts", 0),
        "clip_bytes": len(body),
        "transcode": "pending",
    })
    _update_meta(event_dir, **meta)
    _transcodes.put(event_dir)
    _retention()
    return {"stored": "clip.mp4", "event_id": event_id, "bytes": len(body)}


@app.get("/api/events")
def list_events(limit: int = 50) -> dict:
    limit = max(1, min(limit, 500))
    events = []
    for day in sorted(EVENTS_DIR.iterdir(), reverse=True):
        if not day.is_dir():
            continue
        for event_dir in sorted(day.iterdir(), reverse=True):
            if not event_dir.is_dir():
                continue
            meta = _read_meta(event_dir)
            if not meta:
                continue
            meta.setdefault("event_id", event_dir.name)
            meta["date"] = day.name
            meta["has_clip"] = (event_dir / "clip.mp4").exists()
            meta["has_h264"] = (event_dir / "clip-h264.mp4").exists()
            meta["has_preview"] = (event_dir / "clip.jpg").exists()
            events.append(meta)
            if len(events) >= limit:
                return {"events": events}
    return {"events": events}


@app.get("/api/events/{event_id}")
def get_event(event_id: str) -> dict:
    path = _find_event(event_id)
    meta = _read_meta(path)
    meta["files"] = sorted(f.name for f in path.iterdir() if f.is_file())
    return meta


@app.get("/media/{date}/{event_id}/{name}")
def media(date: str, event_id: str, name: str) -> FileResponse:
    if ".." in date + event_id + name or "/" in name:
        raise HTTPException(status_code=400, detail="bad path")
    path = EVENTS_DIR / _safe_id(date) / _safe_id(event_id) / name
    if not path.is_file():
        raise HTTPException(status_code=404, detail="not found")
    return FileResponse(path)


@app.get("/", response_class=HTMLResponse)
def index() -> str:
    rows = []
    for meta in list_events(limit=100)["events"]:
        event_id = meta.get("event_id", "")
        date = meta.get("date", "")
        base = f"/media/{date}/{event_id}"
        video = ""
        if meta.get("has_h264"):
            video = (f'<video controls preload="metadata" width="360" '
                     f'src="{base}/clip-h264.mp4"></video>')
        elif meta.get("has_preview"):
            video = f'<img width="360" src="{base}/clip.jpg">'
        clip_link = (f'<a href="{base}/clip.mp4">original H.265</a>'
                     if meta.get("has_clip") else "no clip")
        rows.append(
            f"<tr><td>{meta.get('received_at','')}</td>"
            f"<td>{meta.get('cause','')}</td><td>{meta.get('model','')}</td>"
            f"<td>{meta.get('frames',0)}</td>"
            f"<td>{meta.get('duration_ms',0)} ms</td>"
            f"<td>{meta.get('transcode','')}</td>"
            f"<td>{video}</td><td>{clip_link}</td></tr>")
    return (
        "<!doctype html><meta charset='utf-8'><title>OKO events</title>"
        "<style>body{font:14px system-ui;margin:20px}"
        "td{padding:6px;border-bottom:1px solid #ddd;vertical-align:top}</style>"
        "<h1>OKO events</h1><table><tr><th>received</th><th>cause</th>"
        "<th>model</th><th>frames</th><th>duration</th><th>transcode</th>"
        "<th>video</th><th>original</th></tr>" + "".join(rows) +
        "</table>")


@app.get("/api/stats")
def stats() -> dict:
    total = 0
    count = 0
    for day in EVENTS_DIR.iterdir():
        if not day.is_dir():
            continue
        for event_dir in day.iterdir():
            if not event_dir.is_dir():
                continue
            count += 1
            total += sum(f.stat().st_size for f in event_dir.iterdir()
                         if f.is_file())
    return {"events": count, "bytes": total, "max_bytes": int(MAX_GB * 1024 ** 3)}


if __name__ == "__main__":
    import uvicorn

    uvicorn.run(app, host="0.0.0.0", port=int(os.environ.get("EVENTS_PORT", "8090")))
