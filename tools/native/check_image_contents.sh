#!/bin/bash
# Static check of the packed OKO image content.
#
# Reads project/cfg/BoardConfig_IPC/oko-prune.list (the forbidden set) and a
# baked-in required set, then inspects the assembled directories:
#   output/out/rootfs_uclibc_rv1106/
#   output/out/oem/
#
# Usage:
#   tools/native/check_image_contents.sh
#   OKO_ROOTFS_DIR=/path OKO_OEM_DIR=/path tools/native/check_image_contents.sh
#
# Exit code: 0 when every required path is present and nothing from the prune
# list survives; 1 otherwise.

set -u

export LC_ALL=C

SDK_ROOT_DIR=$(realpath "$(dirname "$0")/../..")
PRUNE_LIST=${OKO_PRUNE_LIST:-"$SDK_ROOT_DIR/project/cfg/BoardConfig_IPC/oko-prune.list"}
ROOTFS_DIR=${OKO_ROOTFS_DIR:-"$SDK_ROOT_DIR/output/out/rootfs_uclibc_rv1106"}
OEM_DIR=${OKO_OEM_DIR:-"$SDK_ROOT_DIR/output/out/oem"}

# Paths that must survive: the OKO application set and the diagnostic tools
# referenced by TESTBENCH.md.
OEM_REQUIRED=(
	usr/bin/divinus
	usr/bin/oko-lunch.sh
	usr/bin/oko-lunch-stop.sh
	usr/bin/oko-ec
	usr/bin/oko-flash
	usr/bin/oko-motor
	usr/bin/wg
	usr/bin/awg
	usr/bin/rgaImDemo
	usr/share/simsun_en.ttf
	usr/share/iqfiles/sc230ai_CMK-OT2115-PC1_30IRC-F16.json
	usr/lib/librkaiq.so
	usr/lib/librockit.so
	usr/lib/librga.so
	usr/ko/sc230ai.ko
	usr/ko/rockit.ko
)

ROOTFS_REQUIRED=(
	usr/bin/v4l2-ctl
	usr/bin/media-ctl
	usr/bin/dtc
	usr/bin/oko-update
	etc/init.d/S20oko-update
	etc/oko-release
	usr/sbin/fw_setenv
)

failures=0
checked_required=0
checked_forbidden=0

note() {
	printf '%s\n' "$*"
}

for dir in "$ROOTFS_DIR" "$OEM_DIR"; do
	if [ ! -d "$dir" ]; then
		note "FAIL: directory is missing: $dir"
		failures=$((failures + 1))
	fi
done
if [ ! -f "$PRUNE_LIST" ]; then
	note "FAIL: prune list is missing: $PRUNE_LIST"
	failures=$((failures + 1))
fi

if [ -d "$OEM_DIR" ]; then
	for rel in "${OEM_REQUIRED[@]}"; do
		checked_required=$((checked_required + 1))
		if [ ! -e "$OEM_DIR/$rel" ]; then
			note "FAIL: required oem path is missing: $rel"
			failures=$((failures + 1))
		fi
	done
fi

if [ -d "$ROOTFS_DIR" ]; then
	for rel in "${ROOTFS_REQUIRED[@]}"; do
		checked_required=$((checked_required + 1))
		if [ ! -e "$ROOTFS_DIR/$rel" ]; then
			note "FAIL: required rootfs path is missing: $rel"
			failures=$((failures + 1))
		fi
	done
fi

if [ -f "$PRUNE_LIST" ]; then
	while IFS= read -r line || [ -n "$line" ]; do
		rel=$(printf '%s\n' "$line" |
			sed -e 's/[[:space:]]#.*$//' -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')
		case "$rel" in
		'' | '#'*) continue ;;
		esac
		checked_forbidden=$((checked_forbidden + 1))
		for base in "$ROOTFS_DIR" "$OEM_DIR"; do
			[ -d "$base" ] || continue
			# shellcheck disable=SC2086
			matches=$(compgen -G "$base/$rel" || true)
			for match in $matches; do
				note "FAIL: pruned path survived in $base: ${match#"$base"/}"
				failures=$((failures + 1))
			done
		done
	done <"$PRUNE_LIST"
fi

if [ "$failures" -ne 0 ]; then
	note "Image content check failed: $failures problem(s); " \
		"$checked_required required and $checked_forbidden forbidden paths checked."
	exit 1
fi

note "Image content check passed: $checked_required required and " \
	"$checked_forbidden forbidden paths checked."
exit 0