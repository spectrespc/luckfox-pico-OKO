# RKNN mini-runtime 2.x (second copy)

`lib/librknnmrt2.so` is the vendor mini-runtime from
[`airockchip/rknn-toolkit2`](https://github.com/airockchip/rknn-toolkit2),
tag `v2.3.2` (commit `42aa1d426c0a9e0869b6374edba009f7208a1926`),
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
| Source tag | `v2.3.2` (2025-04-01) |
| Source commit | `42aa1d426c0a9e0869b6374edba009f7208a1926` |
| Original SHA-256 | `5dcaf6201a019b24d99bb05560b79a1a53fdf81451b89e0ada541ccf31cd3e84` |
| Patched SHA-256 | `17935808325a59a6045158d1364ca0b249c9580f089f536dea90a027937e4bd4` |
| patchelf | 0.14.3 (Ubuntu 22.04) |

Reproduction (patchelf runs in a throw-away image, never on the host):

```sh
docker build -f tools/rknn-toolkit2/Dockerfile.runtime \
  -t oko-rknn-runtime2 tools/rknn-toolkit2
mkdir -p output/rknn/runtime2
docker run --rm oko-rknn-runtime2 > output/rknn/runtime2/librknnmrt2.so
cp output/rknn/runtime2/librknnmrt2.so media/rknn2/lib/
```

`Dockerfile.runtime` pins `RKNN_TAG=v2.3.2`; the original download is checked
against the SHA-256 above before patching.

`media/rknn2/Makefile` copies the file to `media/out/lib`, and the image
packaging step installs it to `/oem/usr/lib/librknnmrt2.so`.

The runtime is loaded by `divinus` only when `yolo.runtime: 2` is selected;
AIISP and RockIVA keep using the 1.6.0 `librknnmrt.so`.