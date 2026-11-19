FROM python:3.12-slim

ENV DEBIAN_FRONTEND=noninteractive \
    PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1

# Pinned: this recorder is part of the documented bench chain, so a moving
# "latest" would make MQTT results irreproducible.
RUN apt-get update && apt-get install -y --no-install-recommends \
        curl \
    && rm -rf /var/lib/apt/lists/*

RUN pip install --no-cache-dir \
        "fastapi==0.115.6" "uvicorn==0.34.0" "paho-mqtt==2.1.0"

WORKDIR /app
COPY app /app

EXPOSE 8092
HEALTHCHECK --interval=30s --timeout=5s --retries=3 \
    CMD curl -fsS http://127.0.0.1:8092/health || exit 1

CMD ["uvicorn", "main:app", "--host", "0.0.0.0", "--port", "8092"]