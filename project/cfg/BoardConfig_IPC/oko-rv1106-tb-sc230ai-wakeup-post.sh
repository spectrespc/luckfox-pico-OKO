#!/bin/bash
#
# OKO post-build script for the SC230AI thunder-boot ("wakeup IPC") image.
#
# The vendor template (luckfox-rv1106-tb-spi_nand-post.sh) rewrites rcS to run
# fastboot_server + smart_door and prunes everything else from /oem.  This line
# runs the rkipc wakeup-ipc variant instead, so the pruning keeps rkipc and the
# media libraries it links against.

function remove_unused_apps()
{
	# Keep the media stack (rkipc + libs).  Drop only binaries that this image
	# does not start; deleting "/oem/usr/lib/*.so*" like the vendor script does
	# would leave rkipc without its shared libraries.
	local bin_dir="$RK_PROJECT_PACKAGE_ROOTFS_DIR/oem/usr/bin"

	if [ -d "$bin_dir" ]; then
		rm -f "$bin_dir"/smart_door
		rm -f "$bin_dir"/fastboot_server
		rm -f "$bin_dir"/sample_*
		rm -f "$bin_dir"/rk_mpi_*_test
		rm -f "$bin_dir"/mpi_enc_test
		rm -f "$bin_dir"/mpp_info_test
		rm -f "$bin_dir"/vpu_api_test
		rm -f "$bin_dir"/rgaImDemo
		rm -f "$bin_dir"/rkisp_demo
		rm -f "$bin_dir"/j2s4b_dev
		rm -f "$bin_dir"/simple_*
		rm -f "$bin_dir"/luckfox_*_test
		rm -f "$bin_dir"/rk_adc_test
		rm -f "$bin_dir"/rk_event_test
		rm -f "$bin_dir"/rk_gpio_test
		rm -f "$bin_dir"/rk_led_test
		rm -f "$bin_dir"/rk_pwm_test
		rm -f "$bin_dir"/rk_system_test
		rm -f "$bin_dir"/rk_time_test
		rm -f "$bin_dir"/rk_watchdog_test
		rm -f "$bin_dir"/rk_rve_sample_test
		rm -f "$bin_dir"/rkaiq_3A_server
		rm -f "$bin_dir"/rkaiq_tool_server
		rm -f "$bin_dir"/modetest
		rm -f "$bin_dir"/dumpsys
		rm -f "$bin_dir"/wpa_supplicant*
		rm -f "$bin_dir"/wpa_cli*
		rm -f "$bin_dir"/wifi_start.sh
		rm -f "$bin_dir"/rkwifi_server
		rm -f "$bin_dir"/dnsmasq
		rm -f "$bin_dir"/hostapd
		rm -f "$bin_dir"/iperf
		# OKO product applications are not part of this experimental rootfs.
		rm -f "$bin_dir"/divinus
		rm -f "$bin_dir"/oko-ec
		rm -f "$bin_dir"/oko-flash
		rm -f "$bin_dir"/oko-lunch.sh
		rm -f "$bin_dir"/oko-lunch-stop.sh
		rm -f "$bin_dir"/oko-motor
		rm -f "$bin_dir"/ipo
		rm -f "$bin_dir"/wg
		rm -f "$bin_dir"/awg
		rm -f "$bin_dir"/ipcweb-backend
		rm -f "$bin_dir"/fastboot_client
	fi

	# No Wi-Fi on this line: drop the vendor Wi-Fi modules and firmware.  They
	# are the largest single item in the vendor rootfs (~13 MB).
	local ko_dir="$RK_PROJECT_PACKAGE_ROOTFS_DIR/oem/usr/ko"
	if [ -d "$ko_dir" ]; then
		rm -rf "$ko_dir"/aic_fw "$ko_dir"/aic8800dc_fw
		rm -f "$ko_dir"/aic8800_bsp.ko
		rm -f "$ko_dir"/aic8800_btlpm.ko
		rm -f "$ko_dir"/aic8800_fdrv.ko
		rm -f "$ko_dir"/aic8800_netdrv.ko
		rm -f "$ko_dir"/aic_load_fw.ko
		rm -f "$ko_dir"/8188fu.ko
		rm -f "$ko_dir"/8189fs.ko
		rm -f "$ko_dir"/8821cu.ko
		rm -f "$ko_dir"/atbm603x_.ko
		rm -f "$ko_dir"/atbm6041_wifi_sdio.ko
		rm -f "$ko_dir"/ssv6115.ko
		rm -f "$ko_dir"/ssv6x5x.ko
		rm -f "$ko_dir"/r8723bs.ko
		rm -f "$ko_dir"/hichannel.ko
		rm -f "$ko_dir"/insmod_wifi.sh
	fi

	# Debug/UI payload of the product image, not used by rkipc.
	rm -rf "$RK_PROJECT_PACKAGE_ROOTFS_DIR/lib/libitm"*
	rm -f "$RK_PROJECT_PACKAGE_ROOTFS_DIR/lib/libstdc++.so.6.0.25-gdb.py"
	rm -f "$RK_PROJECT_PACKAGE_ROOTFS_DIR/bin/gdb"*
	rm -f "$RK_PROJECT_PACKAGE_ROOTFS_DIR/usr/bin/gdbserver"
	rm -f "$RK_PROJECT_PACKAGE_ROOTFS_DIR/usr/bin/adbd"

	# Vendor TB dev flow: preload rtthread/meta from the rootfs.  Our images
	# boot the MCU from idblock and load meta from the meta partition, so the
	# io(1) preload script is dead weight here.
	rm -f "$RK_PROJECT_PACKAGE_ROOTFS_DIR/oem/usr/share/resume_suspend_test.sh"
	rm -f "$RK_PROJECT_PACKAGE_ROOTFS_DIR/oem/usr/share/rtthread.bin"
	rm -f "$RK_PROJECT_PACKAGE_ROOTFS_DIR/oem/usr/share/meta.img"

	# rkipc links librockit.so; the full/tiny variants and the AVS generators
	# are not referenced by the wakeup-ipc variant.
	local lib_dir="$RK_PROJECT_PACKAGE_ROOTFS_DIR/oem/usr/lib"
	rm -f "$lib_dir"/librockit_full.so
	rm -f "$lib_dir"/librockit_tiny.so
	rm -f "$lib_dir"/librkAVS_genLut.so
	rm -f "$lib_dir"/librkAVS_genStitch.so
	rm -rf "$RK_PROJECT_PACKAGE_ROOTFS_DIR/oem/usr/share/divinus"

	# The MCU consumes the IQ bin from meta; the Linux rkaiq needs the json.
	# RK_CAMERA_SENSOR_IQFILES points at the bin (for meta), so install the
	# matching json next to it for rk_isp_init().
	local iq_dir="$RK_PROJECT_PACKAGE_ROOTFS_DIR/oem/usr/share/iqfiles"
	local iq_json="sc230ai_CMK-OT2115-PC1_30IRC-F16.json"

	mkdir -p "$iq_dir"
	if [ -f "$RK_PROJECT_PATH_MEDIA/isp_iqfiles/$iq_json" ]; then
		cp -f "$RK_PROJECT_PATH_MEDIA/isp_iqfiles/$iq_json" "$iq_dir/"
	fi
}

function strip_payload()
{
	# The vendor rootfs keeps debug_info in libstdc++/libgcc_s (16 MB
	# uncompressed).  The thunder-boot ramdisk only has 20 MB in the DDR map,
	# so strip every shipped ELF and drop the static archives.
	local strip_bin="${CROSS_COMPILE}strip"

	command -v "$strip_bin" >/dev/null 2>&1 || strip_bin=strip

	find "$RK_PROJECT_PACKAGE_ROOTFS_DIR/oem/usr/lib" -type f -name '*.a' -delete 2>/dev/null

	for dir in "$RK_PROJECT_PACKAGE_ROOTFS_DIR/oem/usr/bin" \
		"$RK_PROJECT_PACKAGE_ROOTFS_DIR/oem/usr/lib" \
		"$RK_PROJECT_PACKAGE_ROOTFS_DIR/lib" \
		"$RK_PROJECT_PACKAGE_ROOTFS_DIR/usr/lib" \
		"$RK_PROJECT_PACKAGE_ROOTFS_DIR/usr/bin" \
		"$RK_PROJECT_PACKAGE_ROOTFS_DIR/bin" \
		"$RK_PROJECT_PACKAGE_ROOTFS_DIR/sbin"; do
		[ -d "$dir" ] || continue
		find "$dir" -type f \( -name '*.so*' -o -perm -u+x \) \
			-exec sh -c '"$0" --strip-unneeded "$1" 2>/dev/null || true' \
			"$strip_bin" {} \;
	done
}

function rewrite_script()
{
	# export default rk_log_level for app
	echo "export rt_log_level=4" >>"$RK_PROJECT_PACKAGE_ROOTFS_DIR/etc/profile.d/RkEnv.sh"

	rm -f "$RK_PROJECT_PACKAGE_ROOTFS_DIR"/etc/init.d/S21appinit*
	rm -f "$RK_PROJECT_PACKAGE_ROOTFS_DIR"/etc/init.d/S20urandom*

	cat >"$RK_PROJECT_PACKAGE_ROOTFS_DIR/etc/init.d/rcS" <<'EOF'
#!/bin/sh
echo 256 > /proc/sys/kernel/threads-max

if [ -f "/etc/profile.d/RkEnv.sh" ]; then
	source /etc/profile.d/RkEnv.sh
fi

# userdata carries rkipc.ini; oem lives inside the boot erofs image
sh /etc/init.d/_S20linkmount start &>/dev/null &

echo "oko-wakeup: load media modules" > /dev/kmsg
if [ -f /oem/usr/ko/insmod_ko.sh ]; then
	cd /oem/usr/ko && sh insmod_ko.sh
	cd /
fi

ifconfig eth0 up
udhcpc -i eth0 -b -q &

cnt=0
while [ $cnt -lt 50 ]; do
	cnt=$((cnt + 1))
	mount | grep -w /userdata >/dev/null && break
	sleep .2
done

if [ ! -f /userdata/rkipc.ini ]; then
	cp -f /oem/usr/share/rkipc-300w.ini /userdata/rkipc.ini
fi

echo "oko-wakeup: start rkipc" > /dev/kmsg
rkipc -c /userdata/rkipc.ini -a /oem/usr/share/iqfiles &>/tmp/rkipc.log &
EOF
	chmod +x "$RK_PROJECT_PACKAGE_ROOTFS_DIR/etc/init.d/rcS"

	cat >"$RK_PROJECT_PACKAGE_ROOTFS_DIR/etc/init.d/rcK" <<'EOF'
#!/bin/sh
echo "oko-wakeup: stop" > /dev/kmsg
killall rkipc
sync
umount /userdata
EOF
	chmod +x "$RK_PROJECT_PACKAGE_ROOTFS_DIR/etc/init.d/rcK"

	cat >"$RK_PROJECT_PACKAGE_ROOTFS_DIR/etc/profile" <<'EOF'
export PATH="/bin:/sbin:/usr/bin:/usr/sbin"

if [ "$PS1" ]; then
	if [ "`id -u`" -eq 0 ]; then
		export PS1='[\u@\h \W]# '
	else
		export PS1='[\u@\h \W]$ '
	fi
fi

export PAGER='/bin/more '
export EDITOR='/bin/vi'

for i in /etc/profile.d/*.sh ; do
	if [ -r "$i" ]; then
		. $i
	fi
	unset i
done
EOF

	echo "luckfox" >"$RK_PROJECT_PACKAGE_ROOTFS_DIR/etc/hostname"
}

#=========================
# post
#=========================
remove_unused_apps
strip_payload
rewrite_script