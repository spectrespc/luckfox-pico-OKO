#!/bin/sh
# Quantize an RKNN-optimized ONNX model on calibration JPEGs.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
ONNX=${1:?usage: convert_rknn.sh MODEL.onnx CALIB_DIR OUTPUT.rknn}
CALIB=${2:?usage: convert_rknn.sh MODEL.onnx CALIB_DIR OUTPUT.rknn}
OUTPUT=${3:?usage: convert_rknn.sh MODEL.onnx CALIB_DIR OUTPUT.rknn}
WORK="$ROOT/output/rknn"

ONNX=$(CDPATH= cd -- "$(dirname -- "$ONNX")" && pwd)/$(basename -- "$ONNX")
CALIB=$(CDPATH= cd -- "$CALIB" && pwd)
mkdir -p "$WORK"
OUTPUT=$WORK/$(basename -- "$OUTPUT")

find "$CALIB" -maxdepth 1 -type f -name '*.jpg' | sort | \
    sed "s|^$CALIB/|/calib/|" > "$WORK/dataset.txt"
COUNT=$(wc -l < "$WORK/dataset.txt")
if [ "$COUNT" -lt 20 ]; then
    echo "need at least 20 calibration JPEGs, got $COUNT" >&2
    exit 1
fi

docker run --rm \
    -v "$(dirname -- "$ONNX")":/onnx:ro \
    -v "$CALIB":/calib:ro \
    -v "$ROOT/tools/rknn-toolkit2":/scripts:ro \
    -v "$WORK":/work \
    -w /work "${RKNN_TOOLKIT_IMAGE:-oko-rknn-toolkit2:1.6.0}" \
    python /scripts/convert.py "/onnx/$(basename -- "$ONNX")" \
        /work/dataset.txt "/work/$(basename -- "$OUTPUT")"

echo "RKNN: $OUTPUT"
echo "calibration frames: $COUNT"
