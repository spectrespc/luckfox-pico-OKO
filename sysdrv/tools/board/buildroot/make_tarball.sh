#!/usr/bin/env bash
# Fetch the upstream Buildroot release tarball used by the OKO SDK.
#
#   sysdrv/tools/board/buildroot/make_tarball.sh [VERSION]
#
# The vendor delta (defconfigs, busybox.config, package patches) lives next to
# this script and is applied by sysdrv/Makefile buildroot_create; the tarball
# itself stays pristine upstream so it can be re-fetched and verified.
set -euo pipefail

VERSION="${1:-2026.08.1}"
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TARBALL="$DIR/buildroot-$VERSION.tar.gz"
URL="https://buildroot.org/downloads/buildroot-$VERSION.tar.gz"

# SHA-256 of the official release tarball. Add new releases here.
declare -A SHA256=(
    [2026.08.1]="38a6c152490096018720a9ade379ee1e571c9bf5f93a3657a166566945a98b0d"
)

expected="${SHA256[$VERSION]:-}"
if [ -z "$expected" ]; then
    echo "no pinned sha256 for buildroot-$VERSION" >&2
    exit 2
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

curl -fsSL -o "$tmp/buildroot-$VERSION.tar.gz" "$URL"
echo "$expected  $tmp/buildroot-$VERSION.tar.gz" | sha256sum -c -
mv "$tmp/buildroot-$VERSION.tar.gz" "$TARBALL"

echo "wrote $TARBALL"