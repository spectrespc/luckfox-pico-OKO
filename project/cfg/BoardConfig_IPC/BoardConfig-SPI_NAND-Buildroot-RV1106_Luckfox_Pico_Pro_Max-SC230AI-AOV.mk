#!/bin/bash

#################################################
# 	Board Config
#################################################
export LF_ORIGIN_BOARD_CONFIG=BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-SC230AI.mk
# OKO AOV profile: product image plus the AOV MCU in the idblock and the
# kernel HPMCU wake proxy.  All three runtime switches (sleep, PMU timer,
# AOV) stay OFF by default; the image is otherwise the product image.
# The MCU image is loaded by BootROM from the idblock (LOADER2=Hpmcu) and
# released by the kernel only when divinus writes aov_wakeproxy=1.
export RK_CHIP=rv1106

# app config
export RK_APP_TYPE=RKIPC_RV1106

# Config CMA size in environment
export RK_BOOTARGS_CMA_SIZE="66M"

# Kernel dts
export RK_KERNEL_DTS=rv1106g-luckfox-pico-pro-max-sc230ai-aov.dts

#################################################
#	BOOT_MEDIUM
#################################################

# Target boot medium: emmc/spi_nor/spi_nand
export RK_BOOT_MEDIUM=spi_nand

# Uboot defconfig fragment
export RK_UBOOT_DEFCONFIG_FRAGMENT=rk-sfc.config

# specify post.sh for delete/overlay files
# export RK_PRE_BUILD_OEM_SCRIPT=rv1103-spi_nor-post.sh

# config partition in environment
# RK_PARTITION_CMD_IN_ENV format:
#     <partdef>[,<partdef>]
#       <partdef> := <size>[@<offset>](part-name)
# Note:
#   If the first partition offset is not 0x0, it must be added. Otherwise, it needn't adding.
# The AOV idblock carries DDR init + usbplug + the 117 KB Hpmcu image +
# SPL: ~310 KB, so the product 256K idblock does not fit.  The AOV profile
# uses a 1M idblock (the same size the vendor thunder-boot profile uses);
# every following partition moves by 768K and the product profile keeps its
# original table.  rootfs gives up 1M (168M -> 167M); the total is 255.25MB
# and still fits the 256MB NAND.
export RK_PARTITION_CMD_IN_ENV="256K(env),1M@256K(idblock),512K(uboot),6M(boot),40M(oem),40M(userdata),167M(rootfs)"

# config partition's filesystem type (squashfs is readonly)
# emmc:    squashfs/ext4
# nand:    squashfs/ubifs
# spi nor: squashfs/jffs2
# RK_PARTITION_FS_TYPE_CFG format:
#     AAAA:/BBBB/CCCC@ext4
#         AAAA ----------> partition name
#         /BBBB/CCCC ----> partition mount point
#         ext4 ----------> partition filesystem type
export RK_PARTITION_FS_TYPE_CFG=rootfs@IGNORE@ubifs,oem@/oem@ubifs,userdata@/userdata@ubifs

# config filesystem compress (Just for squashfs or ubifs)
# squashfs: lz4/lzo/lzma/xz/gzip, default xz
# ubifs:    lzo/zlib, default lzo
# export RK_SQUASHFS_COMP=xz
# export RK_UBIFS_COMP=lzo

#################################################
#	TARGET_ROOTFS
#################################################

# Target rootfs : buildroot/busybox
export LF_TARGET_ROOTFS=buildroot

# Buildroot defconfig
export RK_BUILDROOT_DEFCONFIG=luckfox_pico_defconfig

#################################################
# 	Defconfig
#################################################

# Target arch
export RK_ARCH=arm

# Target Toolchain Cross Compile
export RK_TOOLCHAIN_CROSS=arm-rockchip830-linux-uclibcgnueabihf

#misc image
export RK_MISC=wipe_all-misc.img

# Uboot defconfig
export RK_UBOOT_DEFCONFIG=luckfox_rv1106_uboot_defconfig

# Kernel defconfig
export RK_KERNEL_DEFCONFIG=luckfox_rv1106_linux_defconfig

# Kernel config fragments: NFS/TFTP netboot + SC230AI sensor + USB host classes.
# The netboot fragment is deliberately part of the stock image so the NAND
# boot can be switched to an NFS root with `fw_setenv sys_bootargs` alone,
# without reflashing the kernel.
# The usb-host fragment enables HID / USB-net / Bluetooth built-ins that the
# shared vendor defconfig disables; the DTS runs the port in dual-role "otg"
# mode, so these classes are only reachable through a hub in host role.
# The pmu-timeout fragment enables CONFIG_SUSPEND plus the RV1106 PMU wakeup
# timer, which the divinus sleep policy ([sleep] section) uses for the
# idle camera sleep: the AP suspends and the PMU timer wakes it periodically.
export RK_KERNEL_DEFCONFIG_FRAGMENT="rv1106-netboot.config rv1106-sc230ai.config rv1106-usb-host.config rv1106-pmu-timeout.config rv1106-hpmcu-aov.config"

# The HPMCU image lives at 0x40000..0x7c000, so the -AOV kernel is linked at
# 0x208000 (arch/arm/Makefile, CONFIG_RV1106_HPMCU_FAST_WAKEUP) instead of
# the product 0x8000.  boot.img is therefore rebuilt by build.sh from
# Image.gz with boot-aov.its: the FIT loads the kernel at 0x208000
# (compression = "gzip", load = entry = 0x208000) and U-Boot decompresses it
# in place, the same way the vendor thunder-boot image does.  The stock
# zImage FIT keeps loading at 0x8000 and is left untouched for the product
# profile.
export RK_KERNEL_BOOT_ITS=boot-aov.its
export RK_KERNEL_BOOT_IMAGE=Image.gz

# Config sensor IQ files
# RK_CAMERA_SENSOR_IQFILES format:
#     "iqfile1 iqfile2 iqfile3 ..."
# ./build.sh media and copy <SDK root dir>/output/out/media_out/isp_iqfiles/$RK_CAMERA_SENSOR_IQFILES
export RK_CAMERA_SENSOR_IQFILES="sc230ai_CMK-OT2115-PC1_30IRC-F16.json"

# AIISP model matching the native 1920x1080 sensor mode; build.sh copies only
# this model to /oem/usr/lib and removes the other rkmodel_*.aiisp files.
export RK_AIISP_MODEL=rkmodel_1080_1920.aiisp

# Config sensor lens CAC calibrattion bin files
# No CAC calibration available for SC230AI in media/isp
export RK_CAMERA_SENSOR_CAC_BIN=""

# build ipc web backend
#export RK_APP_IPCWEB_BACKEND=y

# enable install app to oem partition
export RK_BUILD_APP_TO_OEM_PARTITION=y

#################################################
#	AOV MCU (idblock LOADER2)
#################################################

# build_mcu() compiles this board and build_uboot() injects its rtthread.bin
# into the idblock through the overlay ini.  This path does not require
# RK_ENABLE_FASTBOOT: the ini keeps the product SPL as LOADER3, so NAND boot
# and the partition table stay exactly as in the product image.
export RK_UBOOT_RKBIN_MCU_CFG=oko-wakeup-sc230ai-ADC
export RK_UBOOT_RKBIN_INI_OVERLAY=RKBOOT/RV1106MINIALL_SPI_NAND_HPMCU.ini

# Meta blob (IQ + app/sensor parameters) for the MCU.  There is no room for a
# separate 2M meta partition in the 256MB NAND layout, so the blob is shipped
# in OEM and copied to the reserved 0x1000000 region by divinus before the
# first AOV suspend (see the AOV DTS).  The AP keeps the JSON IQ; the MCU
# needs the bin form.
export RK_META_SIZE=393216
export RK_META_CAMERA_SENSOR_IQFILES="sc230ai_CMK-OT2115-PC1_30IRC-F16.bin"
export RK_META_PARAM=" \
 --meta_load_addr=0x01000000 \
 --rk_cam_mirror_flip=0 \
 --rk_cam_fps=25 \
 --rk_cam_hdr=0 \
 --rk_led_value=20 \
 --rk_night_mode=0 \
 --no-compress-iq \
"

# enable rockchip test
export RK_ENABLE_ROCKCHIP_TEST=y

# enable rockchip wifi
export RK_ENABLE_WIFI=y

# enable AmneziaWG kernel module (in-tree WireGuard stays built in)
export RK_ENABLE_AWG=y

# vendor wpa_supplicant template; OKO Wi-Fi is configured at runtime
# through /usr/bin/oko-wifi and oko_wlan_* in the U-Boot env.
export LF_WIFI_SSID="Your wifi ssid"
export LF_WIFI_PSK="Your wifi password"

#################################################
# 	PRE and POST
#################################################

# specify pre.sh for delete/overlay files
export RK_PRE_BUILD_OEM_SCRIPT=luckfox-buildroot-oem-pre.sh

# specify post.sh for delete/overlay files
export RK_PRE_BUILD_USERDATA_SCRIPT=luckfox-userdata-pre.sh

# declare overlay directory
export RK_POST_OVERLAY="overlay-luckfox-config overlay-luckfox-buildroot-init overlay-luckfox-buildroot-shadow overlay-luckfox-wifibt-firmware"