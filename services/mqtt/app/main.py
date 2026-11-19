"""OKO MQTT recorder.

Subscribes to the camera topic tree (``oko/#`` by default) and stores every
JSON message under ``$MQTT_REC_DIR``:

    events/<date>.jsonl      one line per event (motion / yolo / clip / heartbeat)
    telemetry/<id>.json      last telemetry sample per device
    status/<id>.json         last retained status (online/LWT)
    messages.log.jsonl       everything, for debugging

The HTTP API on 8092 mirrors the services/events receiver style: a health
probe and small read-only listings for the bench scripts.  Only stdlib plus
FastAPI/uvicorn and paho-mqtt are used.
"""
import json
import os
import threading
import time
from collections import deque
from datetime import datetime, timezone
from pathlib import Path

import paho.mqtt.client as mqtt
from fastapi import FastAPI, HTTPException, Query
from fastapi.responses import HTMLResponse

DATA_DIR = Path(os.environ.get("MQTT_REC_DIR", "/data"))
BROKER = os.environ.get("MQTT_BROKER", "127.0.0.1")
PORT = int(os.environ.get("MQTT_PORT", "1883"))
USER = os.environ.get("MQTT_REC_USER", "oko-rec")
PASSWORD = os.environ.get("MQTT_REC_PASS", "oko-rec")
TOPIC = os.environ.get("MQTT_REC_TOPIC", "oko/#")
MAX_MB = float(os.environ.get("MQTT_REC_MAX_MB", "16"))

EVENTS_DIR = DATA_DIR / "events"
TELEMETRY_DIR = DATA_DIR / "telemetry"
STATUS_DIR = DATA_DIR / "status"
MESSAGES_FILE = DATA_DIR / "messages.log.jsonl"
MEMORY_MESSAGES = 2000

for directory in (DATA_DIR, EVENTS_DIR, TELEMETRY_DIR, STATUS_DIR):
    directory.mkdir(parents=True, exist_ok=True)

app = FastAPI(title="OKO MQTT recorder", docs_url=None, redoc_url=None)

_lock = threading.Lock()
_messages: deque = deque(maxlen=MEMORY_MESSAGES)
_events: deque = deque(maxlen=MEMORY_MESSAGES)
_telemetry: dict = {}
_status: dict = {}
_connected = False
_received = 0


def _now_ms() -> int:
    return int(time.time() * 1000)


def _iso() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def _kind(topic: str) -> str:
    tail = topic.rsplit("/", 1)[-1]
    return tail if tail in ("event", "telemetry", "status") else "other"


def _device(topic: str) -> str:
    parts = topic.split("/")
    if len(parts) >= 2:
        return parts[-2]
    return "unknown"


def _append(path: Path, record: dict) -> None:
    try:
        with path.open("a", encoding="utf-8") as file:
            file.write(json.dumps(record, ensure_ascii=False) + "\n")
    except OSError:
        pass


def _write_json(path: Path, record: dict) -> None:
    try:
        tmp = path.with_suffix(path.suffix + ".part")
        tmp.write_text(json.dumps(record, ensure_ascii=False, indent=1),
                       encoding="utf-8")
        tmp.replace(path)
    except OSError:
        pass


def _trim_jsonl(path: Path, keep_lines: int) -> None:
    """Keeps the newest `keep_lines` records of a JSONL file."""
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
        if len(lines) > keep_lines:
            path.write_text("\n".join(lines[-keep_lines:]) + "\n",
                            encoding="utf-8")
    except OSError:
        pass


def _prune() -> None:
    """Keeps the archive under MQTT_REC_MAX_MB by deleting old event days,
    then trimming the files that remain.  Runs in the paho thread only."""
    try:
        limit = int(MAX_MB * 1024 * 1024)
        files = sorted(EVENTS_DIR.glob("*.jsonl"))
        total = sum(f.stat().st_size for f in files)
        total += MESSAGES_FILE.stat().st_size if MESSAGES_FILE.exists() else 0
        # Never delete the newest day: a single busy day can exceed the cap on
        # its own, and dropping today's events would lose the freshest data.
        while total > limit and len(files) > 1:
            old = files.pop(0)
            total -= old.stat().st_size
            old.unlink(missing_ok=True)
        if total > limit and files:
            _trim_jsonl(files[-1], keep_lines=20000)
        if MESSAGES_FILE.exists() and MESSAGES_FILE.stat().st_size > limit // 4:
            _trim_jsonl(MESSAGES_FILE, keep_lines=MEMORY_MESSAGES)
    except OSError:
        pass


def _on_connect(client, userdata, flags, reason_code, properties) -> None:
    global _connected

    _connected = reason_code == 0
    if _connected:
        client.subscribe(TOPIC, qos=1)


def _on_disconnect(client, userdata, flags, reason_code, properties) -> None:
    global _connected

    _connected = False


def _on_message(client, userdata, message) -> None:
    global _received

    topic = message.topic
    try:
        payload = json.loads(message.payload.decode("utf-8"))
    except (UnicodeDecodeError, ValueError):
        payload = {"raw": message.payload.decode("utf-8", "replace")[:4096]}

    record = {"topic": topic, "received": _iso(), "ts_ms": _now_ms(),
              "payload": payload}
    kind = _kind(topic)
    device = _device(topic)
    date = datetime.now(timezone.utc).strftime("%Y-%m-%d")

    _append(MESSAGES_FILE, record)
    if kind == "event":
        _append(EVENTS_DIR / f"{date}.jsonl", record)
    elif kind == "telemetry":
        _write_json(TELEMETRY_DIR / f"{device}.json", record)
    elif kind == "status":
        _write_json(STATUS_DIR / f"{device}.json", record)

    with _lock:
        _received += 1
        _messages.append(record)
        if kind == "event":
            _events.append(record)
        elif kind == "telemetry":
            _telemetry[device] = record
        elif kind == "status":
            _status[device] = record
        count = _received
    if count % 200 == 0:
        _prune()


def _connect_loop() -> None:
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,
                         client_id="oko-mqtt-recorder")
    client.username_pw_set(USER, PASSWORD)
    client.on_connect = _on_connect
    client.on_disconnect = _on_disconnect
    client.on_message = _on_message
    while True:
        try:
            client.connect(BROKER, PORT, keepalive=30)
            client.loop_forever(retry_first_connection=True)
        except Exception:                       # noqa: BLE001
            time.sleep(5)


threading.Thread(target=_connect_loop, name="mqtt-recorder",
                 daemon=True).start()


def _filter(messages, topic: str | None, limit: int):
    if topic:
        prefix = topic[:-1] if topic.endswith("#") else topic
        messages = [m for m in messages if m["topic"].startswith(prefix)]
    return list(reversed(messages))[:limit]


@app.get("/health")
def health():
    return {"ok": True, "broker": f"{BROKER}:{PORT}", "topic": TOPIC,
            "connected": _connected, "received": _received}


@app.get("/api/messages")
def api_messages(topic: str | None = Query(default=None),
                 limit: int = Query(default=100, ge=1, le=1000)):
    with _lock:
        return {"messages": _filter(list(_messages), topic, limit),
                "count": len(_messages)}


@app.get("/api/events")
def api_events(limit: int = Query(default=50, ge=1, le=1000)):
    with _lock:
        return {"events": _filter(list(_events), None, limit),
                "count": len(_events)}


@app.get("/api/telemetry")
def api_telemetry():
    with _lock:
        devices = {name: record for name, record in _telemetry.items()}
    return {"available": bool(devices), "devices": devices}


@app.get("/api/status")
def api_status():
    with _lock:
        devices = {name: record for name, record in _status.items()}
    return {"devices": devices}


@app.get("/", response_class=HTMLResponse)
def index():
    with _lock:
        messages = len(_messages)
        events = len(_events)
        online = {name: rec["payload"].get("online")
                  for name, rec in _status.items()}
    rows = "".join(
        f"<tr><td>{name}</td><td>{state}</td></tr>"
        for name, state in sorted(online.items()))
    return f"""<!doctype html><html lang="en"><head><meta charset="utf-8">
<title>OKO MQTT recorder</title></head><body>
<h1>OKO MQTT recorder</h1>
<p>broker {BROKER}:{PORT}, topic <code>{TOPIC}</code>, connected:
<b>{_connected}</b>, messages in RAM: {messages}, events: {events}</p>
<table border="1" cellpadding="4"><tr><th>device</th><th>online</th></tr>
{rows}</table>
<ul>
<li><a href="/api/events?limit=20">/api/events</a></li>
<li><a href="/api/messages?limit=20">/api/messages</a></li>
<li><a href="/api/telemetry">/api/telemetry</a></li>
<li><a href="/api/status">/api/status</a></li>
<li><a href="/health">/health</a></li>
</ul></body></html>"""