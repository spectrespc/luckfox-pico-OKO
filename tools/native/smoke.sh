#!/usr/bin/env bash
# Smoke-test the native (no container) build environment.
#
#   tools/native/smoke.sh
#
# Writes a report to output/bench/native/smoke.log and returns non-zero when a
# required tool or shared library is missing.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${OKO_NATIVE_OUT:-$ROOT/output/bench/native}"
LOG="$OUT/smoke.log"
TC_BIN="$ROOT/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf/bin"

# shellcheck source=env.sh
. "$ROOT/tools/native/env.sh"

mkdir -p "$OUT"
: >"$LOG"

fail=0
say() { echo "$@" | tee -a "$LOG"; }
run() {
    echo "--- $*" >>"$LOG"
    "$@" >>"$LOG" 2>&1 || { echo "FAILED: $*" | tee -a "$LOG"; fail=1; }
}

say "== host =="
say "$(grep PRETTY_NAME /etc/os-release)"
run gcc --version
run g++ --version
run make --version
run cmake --version
run ninja --version
run pkg-config --version
run dtc --version
run mkimage -V
run mkenvimage -V
run mksquashfs -version
run mkfs.ext4 -V
run python3 --version

say "== cross toolchain =="
run "$TC_BIN/arm-rockchip830-linux-uclibcgnueabihf-gcc" --version
run "$TC_BIN/arm-rockchip830-linux-uclibcgnueabihf-as" --version
for tool in gcc as ld; do
    if ldd "$TC_BIN/arm-rockchip830-linux-uclibcgnueabihf-$tool" 2>/dev/null | grep -q 'not found'; then
        say "FAILED: missing shared libraries for arm-rockchip830-$tool"
        ldd "$TC_BIN/arm-rockchip830-linux-uclibcgnueabihf-$tool" 2>&1 | grep 'not found' | tee -a "$LOG"
        fail=1
    fi
done

say "== rockusb upgrade_tool =="
run "$ROOT/tools/linux/Linux_Upgrade_Tool/upgrade_tool" ld

say "== locale =="
run locale charmap
run locale -a

if [ "$fail" -ne 0 ]; then
    say "RESULT: FAIL (see $LOG)"
    exit 1
fi
say "RESULT: OK (see $LOG)"