FROM python:3.8-slim

# Python 3.8 is mandatory: rknn-toolkit2 2.5.0 is published as a cp38-only
# wheel.  The web layer is pinned to the last py3.8-compatible releases in
# conf/requirements-web.txt.
ENV DEBIAN_FRONTEND=noninteractive \
    PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates curl libgomp1 libgl1 libglib2.0-0 \
    && rm -rf /var/lib/apt/lists/*

# Official rknn-toolkit2 2.5.0 wheel (cp38 x86_64).  The sha256 is pinned by
# hand from the vendor's packages/x86_64/sha256sum.txt so a retagged upstream
# file cannot silently change the converter; update it consciously.
ARG RKNN_WHEEL_URL=https://raw.githubusercontent.com/airockchip/rknn-toolkit2/v2.5.0/rknn-toolkit2/packages/x86_64/rknn_toolkit2-2.5.0-cp38-cp38-manylinux_2_17_x86_64.manylinux2014_x86_64.whl
ARG RKNN_WHEEL_SHA256=cf391bd54357f16bfa455a443770128a6e90efce028ba181377c1c2955bdc5c7
RUN curl -fsSL -o /tmp/rknn_toolkit2-2.5.0-cp38-cp38-manylinux_2_17_x86_64.manylinux2014_x86_64.whl "$RKNN_WHEEL_URL" \
    && echo "${RKNN_WHEEL_SHA256}  /tmp/rknn_toolkit2-2.5.0-cp38-cp38-manylinux_2_17_x86_64.manylinux2014_x86_64.whl" | sha256sum -c - \
    && pip install --no-cache-dir --no-deps \
        /tmp/rknn_toolkit2-2.5.0-cp38-cp38-manylinux_2_17_x86_64.manylinux2014_x86_64.whl \
    && rm -f /tmp/rknn_toolkit2-2.5.0-cp38-cp38-manylinux_2_17_x86_64.manylinux2014_x86_64.whl

# toolkit 2.x imports torch unconditionally; the CPU wheel is enough for
# ONNX conversion and is much smaller than the default CUDA build.
RUN pip install --no-cache-dir \
        --extra-index-url https://download.pytorch.org/whl/cpu \
        "torch==1.10.1+cpu"

# Same dependency set as tools/rknn-toolkit2/Dockerfile.toolkit2 so the CLI
# and web paths run byte-identical converter code on identical versions.
RUN pip install --no-cache-dir \
        "numpy==1.24.4" "protobuf==4.25.4" flatbuffers "onnx==1.17.0" \
        onnxoptimizer "onnxruntime==1.17.3" opencv-python-headless \
        ruamel.yaml scipy tqdm psutil h5py fast-histogram \
        humanfriendly coloredlogs

COPY conf/requirements-web.txt /tmp/requirements-web.txt
RUN pip install --no-cache-dir -r /tmp/requirements-web.txt \
    && rm -f /tmp/requirements-web.txt

WORKDIR /app
COPY app /app
# Fixture generator ships inside the image so the host needs no Python deps:
#   docker run --rm -v "$PWD/output/srv/converter:/out" \
#       --entrypoint python oko-converter:latest \
#       /app/tools_fixture/make_fixture.py --out /out/fixtures
COPY tools_fixture /app/tools_fixture

RUN mkdir -p /data/jobs
VOLUME ["/data"]
EXPOSE 8091

HEALTHCHECK --interval=30s --timeout=5s --retries=3 \
    CMD curl -fsS http://127.0.0.1:8091/healthz || exit 1

CMD ["uvicorn", "main:app", "--host", "0.0.0.0", "--port", "8091"]