#!/bin/sh

rcS() {
	for i in /oem/usr/etc/init.d/S??*; do

		# Ignore dangling symlinks (if any).
		[ ! -f "$i" ] && continue

		case "$i" in
		*.sh)
			# Source shell script for speed.
			(
				trap - INT QUIT TSTP
				set start
				. $i
			)
			;;
		*)
			# No sh extension, so fork subprocess.
			$i start
			;;
		esac
	done
}

check_linker() {
	[ ! -L "$2" ] && ln -sf $1 $2
}

network_init() {
	if grep -q 'nfsroot=' /proc/cmdline 2>/dev/null; then
		# netboot: root fs and oem/userdata are mounted over NFS using the
		# static address from the kernel ip= parameter. Changing the MAC or
		# taking a DHCP lease would break the established NFS connections.
		ifconfig eth0 up
		echo "netboot: keep static eth0 config, skip dhcp"
		return 0
	fi

	# OKO keeps the MAC and DHCP/static settings in the U-Boot env partition,
	# so a full firmware update (which recreates /userdata) does not change the
	# DHCP identity.  Fall back to the vendor logic if the helper is missing.
	if [ -x /usr/bin/oko-net ]; then
		/usr/bin/oko-net apply
		return
	fi

	ethaddr1=$(ifconfig -a | grep "eth.*HWaddr" | awk '{print $5}')

	if [ -f /data/ethaddr.txt ]; then
		ethaddr2=$(cat /data/ethaddr.txt)
		if [ $ethaddr1 == $ethaddr2 ]; then
			echo "eth HWaddr cfg ok"
		else
			ifconfig eth0 down
			ifconfig eth0 hw ether $ethaddr2
		fi
	else
		echo $ethaddr1 >/data/ethaddr.txt
	fi
	ifconfig eth0 up && udhcpc -i eth0 >/dev/null 2>&1
}

post_chk() {
	#TODO: ensure /userdata mount done
	cnt=0
	while [ $cnt -lt 30 ]; do
		cnt=$((cnt + 1))
		if mount | grep -w userdata; then
			break
		fi
		sleep .1
	done

	# if ko exist, install ko first
	default_ko_dir=/ko
	if [ -f "/oem/usr/ko/insmod_ko.sh" ]; then
		default_ko_dir=/oem/usr/ko
	fi
	if [ -f "$default_ko_dir/insmod_ko.sh" ]; then
		cd $default_ko_dir && sh insmod_ko.sh && cd -
	fi

	network_init &
	check_linker /userdata /oem/usr/www/userdata
	check_linker /media/usb0 /oem/usr/www/usb0
	check_linker /mnt/sdcard /oem/usr/www/sdcard

	# divinus (OpenIPC) is the only streamer in the OKO image; rkipc and its
	# resources are pruned at pack time, so there is no fallback path here.
	if [ ! -f /userdata/divinus.enable ]; then
		echo "divinus: /userdata/divinus.enable is missing; no streamer started"
		return
	fi
	if [ ! -x /oem/usr/bin/divinus ]; then
		echo "divinus: /oem/usr/bin/divinus not found; no streamer started"
		return
	fi
	if [ ! -f /userdata/divinus.yaml ]; then
		cp -f /oem/usr/share/divinus/divinus.yaml /userdata/divinus.yaml
	fi
	# config is searched in the exe dir, then ./, then /etc; run from
	# /userdata so WebUI saves rewrite /userdata/divinus.yaml itself
	cd /userdata
	/oem/usr/bin/divinus &
}

rcS

ulimit -c unlimited
echo "/data/core-%p-%e" >/proc/sys/kernel/core_pattern
# echo 0 > /sys/devices/platform/rkcif-mipi-lvds/is_use_dummybuf

echo 1 >/proc/sys/vm/overcommit_memory

post_chk &
