#!/usr/bin/env bash
# Build an AOV Hpmcu image and stage it for the idblock hook.
#
#   tools/native/build_hpmcu.sh [board]
#
# The default board is the OKO AOV board; build.sh mcu compiles it with
# RT-Thread and copies rtthread.bin to output/out/mcu_out/rtthread.bin.
# project/build.sh build_uboot() then substitutes that path into the
# RV1106MINIALL_SPI_NAND_HPMCU.ini overlay and packs the idblock.
#
# The profile is experimental (KI-072): the idblock is 512K in the -AOV
# BoardConfig, and the MCU image is ~117 KB.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BOARD="${1:-oko-wakeup-sc230ai-ADC}"

exec "$ROOT/build.sh" mcu "$BOARD"