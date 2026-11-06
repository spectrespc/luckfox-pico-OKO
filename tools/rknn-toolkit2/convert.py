#!/usr/bin/env python3
"""Convert an RKNN-optimized YOLOv5 ONNX model to a quantized .rknn file."""
import sys

from rknn.api import RKNN


def main():
    if len(sys.argv) != 4:
        print("usage: convert.py MODEL.onnx DATASET.txt OUTPUT.rknn")
        return 2
    model, dataset, output = sys.argv[1:4]

    rknn = RKNN(verbose=False)
    ret = rknn.config(mean_values=[[0, 0, 0]], std_values=[[255, 255, 255]],
                      target_platform='rv1106',
                      quantized_dtype='asymmetric_quantized-8')
    print('config', ret)
    ret = rknn.load_onnx(model=model)
    print('load', ret)
    ret = rknn.build(do_quantization=True, dataset=dataset)
    print('build', ret)
    ret = rknn.export_rknn(output)
    print('export', ret)
    rknn.release()
    return 0 if ret == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
