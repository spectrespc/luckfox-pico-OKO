#!/usr/bin/env python3
"""Generate a throw-away ONNX model and synthetic calibration JPEGs.

The fixture is only for exercising services/converter end-to-end without a
real model: a two-conv network with a static 640x384 NCHW input and 25
deterministic JPEGs.  It writes only to the requested directory -- use /tmp or
output/srv/converter, never a git-tracked path.

Run it from the converter image (the host needs no Python deps):

    docker run --rm -v "$PWD/output/srv/converter:/out" \
        --entrypoint python oko-converter:latest \
        /app/tools_fixture/make_fixture.py --out /out/fixtures
"""
import argparse
import os

import numpy as np


def build_onnx(path, height, width, seed):
    import onnx
    from onnx import TensorProto, helper, numpy_helper

    rng = np.random.default_rng(seed)

    def conv_weights(out_channels, in_channels):
        weights = (rng.standard_normal((out_channels, in_channels, 3, 3))
                   * 0.08).astype(np.float32)
        bias = (rng.standard_normal(out_channels) * 0.01).astype(np.float32)
        return weights, bias

    w1, b1 = conv_weights(8, 3)
    w2, b2 = conv_weights(16, 8)

    nodes = [
        helper.make_node("Conv", ["input", "w1", "b1"], ["conv1"],
                         name="conv1", kernel_shape=[3, 3], pads=[1, 1, 1, 1],
                         strides=[2, 2]),
        helper.make_node("Relu", ["conv1"], ["relu1"], name="relu1"),
        helper.make_node("Conv", ["relu1", "w2", "b2"], ["conv2"],
                         name="conv2", kernel_shape=[3, 3], pads=[1, 1, 1, 1],
                         strides=[2, 2]),
        helper.make_node("Relu", ["conv2"], ["output"], name="relu2"),
    ]
    graph = helper.make_graph(
        nodes, "oko_converter_fixture",
        [helper.make_tensor_value_info("input", TensorProto.FLOAT,
                                       [1, 3, height, width])],
        [helper.make_tensor_value_info("output", TensorProto.FLOAT,
                                       [1, 16, height // 4, width // 4])],
        initializer=[
            numpy_helper.from_array(w1, "w1"),
            numpy_helper.from_array(b1, "b1"),
            numpy_helper.from_array(w2, "w2"),
            numpy_helper.from_array(b2, "b2"),
        ])
    model = helper.make_model(graph,
                              opset_imports=[helper.make_opsetid("", 13)])
    # IR 8 is understood by every toolkit 2.x loader and by onnx 1.16/1.17.
    model.ir_version = 8
    onnx.checker.check_model(model)
    onnx.save(model, path)


def write_jpegs(directory, count, height, width, seed):
    import cv2

    rng = np.random.default_rng(seed + 1)
    os.makedirs(directory, exist_ok=True)
    gradient = np.linspace(0, 255, width, dtype=np.float32)[None, :]
    paths = []
    for index in range(count):
        frame = np.zeros((height, width, 3), dtype=np.float32)
        frame[..., 0] = gradient
        frame[..., 1] = gradient[::-1]
        frame[..., 2] = 40 + (index * 7) % 180
        noise = rng.normal(0, 6, (height, width, 1)).astype(np.float32)
        frame = np.clip(frame + noise, 0, 255).astype(np.uint8)
        # A moving box gives the quantizer something with real edges.
        x = (index * 23) % max(1, width - 80)
        y = (index * 11) % max(1, height - 60)
        cv2.rectangle(frame, (x, y), (x + 80, y + 60), (255, 255, 255), 2)
        path = os.path.join(directory, "frame-%03d.jpg" % index)
        if not cv2.imwrite(path, frame, [cv2.IMWRITE_JPEG_QUALITY, 92]):
            raise RuntimeError("failed to write %s" % path)
        paths.append(path)
    return paths


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", default="/tmp/oko-converter-fixture",
                        help="output directory (default /tmp)")
    parser.add_argument("--images", type=int, default=25,
                        help="number of JPEG calibration frames")
    parser.add_argument("--height", type=int, default=384)
    parser.add_argument("--width", type=int, default=640)
    parser.add_argument("--seed", type=int, default=20261114)
    args = parser.parse_args()

    os.makedirs(args.out, exist_ok=True)
    model_path = os.path.join(args.out, "fixture.onnx")
    build_onnx(model_path, args.height, args.width, args.seed)
    calib_dir = os.path.join(args.out, "calib")
    images = write_jpegs(calib_dir, args.images, args.height, args.width,
                         args.seed)
    with open(os.path.join(calib_dir, "dataset.txt"), "w") as handle:
        handle.write("\n".join(images) + "\n")

    print("model:   %s (%d bytes)" % (model_path, os.path.getsize(model_path)))
    print("calib:   %s (%d JPEG)" % (calib_dir, len(images)))
    print("dataset: %s" % os.path.join(calib_dir, "dataset.txt"))
    print("\nCLI path check:")
    print("  tools/rknn-toolkit2/convert_rknn.sh %s %s /tmp/fixture.rknn"
          % (model_path, calib_dir))


if __name__ == "__main__":
    main()