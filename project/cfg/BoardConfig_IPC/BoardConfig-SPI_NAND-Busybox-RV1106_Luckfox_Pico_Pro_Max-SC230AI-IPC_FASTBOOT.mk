#!/bin/bash

#################################################
# 	Board Config
#################################################
# OKO experimental line: thunder-boot ("fastboot") image for the Luckfox
# Pico Pro Max with the SC230AI sensor.  Not the product image: it uses a
# busybox rootfs, no OEM/rootfs ubifs partitions and no OKO stack.
#
# Derived from BoardConfig-SPI_NAND-Busybox-RV1106_Luckfox_Pico_Pro_Max-IPC_FASTBOOT.mk
# (the vendor template is SC3338/EVB2 based).
export LF_ORIGIN_BOARD_CONFIG=BoardConfig-SPI_NAND-Busybox-RV1106_Luckfox_Pico_Pro_Max-IPC_FASTBOOT.mk

# Target CHIP
export RK_CHIP=rv1106

# app config
# rkipc wakeup-ipc variant: RTSP + rockit IVS motion detection plus the
# AP-side sleep policy in video/aov_sleep.c.
export RK_APP_TYPE="RKIPC_RV1106_WAKEUP_IPC"

# Config CMA size in environment
export RK_BOOTARGS_CMA_SIZE="32M"

# Kernel dts
export RK_KERNEL_DTS=rv1106g-luckfox-pico-pro-max-sc230ai-fastboot.dts

#################################################
#	BOOT_MEDIUM
#################################################

# Target boot medium
export RK_BOOT_MEDIUM=spi_nand

# Uboot defconfig fragment
export RK_UBOOT_DEFCONFIG_FRAGMENT=rk-sfc.config

export LF_ENABLE_SPI_NAND_FAST_BOOT=y

# config partition in environment
# RK_PARTITION_CMD_IN_ENV format:
#     <partdef>[,<partdef>]
#       <partdef> := <size>[@<offset>](part-name)
# Note:
#   If the first partition offset is not 0x0, it must be added. Otherwise, it needn't adding.
export RK_PARTITION_CMD_IN_ENV="256K(env),1M@256K(idblock),512K(uboot),2M(meta),32M(boot),20M(userdata)"

export RK_META_SIZE=393216

# config partition's filesystem type (squashfs is readonly)
# emmc:    squashfs/ext4
# nand:    squashfs/ubifs
# spi nor: squashfs/jffs2
# RK_PARTITION_FS_TYPE_CFG format:
#     AAAA:/BBBB/CCCC@ext4
#         AAAA ----------> partition name
#         /BBBB/CCCC ----> partition mount point
#         ext4 ----------> partition filesystem type
export RK_PARTITION_FS_TYPE_CFG=boot@IGNORE@erofs,userdata@/userdata@ubifs

#################################################
#	TARGET_ROOTFS
#################################################

# Target rootfs
export LF_TARGET_ROOTFS=busybox

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
export RK_UBOOT_DEFCONFIG=luckfox_rv1106_spi_nand_tb_defconfig

# MCU firmware packed as the Hpmcu loader image (idblock) by build_uboot().
# build.sh mcu info --> battery_ipc_demo_v10-SC230AI-ADC (ported for this board:
# i2c4 m2 sensor bus, address 0x32).
export RK_UBOOT_RKBIN_MCU_CFG=battery_ipc_demo_v10-SC230AI-ADC

# Kernel defconfig
export RK_KERNEL_DEFCONFIG=luckfox_rv1106_linux_tb_defconfig

# Kernel defconfig fragment:
#   luckfox_rv1106-tb.config   thunder-boot kernel (ISP/CIF/SFC built-in)
#   rv1106-smart-door.config   media stack: VENC, RGA, MPP, TB service, sensors
#   rv1106-nand.config         SPI NAND
#   rv1106-sc230ai.config      SC230AI sensor driver, other cameras off
#   rv1106-pmu-timeout.config  system suspend + periodic PMU timeout wake
#
# rv1106-pmu-timeout.config holds bare CONFIG_ lines only: the kernel merge
# mechanism (scripts/kconfig/merge_config.sh) mangles free-form comments.
# CONFIG_RV1106_HPMCU_FAST_WAKEUP is deliberately NOT enabled: the non-AOV MCU
# firmware parks after the thunder-boot handoff and cannot act as a wake proxy,
# so the AP sleeps until the PMU timeout, then runs one IVS MD window.
export RK_KERNEL_DEFCONFIG_FRAGMENT="luckfox_rv1106-tb.config rv1106-smart-door.config rv1106-nand.config rv1106-sc230ai.config rv1106-pmu-timeout.config"

# Recovery defconfig fragment
export RK_RECOVERY_KERNEL_DEFCONFIG_FRAGMENT="rv1106-tb.config rv1106-recovery.config"

# Config sensor IQ files
# RK_CAMERA_SENSOR_IQFILES format:
#     "iqfile1 iqfile2 iqfile3 ..."
# ./build.sh media and copy <SDK root dir>/output/out/media_out/isp_iqfiles/$RK_CAMERA_SENSOR_IQFILES
# The thunder-boot meta carries the IQ as a bin (rk_aiq bin mode), the
# product image uses the matching json.
export RK_CAMERA_SENSOR_IQFILES="sc230ai_CMK-OT2115-PC1_30IRC-F16.bin"

# Config sensor lens CAC calibrattion bin files
# No CAC calibration available for SC230AI in media/isp
export RK_CAMERA_SENSOR_CAC_BIN=""

# enable install app to oem partition
export RK_BUILD_APP_TO_OEM_PARTITION=n

# This line runs rkipc, which the product prune list (oko-prune.list) removes.
export RK_SKIP_OKO_PRUNE=y

# config AI-ISP model
export RK_AIISP_MODEL=NONE

# config AUDIO model
export RK_AUDIO_MODEL=NONE

# config NPU model
export RK_NPU_MODEL=NONE

# specify post.sh for delete/overlay files
export RK_POST_BUILD_SCRIPT=oko-rv1106-tb-sc230ai-wakeup-post.sh

# enable fastboot
export RK_ENABLE_FASTBOOT=y

# declare overlay directory
export RK_POST_OVERLAY=overlay-luckfox-fastboot

# enable tiny meta only support cmdline overlay
export RK_TINY_META=n

# fastboot rndis
export RK_ENABLE_RNDIS=n

# set camera features (meta app params; consumed by the MCU fast-AE and the
# media stack).  1920x1080 matches the SC230AI product mode.
export RK_META_PARAM=" \
 --rk_cam_mirror_flip=0 \
 --rk_cam_fps=25 \
 --rk_cam_hdr=0 \
 --rk_led_value=20 \
 --rk_night_mode=0 \
"