# Dual-mode Makefile.
#
# Standalone checkout (this is the STM8 firmware repository):
#   make            -> firmware via Makefile.stm8 (SDCC + STM8-SPL-SDCC)
#   make flash      -> ST-Link write of factory.ihx
#
# OKO SDK build (project/app/Makefile calls this file from project/app):
#   ../Makefile.param exists, so the SDK wrapper below cross-compiles only
#   the host utility oko-motor; the SDK builder image has no SDCC.
ifeq ($(wildcard ../Makefile.param),)
include Makefile.stm8
else
# SDK wrapper for project/app/stm8-motor.
#
# `project/app/Makefile` builds every subdirectory that has a Makefile, so this
# file must not invoke SDCC (the SDK builder image does not ship it).  It only
# cross-compiles the host utility oko-motor; the STM8 firmware is built with
# Makefile.stm8 inside tools/stm8-sdcc (see README.md).
ifeq ($(APP_PARAM), )
    APP_PARAM:=../Makefile.param
    include $(APP_PARAM)
endif

export LC_ALL=C
SHELL:=/bin/bash

CURRENT_DIR := $(shell pwd)

PKG_NAME := stm8-motor
PKG_BIN ?= out

CC = $(RK_APP_CROSS)-gcc
CFLAGS = -Os -s -std=c99 -Wall -Wextra -Wno-unused-function $(RK_APP_OPTS) $(RK_APP_CROSS_CFLAGS)
INCLUDES = -Isrc/common -Isrc/i2c -Iutil
SRCS = util/motorctl.c util/i2c_host.c src/common/oko_boot.c src/common/oko_crc.c

ifeq ($(PKG_BIN),)
$(error ### $(CURRENT_DIR): PKG_BIN is NULL, Please Check !!!)
endif

all: oko-motor-build
	@echo "build $(PKG_NAME) done"

oko-motor-build:
	@echo "RK_APP_CHIP is $(RK_APP_CHIP)"
	@echo "RK_APP_CROSS is $(RK_APP_CROSS)"
	@test -n "$(RK_APP_CROSS)" || (echo "### $(PKG_NAME): RK_APP_CROSS is empty" && exit 1)
	@test -f src/common/oko_boot.h || (echo "### $(PKG_NAME): sources missing" && exit 1)
	rm -rf $(PKG_BIN)
	mkdir -p $(PKG_BIN)/usr/bin
	$(CC) $(CFLAGS) $(INCLUDES) -o $(PKG_BIN)/usr/bin/oko-motor $(SRCS)
	$(call MAROC_COPY_PKG_TO_APP_OUTPUT, $(RK_APP_OUTPUT), $(PKG_BIN))

clean:
	@rm -rf $(PKG_BIN)

distclean: clean

info:
	@echo "PKG_NAME=$(PKG_NAME)"
	@echo "PKG_BIN=$(PKG_BIN)"
	@echo "CC=$(CC)"
	@echo "SRCS=$(SRCS)"

endif
