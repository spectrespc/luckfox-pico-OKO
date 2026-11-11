#!/bin/sh
# OKO application launcher (replaces the vendor RkLunch.sh).
#
# Called from /etc/init.d/S21appinit start:
#   sh /oem/usr/bin/oko-lunch.sh
#
# Loads the kernel modules, brings up the network and starts the only
# streamer of the image (divinus).  The heavy work runs in the background
# (post_chk), so the init sequence does not wait for it.

check_linker() {
	[ ! -L "$2" ] && ln -sf "$1" "$2"
}

network_init() {
	if grep -q 'nfsroot=' /proc/cmdline 2>/dev/null; then
		# netboot: root fs and oem/userdata are mounted over NFS using the
		# static address from the kernel ip= parameter.  Changing the MAC or
		# taking a DHCP lease would break the established NFS connections.
		ifconfig eth0 up
		echo "netboot: keep static eth0 config, skip dhcp"
		return 0
	fi

	# OKO keeps the MAC and the DHCP/static settings in the U-Boot env
	# partition, so a full firmware update (which recreates /userdata) does not
	# change the DHCP identity.  /usr/bin/oko-net comes from the OKO rootfs
	# overlay and is a hard requirement: without it the network stays down.
	if [ -x /usr/bin/oko-net ]; then
		/usr/bin/oko-net apply
		return 0
	fi

	echo "oko-lunch: /usr/bin/oko-net is missing; network is NOT configured"
	return 1
}

post_chk() {
	# /userdata is mounted by S20linkmount; still wait up to ~3 s for it.
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
		(cd "$default_ko_dir" && sh insmod_ko.sh)
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

# Kernel core dumps are configured by /etc/profile.d/enable_coredump.sh
# (generated in project/build.sh); this launcher does not duplicate them.

echo 1 >/proc/sys/vm/overcommit_memory

post_chk &
echo $! >/var/run/oko-lunch.pid

echo "oko-lunch: application start queued (post_chk pid $!)"