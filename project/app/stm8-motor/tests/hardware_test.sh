#!/usr/bin/env bash
# hardware_test.sh — automated on-target tests for the STM8S103 + DRV8833 firmware.
#
# Covers stages 2–13 of the test plan. Manual-only checks are intentionally
# skipped and listed at the end: 0.4 visual wiring, 1.5 LED, 5.x visual
# rotation, 8.x mechanical stall (nFAULT), 9.2/9.3 power cycle EEPROM.
#
# Requires: i2cdetect, i2ctransfer, and a built motorctl (../util/motorctl).
# Run from anywhere; the paths are resolved relative to this script.
#
# Stage 12 needs a quiet bus: stop divinus first (`killall divinus`).  Its
# firmware_status poll runs every ~2 s, which is shorter than the 3 s sleep
# idle threshold, so with divinus running the controller never enters sleep
# and the wake counters stay 0.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MCTL="$SCRIPT_DIR/../util/motorctl"

BUS=${BUS:-7}
ADDR=${ADDR:-0x60}
FW_VERSION=${FW_VERSION:-2.3.2}
# Raw 0xFF reply derives from FW_VERSION (e.g. 2.3.2 -> "0x02 0x03 0x02").
IFS=. read -r FW_MAJ FW_MIN FW_PAT <<EOF
$FW_VERSION
EOF
RAW_VERSION=${RAW_VERSION:-$(printf '0x%02x 0x%02x 0x%02x' \
    "$((10#$FW_MAJ))" "$((10#$FW_MIN))" "$((10#$FW_PAT))")}
I2CDETECT=$(command -v i2cdetect || echo /usr/sbin/i2cdetect)
I2CTRANSFER=$(command -v i2ctransfer || echo /usr/sbin/i2ctransfer)

PASS=0
FAIL=0
RESULTS=""

if [ ! -x "$MCTL" ]; then
    echo "ERROR: $MCTL not found or not executable. Run: make -C \"$SCRIPT_DIR/../util\"" >&2
    exit 2
fi

# --- assertion helpers -------------------------------------------------------

pass() {
    echo "  PASS: $1"
    PASS=$((PASS + 1))
    RESULTS+="PASS $1"$'\n'
}

fail() {
    echo "  FAIL: $1 ${2:-}"
    FAIL=$((FAIL + 1))
    RESULTS+="FAIL $1 ${2:-}"$'\n'
}

# check_contains NAME EXPECTED CMD... — output must contain EXPECTED (regex)
check_contains() {
    local name="$1" expected="$2"; shift 2
    local output
    output=$("$@" 2>&1) || true
    if grep -q "$expected" <<<"$output"; then
        pass "$name"
    else
        fail "$name" "(expected '$expected', got '$output')"
    fi
}

# check_silent NAME CMD... — command must exit 0 (i2ctransfer success = no I2C error)
check_silent() {
    local name="$1"; shift
    if "$@" >/dev/null 2>&1; then
        pass "$name"
    else
        fail "$name" "(command failed: $*)"
    fi
}

# check_no_i2c_error NAME CMD... — for fire-and-forget frames the firmware should
# ACK but silently ignore: assert no "Error" in output AND exit 0.
check_no_i2c_error() {
    local name="$1"; shift
    local output rc
    output=$("$@" 2>&1) && rc=0 || rc=$?
    if [ "$rc" -eq 0 ] && ! grep -qi "error" <<<"$output"; then
        pass "$name"
    else
        fail "$name" "(rc=$rc, out='$output')"
    fi
}

# Focus/zoom position as an integer, robust to the "focus motor position: N" text.
get_pos() {
    mctl position "$1" 2>/dev/null | grep -oE '\-?[0-9]+' | head -1 || true
}

# check_pos NAME EXPECTED MOTOR — numeric compare with a 1-step tolerance
check_pos() {
    local name="$1" expected="$2" motor="$3"
    local got delta
    got=$(get_pos "$motor")
    delta=$(( got - expected )); delta=${delta#-}
    if [ -n "$got" ] && [ "$delta" -le 1 ]; then
        pass "$name"
    else
        fail "$name" "(expected $expected, got '${got:-<empty>}')"
    fi
}

# Motor status code from 0x05: 0 IDLE, 1 RUNNING, 2 ERROR (motorctl prints the
# number, not a word; the stage names keep the protocol meaning readable).
STATUS_IDLE=0
STATUS_RUNNING=1
STATUS_ERROR=2

get_status() {
    mctl status "$1" 2>/dev/null | grep -oE '^[0-9]+' | head -1 || true
}

# check_status NAME EXPECTED MOTOR — exact status code compare
check_status() {
    local name="$1" expected="$2" motor="$3"
    local got
    got=$(get_status "$motor")
    if [ -n "$got" ] && [ "$got" = "$expected" ]; then
        pass "$name"
    else
        fail "$name" "(expected status $expected, got '${got:-<empty>}')"
    fi
}

# Reads and idempotent commands retry: the controller may be sleeping and the
# first frame after a wake can be refused (the address match holds SCL until
# the core is running).  Raw i2ctransfer calls below stay single-attempt on
# purpose: that is what the wake-reliability metrics measure.
mctl() {
    local i
    for i in 1 2 3 4 5 6; do
        "$MCTL" "$@" && return 0
        sleep 0.3
    done
    "$MCTL" "$@"
}

# Relative moves are NOT idempotent: a replay after a read-phase timeout would
# move the motor twice and either break the position checks or hide a real
# defect.  One attempt only; the surrounding check_pos reports the outcome.
mctl_once() {
    "$MCTL" "$@"
}

reset_mcu() { "$I2CTRANSFER" -y "$BUS" "w1@$ADDR" 0x06 >/dev/null 2>&1 || true; sleep 1; }

echo "=== Stage 2: I2C communication ==="

# 2.1 Detection: the 0x60 row must list the address itself, not only the row label.
# Capture the scan first: piping i2cdetect straight into `grep -q` lets grep exit
# after the first match, so i2cdetect dies with SIGPIPE (141); under `set -o
# pipefail` that non-zero status is misread as "device absent".
DETECT_OUT=$("$I2CDETECT" -y "$BUS" 2>/dev/null || true)
if grep -qE '^60:[[:space:]]+60' <<<"$DETECT_OUT"; then
    pass "2.1 I2C detect 0x60"
else
    fail "2.1 I2C detect 0x60" "(address not present on bus $BUS)"
fi
check_contains "2.2 Firmware version (motorctl)" "$FW_VERSION" mctl version
check_contains "2.3 Raw version" "$RAW_VERSION" "$I2CTRANSFER" -y "$BUS" "w1@$ADDR" 0xFF r3

echo "=== Stage 3: Focus motor ==="
reset_mcu
check_status "3.1 Focus status IDLE" "$STATUS_IDLE" focus
BASE_FOCUS=$(get_pos focus); BASE_FOCUS=${BASE_FOCUS:-0}
echo "  (focus baseline = $BASE_FOCUS)"

check_silent "3.2 Focus speed 500" mctl speed focus 500
check_no_i2c_error "3.2 Speed=0 clamp (no I2C error)" "$I2CTRANSFER" -y "$BUS" "w4@$ADDR" 0x02 0x00 0x00 0x00
check_no_i2c_error "3.2 Speed>5000 clamp (no I2C error)" "$I2CTRANSFER" -y "$BUS" "w4@$ADDR" 0x02 0x00 0x27 0x10
check_silent "3.2 Focus speed back to 500" mctl speed focus 500

mctl_once move focus 200 >/dev/null 2>&1
sleep 1
check_pos "3.4 Pos after +200" "$((BASE_FOCUS + 200))" focus

mctl_once move focus -200 >/dev/null 2>&1
sleep 1
check_pos "3.5 Pos after -200" "$BASE_FOCUS" focus

# 3.6 Move rejected while speed==0: reset clears speed, so the move must be a no-op.
reset_mcu
P0=$(get_pos focus)
mctl_once move focus 100 >/dev/null 2>&1
sleep 1
P1=$(get_pos focus)
if [ -n "$P0" ] && [ "$P0" = "$P1" ]; then pass "3.6 Move without speed is a no-op"; else fail "3.6 Move without speed is a no-op" "($P0 -> $P1)"; fi

echo "=== Stage 4: Zoom motor ==="
reset_mcu
mctl speed zoom 500 >/dev/null 2>&1
check_status "4.1 Zoom status IDLE" "$STATUS_IDLE" zoom
BASE_ZOOM=$(get_pos zoom); BASE_ZOOM=${BASE_ZOOM:-0}
echo "  (zoom baseline = $BASE_ZOOM)"

mctl_once move zoom 200 >/dev/null 2>&1
sleep 1
check_pos "4.1 Zoom +200" "$((BASE_ZOOM + 200))" zoom

mctl_once move zoom -200 >/dev/null 2>&1
sleep 1
check_pos "4.1 Zoom -200" "$BASE_ZOOM" zoom

# 4.2 Move rejected while speed==0.
reset_mcu
P0=$(get_pos zoom)
mctl_once move zoom 100 >/dev/null 2>&1
sleep 1
P1=$(get_pos zoom)
if [ -n "$P0" ] && [ "$P0" = "$P1" ]; then pass "4.2 Zoom move without speed is a no-op"; else fail "4.2 Zoom move without speed is a no-op" "($P0 -> $P1)"; fi

echo "=== Stage 5: Simultaneous operation ==="
reset_mcu
mctl speed focus 300 >/dev/null 2>&1
mctl speed zoom 300 >/dev/null 2>&1
mctl_once move focus 300 >/dev/null 2>&1 &
mctl_once move zoom 300 >/dev/null 2>&1 &
wait
sleep 2
check_status "5.1 Focus IDLE after dual move" "$STATUS_IDLE" focus
check_status "5.1 Zoom IDLE after dual move" "$STATUS_IDLE" zoom
# No nFAULT/over-current during simultaneous motion is the real criterion:
# both motors must be IDLE (not ERROR) after the dual move.

echo "=== Stage 7: Boundary cases ==="
mctl speed focus 200 >/dev/null 2>&1
P0=$(get_pos focus)
mctl_once move focus 0 >/dev/null 2>&1
sleep 0.5
P1=$(get_pos focus)
if [ -n "$P0" ] && [ "$P0" = "$P1" ]; then pass "7.1 Move 0 steps is a no-op"; else fail "7.1 Move 0 steps is a no-op" "($P0 -> $P1)"; fi

# 7.2 int16 min: accept the command, start, then stop.
mctl speed focus 200 >/dev/null 2>&1
mctl_once move focus -32768 >/dev/null 2>&1
sleep 2
check_status "7.2 Move -32768 starts" "$STATUS_RUNNING" focus
mctl stop focus >/dev/null 2>&1
sleep 1
check_status "7.2 Stop after -32768" "$STATUS_IDLE" focus

# 7.3 int16 max.
mctl speed focus 200 >/dev/null 2>&1
mctl_once move focus 32767 >/dev/null 2>&1
sleep 2
check_status "7.3 Move +32767 starts" "$STATUS_RUNNING" focus
mctl stop focus >/dev/null 2>&1
sleep 1
check_status "7.3 Stop after +32767" "$STATUS_IDLE" focus

check_no_i2c_error "7.4 Invalid motor ID ignored" "$I2CTRANSFER" -y "$BUS" "w4@$ADDR" 0x01 0xFF 0x00 0x64
check_no_i2c_error "7.5 Unknown command ignored" "$I2CTRANSFER" -y "$BUS" "w2@$ADDR" 0x08 0x00
check_no_i2c_error "7.6 Short frame ignored" "$I2CTRANSFER" -y "$BUS" "w2@$ADDR" 0x01 0x00

echo "=== Stage 10: I2C stability ==="
fail_count=0
for _ in $(seq 1 100); do
    mctl version >/dev/null 2>&1 || fail_count=$((fail_count + 1))
done
if [ "$fail_count" -eq 0 ]; then pass "10.1 100x version (0 fails)"; else fail "10.1 100x version ($fail_count fails)"; fi

sleep 3   # > I2C_TIMEOUT_MS (2000 ms)
check_contains "10.2 I2C timeout recovery" "$FW_VERSION" mctl version

for i in $(seq 1 50); do
    mctl speed focus $(( (i % 10) * 100 + 100 )) >/dev/null 2>&1
    mctl_once move focus 10 >/dev/null 2>&1
    sleep 0.05
done
sleep 2
check_status "10.3 Focus IDLE after stress" "$STATUS_IDLE" focus
check_status "10.3 Zoom IDLE after stress" "$STATUS_IDLE" zoom

echo "=== Stage 11: Watchdog (sleep is ON by default on BOARD=test) ==="
P0=$(get_pos focus)
sleep 30
P1=$(get_pos focus)
if [ -n "$P0" ] && [ "$P0" = "$P1" ]; then pass "11.1 WDT no reset (position stable)"; else fail "11.1 WDT reset detected" "($P0 -> $P1)"; fi

echo "=== Stage 12: I2C-idle sleep / wake (divinus must be stopped) ==="
SLEEP_SOAK_S=${SLEEP_SOAK_S:-1800}

sleep_entries() {
    sleep_counters | sed -n 's/.*entries \([0-9]*\).*/\1/p'
}
sleep_counter() {
    sleep_counters | sed -n "s/.*$1 \([0-9]*\).*/\1/p"
}

# The controller is a single-attempt I2C slave: the first frame after a HALT
# wake may be refused (address match holds SCL while the core resumes), so the
# diagnostic readback retries instead of aborting the whole script.
sleep_counters() {
    local out i
    for i in 1 2 3 4 5 6; do
        if out=$(mctl sleep show 2>/dev/null); then
            printf '%s\n' "$out"
            return 0
        fi
        sleep 0.3
    done
    return 1
}

# 12.1 Off: the controller must behave exactly as before the feature.
check_contains "12.1 sleep off" "sleep off" mctl sleep off
sleep 4
check_contains "12.1 version after sleep off" "$FW_VERSION" mctl version

# 12.2 On with the default 3 s idle: one raw (single-attempt) read after the
# controller had time to enter the idle sleep (Wait mode).
check_contains "12.2 sleep on" "sleep on" mctl sleep on
sleep 4
check_contains "12.2 first raw read after sleep" "$RAW_VERSION" \
    "$I2CTRANSFER" -y "$BUS" "w1@$ADDR" 0xFF r3

# 12.3 Wake reliability metric: 100 sleep/wake cycles with idle_s = 1.  Each
# iteration lets the controller sleep for > idle + slice, then performs one
# untranslated single-attempt read.  Any refusal is a lost wake-up.
mctl sleep on 1 >/dev/null 2>&1
WAKE_FAIL=0
for _ in $(seq 1 100); do
    sleep 1.5
    "$I2CTRANSFER" -y "$BUS" "w1@$ADDR" 0xFF r3 >/dev/null 2>&1 || WAKE_FAIL=$((WAKE_FAIL + 1))
done
if [ "$WAKE_FAIL" -eq 0 ]; then
    pass "12.3 100 wake cycles (0 failures)"
else
    fail "12.3 100 wake cycles ($WAKE_FAIL failures)" "(reliability metric, see the acceptance criterion)"
fi

# 12.4 Counters: entries and both wake sources must be non-zero.
E0=$(sleep_entries || true)
WI0=$(sleep_counter "wake i2c" || true)
WA0=$(sleep_counter "timer" || true)
if [ -n "$E0" ] && [ "$E0" -gt 0 ] && [ "${WI0:-0}" -gt 0 ] && [ "${WA0:-0}" -gt 0 ]; then
    pass "12.4 counters entries=$E0 i2c=$WI0 timer=$WA0"
else
    fail "12.4 counters" "(entries='${E0:-}', i2c='${WI0:-}', timer='${WA0:-}')"
fi

# 12.5 Motion immediately after a wake: positive offsets only (KI-042) and a
# clean scale from SET_POS 0.
mctl sleep on >/dev/null 2>&1
sleep 4
mctl set-pos focus 0 >/dev/null 2>&1
mctl speed focus 200 >/dev/null 2>&1
mctl_once move focus 100 >/dev/null 2>&1
sleep 2
check_pos "12.5 move completes after wake" 100 focus

# 12.6 Soak: entries must keep growing across the whole idle window.  The
# STM8S has no reset-cause register, so a false IWDG reset (the failure mode of
# the old Active-halt implementation) is only visible as the RAM counters
# restarting from zero.
E1=$(sleep_entries || true)
echo "  (soak ${SLEEP_SOAK_S}s with idle=1s; entries before=$E1)"
sleep "$SLEEP_SOAK_S"
E2=$(sleep_entries || true)
if [ -n "$E1" ] && [ -n "$E2" ] && [ "$E2" -gt "$E1" ]; then
    pass "12.6 soak: entries $E1 -> $E2 (no IWDG reset)"
else
    fail "12.6 soak" "(entries '$E1' -> '$E2', possible false IWDG reset)"
fi

# 12.7 Watchdog with sleep on: same 30 s position-stability check as stage 11.
mctl sleep on >/dev/null 2>&1
P2=$(get_pos focus)
sleep 30
P3=$(get_pos focus)
if [ -n "$P2" ] && [ "$P2" = "$P3" ]; then pass "12.7 sleep+WDT: position stable"; else fail "12.7 sleep+WDT reset detected" "($P2 -> $P3)"; fi

echo "=== Stage 13: Reset command ==="
reset_mcu
mctl speed focus 200 >/dev/null 2>&1
B12=$(get_pos focus)
mctl_once move focus 100 >/dev/null 2>&1
sleep 2
check_pos "13.1 Pos before reset" "$((B12 + 100))" focus

mctl reset >/dev/null 2>&1
sleep 1
check_contains "13.1 Controller alive after reset" "$FW_VERSION" mctl version

echo "=== SUMMARY ==="
echo "PASS: $PASS"
echo "FAIL: $FAIL"
echo "$RESULTS"
echo
echo "Manual checks still required (not automated):"
echo "  0.4 visual wiring, 1.5 LED blink, 5.x visual rotation,"
echo "  7.2/7.3 direction sanity, 8.x nFAULT stall, 9.2/9.3 power-cycle EEPROM."
exit "$FAIL"