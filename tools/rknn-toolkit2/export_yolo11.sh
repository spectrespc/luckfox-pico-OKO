#!/bin/sh
# Export airockchip YOLO11 to ONNX with a fixed HxW input for RKNN.
#
# The fork strips the DFL/post-processing tail from the graph and adds a
# score-sum branch, so the ONNX output is the 9-tensor RKOPT layout:
# 3 scales x [box 64, cls 80, sum 1] for COCO.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
HEIGHT=${1:-384}
WIDTH=${2:-640}
WORK="$ROOT/output/rknn"
REPO="$WORK/ultralytics_yolo11"
# Pinned 2024-10-23: "[yolo11_rknn] update README and export instructions".
COMMIT=0692e9297670acf4cc6d0cec773d7a9493cb8a5f

mkdir -p "$WORK"
if [ ! -d "$REPO/.git" ]; then
    git clone https://github.com/airockchip/ultralytics_yolo11.git "$REPO"
fi
if ! git -C "$REPO" cat-file -e "$COMMIT^{commit}" 2>/dev/null; then
    git -C "$REPO" fetch origin "$COMMIT"
fi
git -C "$REPO" checkout -q "$COMMIT"

if [ ! -f "$WORK/yolo11n.pt" ]; then
    curl -fL -o "$WORK/yolo11n.pt" \
        https://github.com/ultralytics/assets/releases/download/v8.3.0/yolo11n.pt
fi

# The exporter reads everything from ultralytics/cfg/default.yaml.
sed -i \
    -e "s|^model: .*|model: /work/yolo11n.pt|" \
    -e "s|^imgsz: .*|imgsz: [$HEIGHT, $WIDTH]|" \
    -e "s|^format: .*|format: rknn|" \
    -e "s|^simplify: .*|simplify: True|" \
    "$REPO/ultralytics/cfg/default.yaml"

docker run --rm -v "$WORK:/work" -w /work/ultralytics_yolo11 \
    -e PYTHONPATH=/work/ultralytics_yolo11 oko-yolo11-export \
    python ./ultralytics/engine/exporter.py

OUT="$WORK/yolo11n-${WIDTH}x${HEIGHT}.onnx"
mv "$WORK/yolo11n.onnx" "$OUT"
echo "ONNX: $OUT"
