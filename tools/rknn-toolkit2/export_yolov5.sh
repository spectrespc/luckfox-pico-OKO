#!/bin/sh
# Export airockchip YOLOv5 to ONNX with fixed HxW input for RKNN.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
HEIGHT=${1:-384}
WIDTH=${2:-640}
WORK="$ROOT/output/rknn"
REPO="$WORK/yolov5"
COMMIT=d25a07534c14f44296f9444bab2aa5c601cdaaab

mkdir -p "$WORK"
if [ ! -d "$REPO/.git" ]; then
    git clone --depth 1 https://github.com/airockchip/yolov5.git "$REPO"
fi
if ! git -C "$REPO" cat-file -e "$COMMIT^{commit}" 2>/dev/null; then
    git -C "$REPO" fetch --depth 1 origin "$COMMIT"
fi
git -C "$REPO" checkout -q "$COMMIT"

if [ ! -f "$WORK/yolov5n.pt" ]; then
    curl -fL -o "$WORK/yolov5n.pt" \
        https://github.com/ultralytics/yolov5/releases/download/v7.0/yolov5n.pt
fi

docker run --rm -v "$WORK:/work" -w /work/yolov5 oko-yolov5-export \
    bash -lc "python export.py --rknpu --weights /work/yolov5n.pt \
        --imgsz $HEIGHT $WIDTH --batch-size 1 --include onnx --opset 12"

OUT="$WORK/yolov5n-${WIDTH}x${HEIGHT}.onnx"
mv "$WORK/yolov5n.onnx" "$OUT"
cp "$REPO/RK_anchors.txt" "$WORK/RK_anchors.txt"
echo "ONNX: $OUT"
echo "anchors: $WORK/RK_anchors.txt"
