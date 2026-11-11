ifeq ($(APP_PARAM), )
    APP_PARAM:=../Makefile.param
    include $(APP_PARAM)
endif

export LC_ALL=C
SHELL:=/bin/bash

CURRENT_DIR := $(shell pwd)

PKG_NAME := oko-lunch
PKG_BIN ?= out
PKG_SCRIPTS := $(wildcard files/*.sh)

ifeq ($(PKG_BIN),)
$(error ### $(CURRENT_DIR): PKG_BIN is NULL, Please Check !!!)
endif

all: oko-lunch-build
	@echo "build $(PKG_NAME) done"

oko-lunch-build:
	@echo "PKG_NAME is $(PKG_NAME)"
	@test -n "$(PKG_SCRIPTS)" || (echo "### $(PKG_NAME): no scripts in files/" && exit 1)
	rm -rf $(PKG_BIN)
	mkdir -p $(PKG_BIN)/usr/bin
	cp -f $(PKG_SCRIPTS) $(PKG_BIN)/usr/bin/
	chmod a+x $(PKG_BIN)/usr/bin/*.sh
	$(call MAROC_COPY_PKG_TO_APP_OUTPUT, $(RK_APP_OUTPUT), $(PKG_BIN))

clean:
	@rm -rf $(PKG_BIN)

distclean: clean

info:
	@echo "PKG_NAME=$(PKG_NAME)"
	@echo "PKG_BIN=$(PKG_BIN)"
	@echo "PKG_SCRIPTS=$(PKG_SCRIPTS)"