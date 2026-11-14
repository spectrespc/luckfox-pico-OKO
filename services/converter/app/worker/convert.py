#!/usr/bin/env python3
"""Convert one ONNX model to RKNN for the rv1106 NPU.

This is the child-process half of the web converter.  Its RKNN parameters are
deliberately identical to tools/rknn-toolkit2/convert.py (target_platform
rv1106, mean 0, std 255, asymmetric_quantized-8) so a model produced through
the web UI is the same as one produced by the CLI path.

Usage:
    convert.py MODEL.onnx OUTPUT.rknn --dtype int8 --dataset DATASET.txt
    convert.py MODEL.onnx OUTPUT.rknn --dtype fp16

Exit status is 0 only when every toolkit stage returned 0 and the output file
was written; the parent worker turns a non-zero status into a failed job.
"""
import argparse
import os
import sys


def _stage(name, ret):
    """Mirror convert.py's per-stage report and stop on the first error."""
    print("{} {}".format(name, ret), flush=True)
    return ret == 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("model", help="input .onnx")
    parser.add_argument("output", help="output .rknn")
    parser.add_argument("--dtype", choices=("int8", "fp16"), default="int8")
    parser.add_argument("--dataset", help="calibration dataset.txt (INT8 only)")
    args = parser.parse_args(argv)

    if args.dtype == "int8":
        if not args.dataset:
            print("error: INT8 conversion needs --dataset", file=sys.stderr)
            return 2
        if not os.path.isfile(args.dataset):
            print("error: dataset file %s is missing" % args.dataset,
                  file=sys.stderr)
            return 2
    if not os.path.isfile(args.model):
        print("error: model file %s is missing" % args.model, file=sys.stderr)
        return 2

    from rknn.api import RKNN

    rknn = RKNN(verbose=False)
    try:
        ret = rknn.config(mean_values=[[0, 0, 0]],
                          std_values=[[255, 255, 255]],
                          target_platform="rv1106",
                          quantized_dtype="asymmetric_quantized-8")
        if not _stage("config", ret):
            return 1

        ret = rknn.load_onnx(model=args.model)
        if not _stage("load", ret):
            return 1

        quantize = args.dtype == "int8"
        ret = rknn.build(do_quantization=quantize,
                         dataset=args.dataset if quantize else None)
        if not _stage("build", ret):
            return 1

        # Write into the job directory under a temporary name so a killed
        # process never leaves a truncated model.rknn behind.
        tmp_output = args.output + ".part"
        if os.path.exists(tmp_output):
            os.unlink(tmp_output)
        ret = rknn.export_rknn(tmp_output)
        if not _stage("export", ret):
            return 1
        size = os.path.getsize(tmp_output)
        if size <= 0:
            print("error: toolkit wrote an empty model", file=sys.stderr)
            return 1
        os.replace(tmp_output, args.output)
        print("done %s (%d bytes, %s)" % (args.output, size, args.dtype),
              flush=True)
        return 0
    finally:
        rknn.release()


if __name__ == "__main__":
    sys.exit(main())