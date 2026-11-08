#!/usr/bin/env bash
# Native build environment for the OKO SDK (former Docker ENV block).
#
#   source tools/native/env.sh
#   ./build.sh firmware
#
# The vendor cross toolchain is used for target code; host tools are built by
# the system gcc. Nothing here is required after the shell exits.
#
# Set OKO_LOG=1 to print the exported values.

_oko_native_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
_oko_tc_bin="$_oko_native_root/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf/bin"

if [ ! -x "$_oko_tc_bin/arm-rockchip830-linux-uclibcgnueabihf-gcc" ]; then
    echo "native env: cross toolchain not found at $_oko_tc_bin" >&2
    return 1 2>/dev/null || exit 1
fi

case ":$PATH:" in
    *":$_oko_tc_bin:"*) ;;
    *) export PATH="$_oko_tc_bin:$PATH" ;;
esac

# Image tools (mkfs.ext4, mkfs.vfat) live in sbin, which is missing from the
# PATH of non-login shells.
for _oko_sbin in /usr/sbin /sbin; do
    case ":$PATH:" in
        *":$_oko_sbin:"*) ;;
        *) export PATH="$PATH:$_oko_sbin" ;;
    esac
done

export CROSS_COMPILE="arm-rockchip830-linux-uclibcgnueabihf-"
export ARCH="arm"

# The old container exported en_US.UTF-8; C.UTF-8 is always generated on
# Debian and keeps the build free of locale packages.
export LANG=C.UTF-8
export LC_ALL=C.UTF-8

if [ "${OKO_LOG:-0}" = "1" ]; then
    echo "PATH+= $_oko_tc_bin"
    echo "CROSS_COMPILE=$CROSS_COMPILE"
    echo "ARCH=$ARCH"
    echo "LANG=$LANG LC_ALL=$LC_ALL"
fi

unset _oko_native_root _oko_tc_bin _oko_sbin