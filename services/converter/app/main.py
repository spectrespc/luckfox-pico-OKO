"""OKO RKNN converter web service.

Accepts a static 640x384 ONNX model (NCHW or NHWC), optional JPEG calibration
frames, and converts it to a `.rknn` for the rv1106 NPU with rknn-toolkit2
2.5.0.  Conversion runs in a child process managed by worker/queue.py; the web
process only validates uploads and serves state.

State layout inside the container (host: output/srv/converter/):

    /data/jobs/<job_id>/meta.json      job state, timestamps, sizes
    /data/jobs/<job_id>/model.onnx     uploaded model
    /data/jobs/<job_id>/calib/         calibration JPEGs + dataset.txt
    /data/jobs/<job_id>/model.rknn     conversion result
    /data/jobs/<job_id>/log.txt        toolkit output, appended per stage
"""
import os
import re
import shutil
import uuid
from contextlib import asynccontextmanager
from datetime import datetime, timezone
from pathlib import Path
from typing import List

import onnx
from fastapi import (APIRouter, Depends, FastAPI, File, Form, Header,
                     HTTPException, UploadFile)
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles

from worker.queue import JobQueue, now_iso, read_meta, tail, write_meta

try:
    from importlib.metadata import version as pkg_version

    TOOLKIT_VERSION = pkg_version("rknn-toolkit2")
except Exception:  # pragma: no cover - only when the wheel is absent
    TOOLKIT_VERSION = "unknown"

APP_DIR = Path(__file__).resolve().parent
STATIC_DIR = APP_DIR / "static"
DATA_DIR = Path(os.environ.get("CONVERTER_DATA_DIR", "/data"))
JOBS_DIR = DATA_DIR / "jobs"
JOBS_DIR.mkdir(parents=True, exist_ok=True)

TARGET_HEIGHT = 384
TARGET_WIDTH = 640
TARGET_SIZE = "%dx%d" % (TARGET_WIDTH, TARGET_HEIGHT)  # file name (ASCII)
TARGET_LABEL = "%d×%d" % (TARGET_WIDTH, TARGET_HEIGHT)  # human messages
EXPORT_HINT = ("экспортируйте модель со статическим входом %s "
               "(`--img %d %d`, высота первой)"
               % (TARGET_LABEL, TARGET_HEIGHT, TARGET_WIDTH))

MAX_UPLOAD_BYTES = int(float(os.environ.get("CONVERTER_MAX_UPLOAD_MIB", "256"))
                       * 1024 ** 2)
MAX_IMAGE_BYTES = int(float(os.environ.get("CONVERTER_MAX_IMAGE_MIB", "20"))
                      * 1024 ** 2)
MAX_QUEUED = int(os.environ.get("CONVERTER_MAX_QUEUED", "10"))
MIN_CALIB_IMAGES = 20
MAX_CALIB_IMAGES = 200
JOB_TIMEOUT = float(os.environ.get("CONVERTER_JOB_TIMEOUT", "900"))
TOKEN = os.environ.get("OKO_CONVERTER_TOKEN", "").strip()


def _retention_bytes():
    """CONVERTER_MAX_MB wins over CONVERTER_MAX_GB (used by the retention test)."""
    max_mb = os.environ.get("CONVERTER_MAX_MB", "").strip()
    if max_mb:
        return int(float(max_mb) * 1024 ** 2)
    return int(float(os.environ.get("CONVERTER_MAX_GB", "2")) * 1024 ** 3)


def check_token(x_oko_token: str = Header(default="")):
    if TOKEN and x_oko_token != TOKEN:
        raise HTTPException(status_code=401,
                            detail="bad or missing X-OKO-Token header")


queue = JobQueue(JOBS_DIR, APP_DIR / "worker" / "convert.py",
                 timeout_seconds=JOB_TIMEOUT, retention_bytes=_retention_bytes())


@asynccontextmanager
async def lifespan(_app):
    queue.start()
    yield


app = FastAPI(title="OKO RKNN converter", docs_url=None, redoc_url=None,
              lifespan=lifespan)
api = APIRouter(prefix="/api", dependencies=[Depends(check_token)])


def _safe_id(job_id):
    allowed = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._"
    if (not job_id or len(job_id) > 80 or ".." in job_id
            or any(ch not in allowed for ch in job_id)):
        raise HTTPException(status_code=400, detail="bad job id")
    return job_id


def _job_dir(job_id):
    job_dir = JOBS_DIR / _safe_id(job_id)
    if not job_dir.is_dir():
        raise HTTPException(status_code=404, detail="job not found")
    return job_dir


def _validate_onnx(path):
    """Check the single static input is 640x384 NCHW or NHWC.

    Dynamic axes break the RKNN build and yield a confusing toolkit error
    later, so they are rejected here with an actionable message.
    """
    try:
        model = onnx.load(str(path))
    except Exception as exc:
        raise HTTPException(status_code=422,
                            detail="ONNX не читается: %s" % exc)
    inputs = list(model.graph.input)
    if len(inputs) != 1:
        raise HTTPException(
            status_code=422,
            detail="ожидается ровно один вход ONNX, найдено %d; %s"
                   % (len(inputs), EXPORT_HINT))
    tensor = inputs[0]
    dims = []
    for dim in tensor.type.tensor_type.shape.dim:
        if not dim.HasField("dim_value") or dim.dim_value <= 0:
            raise HTTPException(
                status_code=422,
                detail="вход динамический (символьные оси); %s" % EXPORT_HINT)
        dims.append(int(dim.dim_value))
    if len(dims) != 4:
        raise HTTPException(
            status_code=422,
            detail="вход %s не 4D; нужен NCHW [1,3,%d,%d] или NHWC "
                   "[1,%d,%d,3]" % (dims, TARGET_HEIGHT, TARGET_WIDTH,
                                    TARGET_HEIGHT, TARGET_WIDTH))
    batch, c1, c2, c3 = dims
    if batch != 1:
        raise HTTPException(
            status_code=422,
            detail="batch=%d; supported batch size is 1; %s"
                   % (batch, EXPORT_HINT))
    if (c1, c2, c3) == (3, TARGET_HEIGHT, TARGET_WIDTH):
        layout = "nchw"
    elif (c1, c2, c3) == (TARGET_HEIGHT, TARGET_WIDTH, 3):
        layout = "nhwc"
    else:
        raise HTTPException(
            status_code=422,
            detail="вход %s не %s; нужен NCHW [1,3,%d,%d] или NHWC "
                   "[1,%d,%d,3]; %s"
                   % (dims, TARGET_LABEL, TARGET_HEIGHT, TARGET_WIDTH,
                      TARGET_HEIGHT, TARGET_WIDTH, EXPORT_HINT))
    return {"name": tensor.name, "shape": dims, "layout": layout}


def _output_name(meta):
    stem = Path(meta.get("name") or "model.onnx").stem
    safe = re.sub(r"[^A-Za-z0-9._-]+", "-", stem).strip("-._") or "model"
    return "%s-%s-rknn2.5.0.rknn" % (safe, TARGET_SIZE)


def _new_job_id():
    stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
    return "%s-%s" % (stamp, uuid.uuid4().hex[:6])


def _job_summary(job_dir):
    meta = read_meta(job_dir)
    if not meta:
        return None
    meta["id"] = meta.get("id") or job_dir.name
    meta["has_model"] = (job_dir / "model.rknn").is_file()
    meta["output_name"] = _output_name(meta)
    return meta


def _list_jobs():
    jobs = []
    for job_dir in JOBS_DIR.iterdir():
        if not job_dir.is_dir():
            continue
        summary = _job_summary(job_dir)
        if summary:
            jobs.append(summary)
    jobs.sort(key=lambda item: (item.get("created_at", ""), item.get("id", "")),
              reverse=True)
    return jobs


@api.post("/jobs", status_code=201)
async def create_job(model: UploadFile = File(...),
                     images: List[UploadFile] = File(default=[]),
                     dtype: str = Form("int8")):
    if dtype not in ("int8", "fp16"):
        raise HTTPException(status_code=422,
                            detail="dtype must be int8 or fp16")
    if dtype == "fp16":
        # rknn-toolkit2 2.5.0 refuses do_quantization=False for rv1106
        # ("Current target_platform(rv1106) not support do_quantization =
        # False") and also refuses do_quantization=True without a dataset.
        # INT8 with 20-200 JPEG is therefore the only buildable mode.
        raise HTTPException(
            status_code=422,
            detail="rv1106 поддерживает только INT8 с калибровкой: "
                   "rknn-toolkit2 2.5.0 отклоняет do_quantization=False для "
                   "target_platform='rv1106', а квантование без dataset "
                   "запрещено. Загрузите 20–200 JPEG и выберите int8.")
    if len(images) > MAX_CALIB_IMAGES:
        raise HTTPException(status_code=422,
                            detail="не больше %d калибровочных JPEG, получено %d"
                                   % (MAX_CALIB_IMAGES, len(images)))
    counts = queue.counts()
    if counts.get("queued", 0) >= MAX_QUEUED:
        raise HTTPException(status_code=409,
                            detail="очередь заполнена (%d задач); дождитесь "
                                   "завершения" % MAX_QUEUED)

    job_id = _new_job_id()
    job_dir = JOBS_DIR / job_id
    job_dir.mkdir(parents=True)
    try:
        incoming = job_dir / "model.onnx.incoming"
        onnx_bytes = 0
        with open(incoming, "wb") as out:
            while True:
                chunk = await model.read(1024 * 1024)
                if not chunk:
                    break
                onnx_bytes += len(chunk)
                if onnx_bytes > MAX_UPLOAD_BYTES:
                    raise HTTPException(
                        status_code=413,
                        detail="модель больше лимита %d МиБ"
                               % (MAX_UPLOAD_BYTES // 1024 ** 2))
                out.write(chunk)
        if onnx_bytes == 0:
            raise HTTPException(status_code=422, detail="пустой ONNX")
        input_info = _validate_onnx(incoming)
        # Validate the model before the frame count so a wrong input size
        # gets the 384x640 hint instead of a misleading "not enough JPEG".
        if len(images) < MIN_CALIB_IMAGES:
            raise HTTPException(
                status_code=422,
                detail="для INT8 нужно %d–%d JPEG с рабочей сцены, получено %d"
                       % (MIN_CALIB_IMAGES, MAX_CALIB_IMAGES, len(images)))

        calib_count = 0
        if dtype == "int8":
            calib_dir = job_dir / "calib"
            calib_dir.mkdir()
            dataset_lines = []
            for index, upload in enumerate(images):
                frame = await upload.read()
                if not frame.startswith(b"\xff\xd8"):
                    raise HTTPException(
                        status_code=422,
                        detail="файл %s не JPEG (нет маркера FFD8)"
                               % (upload.filename or index))
                if len(frame) > MAX_IMAGE_BYTES:
                    raise HTTPException(
                        status_code=413,
                        detail="кадр %s больше %d МиБ"
                               % (upload.filename or index,
                                  MAX_IMAGE_BYTES // 1024 ** 2))
                path = calib_dir / ("frame-%03d.jpg" % index)
                path.write_bytes(frame)
                dataset_lines.append(str(path))
                calib_count += 1
            (calib_dir / "dataset.txt").write_text("\n".join(dataset_lines)
                                                   + "\n")

        os.replace(incoming, job_dir / "model.onnx")
        meta = {
            "id": job_id,
            "name": model.filename or "model.onnx",
            "dtype": dtype,
            "status": "queued",
            "created_at": now_iso(),
            "finished_at": "",
            "input": input_info,
            "onnx_bytes": onnx_bytes,
            "rknn_bytes": 0,
            "images": calib_count,
            "error": "",
            "toolkit": TOOLKIT_VERSION,
        }
        write_meta(job_dir, meta)
        queue.submit(job_id)
        return _job_summary(job_dir)
    except HTTPException:
        shutil.rmtree(job_dir, ignore_errors=True)
        raise
    except Exception as exc:
        shutil.rmtree(job_dir, ignore_errors=True)
        raise HTTPException(status_code=500,
                            detail="не удалось создать задачу: %s" % exc)


@api.get("/jobs")
def list_jobs():
    return {"jobs": _list_jobs(), "counts": queue.counts()}


@api.get("/jobs/{job_id}")
def get_job(job_id: str):
    job_dir = _job_dir(job_id)
    meta = _job_summary(job_dir)
    meta["log"] = tail(job_dir / "log.txt")
    meta["files"] = sorted(item.name for item in job_dir.iterdir()
                           if item.is_file())
    return meta


@api.post("/jobs/{job_id}/cancel")
def cancel_job(job_id: str):
    job_dir = _job_dir(job_id)
    status = read_meta(job_dir).get("status")
    if status not in ("queued", "running"):
        raise HTTPException(status_code=409,
                            detail="задача уже %s" % (status or "неизвестна"))
    result = queue.cancel(job_id)
    if result is None:
        raise HTTPException(status_code=409, detail="задача уже не активна")
    return {"id": job_id, "status": result}


@api.get("/jobs/{job_id}/download")
def download_job(job_id: str):
    job_dir = _job_dir(job_id)
    meta = read_meta(job_dir)
    rknn_file = job_dir / "model.rknn"
    if meta.get("status") != "done" or not rknn_file.is_file():
        raise HTTPException(status_code=404,
                            detail="model.rknn ещё не готов (статус: %s)"
                                   % (meta.get("status") or "нет задачи"))
    return FileResponse(str(rknn_file), media_type="application/octet-stream",
                        filename=_output_name(meta))


@api.delete("/jobs/{job_id}", status_code=204)
def delete_job(job_id: str):
    job_dir = _job_dir(job_id)
    status = read_meta(job_dir).get("status")
    if status in ("queued", "running") or queue.is_active(job_id):
        raise HTTPException(status_code=409,
                            detail="нельзя удалить активную задачу; сначала "
                                   "отмените её")
    shutil.rmtree(job_dir, ignore_errors=True)
    return None


@app.get("/healthz", dependencies=[Depends(check_token)])
def healthz():
    counts = queue.counts()
    return {"ok": True, "toolkit": TOOLKIT_VERSION,
            "queued": counts.get("queued", 0),
            "running": counts.get("running", 0),
            "current": queue.current_job(),
            "max_upload_mib": MAX_UPLOAD_BYTES // 1024 ** 2,
            "token_required": bool(TOKEN)}


@app.get("/")
def index():
    return FileResponse(str(STATIC_DIR / "index.html"))


app.include_router(api)
app.mount("/static", StaticFiles(directory=str(STATIC_DIR)), name="static")