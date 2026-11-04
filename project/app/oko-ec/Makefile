ifeq ($(APP_PARAM), )
    APP_PARAM:=../Makefile.param
    include $(APP_PARAM)
endif

export LC_ALL=C
SHELL:=/bin/bash

CURRENT_DIR := $(shell pwd)

PKG_NAME := oko-ec
PKG_BIN ?= out

CC = $(RK_APP_CROSS)-gcc
CFLAGS = -Os -s -std=c99 -Wall -Wextra $(RK_APP_OPTS) $(RK_APP_CROSS_CFLAGS)
SRCS := $(wildcard source/*.c)

ifeq ($(PKG_BIN),)
$(error ### $(CURRENT_DIR): PKG_BIN is NULL, Please Check !!!)
endif

all: oko-ec-build
	@echo "build $(PKG_NAME) done"

oko-ec-build:
	@echo "RK_APP_CHIP is $(RK_APP_CHIP)"
	@echo "RK_APP_CROSS is $(RK_APP_CROSS)"
	@test -n "$(RK_APP_CROSS)" || (echo "### $(PKG_NAME): RK_APP_CROSS is empty" && exit 1)
	@test -n "$(SRCS)" || (echo "### $(PKG_NAME): no sources" && exit 1)
	rm -rf $(PKG_BIN)
	mkdir -p $(PKG_BIN)/usr/bin
	$(CC) $(CFLAGS) -Isource -o $(PKG_BIN)/usr/bin/oko-ec $(SRCS)
	$(call MAROC_COPY_PKG_TO_APP_OUTPUT, $(RK_APP_OUTPUT), $(PKG_BIN))

clean:
	@rm -rf $(PKG_BIN)

distclean: clean

info:
	@echo "PKG_NAME=$(PKG_NAME)"
	@echo "PKG_BIN=$(PKG_BIN)"
	@echo "CC=$(CC)"
	@echo "SRCS=$(SRCS)"