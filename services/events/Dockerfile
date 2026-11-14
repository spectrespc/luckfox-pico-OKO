FROM python:3.12-slim

ENV DEBIAN_FRONTEND=noninteractive \
    PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1

# ffmpeg does the H.265 -> H.264 browser copy and the preview frame.
RUN apt-get update && apt-get install -y --no-install-recommends \
        ffmpeg curl \
    && rm -rf /var/lib/apt/lists/*

# Pinned: the receiver is part of the documented bench chain, so a moving
# "latest" would make results irreproducible.
RUN pip install --no-cache-dir \
        "fastapi==0.115.6" "uvicorn==0.34.0" "python-multipart==0.0.20"

WORKDIR /app
COPY app /app

EXPOSE 8090
HEALTHCHECK --interval=30s --timeout=5s --retries=3 \
    CMD curl -fsS http://127.0.0.1:8090/health || exit 1

CMD ["uvicorn", "main:app", "--host", "0.0.0.0", "--port", "8090"]
