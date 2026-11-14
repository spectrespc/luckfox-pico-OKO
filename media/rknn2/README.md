# RKNN mini-runtime 2.x (second copy)

`lib/librknnmrt2.so` is the vendor mini-runtime from
[`airockchip/rknn-toolkit2`](https://github.com/airockchip/rknn-toolkit2),
tag `v2.5.0` (commit `2c723177a94fac65cbc036d722907ddbc91980cf`),
path `rknpu2/runtime/Linux/librknn_api/armhf-uclibc/librknnmrt.so`.

The original file has `SONAME librknnmrt.so`, exactly like the 1.6.0 copy
shipped by `media/iva/.../librockiva/`. Two libraries with one SONAME would
collapse into a single loaded object, so this copy is re-stamped:

```sh
patchelf --set-soname librknnmrt2.so librknnmrt2.so
```

Provenance:

| Field | Value |
|---|---|
| Source tag | `v2.5.0` (commit `2c723177a94fac65cbc036d722907ddbc91980cf`) |
| Reported version | `librknnmrt version: 2.5.0 (74ea630ce8@2026-09-30T09:45:21)` |
| Original SHA-256 | `632d0e505ca9c9d3da7037107a7cbc6a593b9b36007cdce600377c529484947e` |
| Patched SHA-256 | `6f1d83dd058d18d7654e1b725881ef155d02b9214e53da91891f6ec3d178d63c` |
| `patchelf` | 0.14.3 (Ubuntu 22.04) |
| In the image | `310492` bytes, SHA-256 `4b48d51977a1a32bfb1dcddb80ce605fb5bba688536d99283fcf3797a4143e6f` (the build strips debug sections with the cross `strip --strip-debug`) |

Reproduction (the image is `FROM scratch`, so a container cannot run `cat`;
`docker create` + `docker cp` extracts the file; `patchelf` never runs on the
host):

```sh
docker build -f tools/rknn-toolkit2/Dockerfile.runtime \
  -t oko-rknn-runtime2 tools/rknn-toolkit2
mkdir -p output/rknn/runtime2
docker create --name oko-runtime2-tmp oko-rknn-runtime2 /bin/true
docker cp oko-runtime2-tmp:/librknnmrt2.so output/rknn/runtime2/librknnmrt2.so
docker rm oko-runtime2-tmp
cp output/rknn/runtime2/librknnmrt2.so media/rknn2/lib/
```

`Dockerfile.runtime` pins `RKNN_TAG=v2.5.0` and checks the download against
`RKNN_SHA256` before patching, so a re-pointed tag fails the build.

`media/rknn2/Makefile` copies the file to `media/out/lib`, and the image
packaging step installs it to `/oem/usr/lib/librknnmrt2.so`.

The runtime is loaded by `divinus` only when `yolo.runtime: 2` is selected;
AIISP and RockIVA keep using the 1.6.0 `librknnmrt.so`.

## Verified on the bench (2026-11-14)

- `strings /oem/usr/lib/librknnmrt2.so` reports `2.5.0`.
- A YOLOv5n model converted by toolkit 2.5.0 (`yolov5n-640x384-r2.rknn`,
  2,659,026 bytes) loads and runs on runtime 2.5.0 at 640×384 / 5 fps.
- The YOLO11n RKOPT model (`yolo11n-640x384-r2.rknn`, 3,905,345 bytes,
  9 outputs) loads, passes the test inference and reports
  `arch=yolo11` through `/api/yolo`; NPU load 28 % at 5 fps (YOLOv5n: 17 %).
- A model built by toolkit **2.3.2** was not present in the tree any more, so
  loading an old 2.3.2 artifact on 2.5.0 is not verified. Rebuild such models
  with `RKNN_TOOLKIT_IMAGE=oko-rknn-toolkit2:2.5.0` if a load error appears
  (`incompatible model version`).