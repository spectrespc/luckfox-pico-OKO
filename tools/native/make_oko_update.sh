#!/usr/bin/env bash
# Build an OKO update package (.okp) from the images of the last firmware build.
#
#   tools/native/make_oko_update.sh --version 1.5.0
#   tools/native/make_oko_update.sh --version 1.5.0 --parts boot,oem \
#       -o /tmp/oko-update-test.okp
#
# The package is an uncompressed tar with the partition images and the
# oko-update.conf manifest.  The manifest carries the SPI NAND offsets and the
# full partition sizes, so the on-device stager does not depend on the board
# config.  Only boot, oem and rootfs are allowed in v1: env/idblock/uboot can
# brick the board and are flashed with the USB recovery procedure instead.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
IMAGE_DIR="${OKO_IMAGE_DIR:-$ROOT/output/image}"
VERSION=""
PARTS="boot,oem,rootfs"
OUT=""

usage() {
    cat <<'EOF'
usage: make_oko_update.sh --version X.Y.Z [options]

  --version X.Y.Z   package version (required, [0-9A-Za-z._-])
  --parts LIST      comma-separated subset of boot,oem,rootfs (default: all)
  -o FILE           output .okp path (default: output/image/oko-update-X.Y.Z.okp)
  --image-dir DIR   directory with boot.img, oem.img, rootfs.img
  -h, --help        this help
EOF
}

die() {
    echo "make_oko_update: $*" >&2
    exit 1
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --version) [ "$#" -ge 2 ] || die "--version needs a value"; VERSION="$2"; shift 2 ;;
        --parts)   [ "$#" -ge 2 ] || die "--parts needs a value"; PARTS="$2"; shift 2 ;;
        -o)        [ "$#" -ge 2 ] || die "-o needs a value"; OUT="$2"; shift 2 ;;
        --image-dir) [ "$#" -ge 2 ] || die "--image-dir needs a value"; IMAGE_DIR="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) die "unknown argument: $1" ;;
    esac
done

[ -n "$VERSION" ] || { usage >&2; exit 2; }
case "$VERSION" in
    *[!0-9A-Za-z._-]*) die "invalid version '$VERSION'" ;;
esac

[ -d "$IMAGE_DIR" ] || die "image directory not found: $IMAGE_DIR"
[ -n "$OUT" ] || OUT="$IMAGE_DIR/oko-update-$VERSION.okp"

# Offset and full size of every updatable SPI NAND partition.  Keep in sync
# with output/image/.env.txt (mtdparts) and TESTBENCH.md §7B.
part_offset() {
    case "$1" in
        boot)   echo 0x100000 ;;
        oem)    echo 0x700000 ;;
        rootfs) echo 0x5700000 ;;
        *)      return 1 ;;
    esac
}

part_mtd_size() {
    case "$1" in
        boot)   echo 0x600000 ;;
        oem)    echo 0x2800000 ;;
        rootfs) echo 0xA800000 ;;
        *)      return 1 ;;
    esac
}

# The host runs in UTC; the manifest is informational, the hashes are the
# actual integrity check.
build_date="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
work="$(mktemp -d "${TMPDIR:-/tmp}/oko-update.XXXXXX")"
trap 'rm -rf "$work"' EXIT

manifest="$work/oko-update.conf"
{
    echo "OKOUPDATE=1"
    echo "VERSION=$VERSION"
    echo "DATE=$build_date"
    echo "BOARD=RV1106_Luckfox_Pico_Pro_Max"
    echo "PARTS=$PARTS"
} >"$manifest"

old_ifs="$IFS"
IFS=','
# shellcheck disable=SC2206 # intentional word splitting on commas
part_list=($PARTS)
IFS="$old_ifs"

[ "${#part_list[@]}" -gt 0 ] || die "empty --parts list"
seen=" "
for part in "${part_list[@]}"; do
    [ -n "$part" ] || die "empty part name in --parts"
    case "$seen" in
        *" $part "*) die "duplicate part '$part'" ;;
    esac
    seen="$seen$part "
    offset="$(part_offset "$part")" || die "unsupported part '$part'"
    mtd_size="$(part_mtd_size "$part")" || die "unsupported part '$part'"
    image="$IMAGE_DIR/$part.img"
    [ -f "$image" ] || die "missing image: $image"
    [ -s "$image" ] || die "empty image: $image"

    bytes="$(stat -c %s "$image")"
    mtd_bytes=$((mtd_size))
    if [ "$bytes" -gt "$mtd_bytes" ]; then
        die "$part.img is $bytes bytes but the partition is $mtd_bytes bytes"
    fi
    sha="$(sha256sum "$image" | cut -d' ' -f1)"
    cp -f "$image" "$work/$part.img"

    {
        echo "${part}_BYTES=$bytes"
        echo "${part}_SHA256=$sha"
        echo "${part}_OFFSET=$offset"
        echo "${part}_MTD_SIZE=$mtd_size"
    } >>"$manifest"
    printf '%-6s %10d bytes  sha256 %s\n' "$part" "$bytes" "$sha"
done

# Uncompressed tar keeps the on-device staging cheap (no gzip decode of 86 MiB
# on the RV1106).  The stager rejects absolute paths and directories.
tar -C "$work" --format=ustar -cf "$OUT.part" \
    oko-update.conf "${part_list[@]/%/.img}"
mv -f "$OUT.part" "$OUT"

echo "package: $OUT"
echo "version: $VERSION"
echo "manifest:"
sed 's/^/  /' "$manifest"