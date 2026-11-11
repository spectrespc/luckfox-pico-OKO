#!/bin/sh
# OKO application stop script (replaces the vendor RkLunch-stop.sh).
#
# Called from /etc/init.d/S21appinit stop:
#   sh /oem/usr/bin/oko-lunch-stop.sh

echo "oko-lunch: stopping application"

# Stop a hung background launcher first, so it cannot (re)start anything.
# The pid file is written by oko-lunch.sh; the /proc cmdline check keeps a
# recycled pid from killing an unrelated process.
if [ -f /var/run/oko-lunch.pid ]; then
	lunch_pid=$(cat /var/run/oko-lunch.pid)
	if [ -n "$lunch_pid" ] &&
		grep -q 'oko-lunch\.sh' "/proc/$lunch_pid/cmdline" 2>/dev/null; then
		kill "$lunch_pid" 2>/dev/null
	fi
	rm -f /var/run/oko-lunch.pid
fi

killall divinus 2>/dev/null
killall udhcpc 2>/dev/null
killall dhcpcd 2>/dev/null

# Wait up to 10 s for divinus to exit instead of looping forever.
cnt=0
while [ $cnt -lt 10 ]; do
	if ! pidof divinus >/dev/null 2>&1; then
		echo "divinus exit"
		break
	fi
	cnt=$((cnt + 1))
	sleep 1
done

if pidof divinus >/dev/null 2>&1; then
	echo "divinus did not exit in 10 s, sending SIGKILL"
	killall -9 divinus 2>/dev/null
fi

# oko-net apply may still be finishing after the launcher was killed; make
# sure it cannot bring the network back up after this stop.
killall udhcpc 2>/dev/null
killall dhcpcd 2>/dev/null

echo "oko-lunch: application stopped"