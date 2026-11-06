ifeq ($(APP_PARAM), )
    APP_PARAM:=../Makefile.param
    include $(APP_PARAM)
endif

export LC_ALL=C
SHELL:=/bin/bash

CURRENT_DIR := $(shell pwd)

PKG_NAME := vpn-tools
PKG_BIN ?= out

CC := $(RK_APP_CROSS)-gcc
AR := $(RK_APP_CROSS)-ar
STRIP := $(RK_APP_CROSS)-strip

WG_SRC := $(CURRENT_DIR)/vendor/wireguard-tools-1.0.20210914/src
AWG_SRC := $(CURRENT_DIR)/vendor/amneziawg-tools-3.1.20260812/src

# wg-quick is intentionally disabled: the OKO helper "oko-vpn" applies the
# configuration, so the shell hooks from an imported .conf must never run.
TOOLS_ARGS := PLATFORM=linux CC=$(CC) AR=$(AR) STRIP=$(STRIP) \
	WITH_SYSTEMDUNITS=no WITH_BASHCOMPLETION=no WITH_WGQUICK=no

ifeq ($(PKG_BIN),)
$(error ### $(CURRENT_DIR): PKG_BIN is NULL, Please Check !!!)
endif

all: vpn-tools-build
	@echo "build $(PKG_NAME) done"

vpn-tools-build:
	@echo "RK_APP_CROSS is $(RK_APP_CROSS)"
	@test -n "$(RK_APP_CROSS)" || (echo "### $(PKG_NAME): RK_APP_CROSS is empty" && exit 1)
	@test -f "$(WG_SRC)/Makefile" || (echo "### $(PKG_NAME): wireguard-tools sources are missing" && exit 1)
	@test -f "$(AWG_SRC)/Makefile" || (echo "### $(PKG_NAME): amneziawg-tools sources are missing" && exit 1)
	rm -rf $(PKG_BIN)
	mkdir -p $(PKG_BIN)/usr/bin
	$(MAKE) -C $(WG_SRC) clean >/dev/null 2>&1 || true
	$(MAKE) -C $(WG_SRC) $(TOOLS_ARGS) wg
	$(STRIP) -s $(WG_SRC)/wg -o $(PKG_BIN)/usr/bin/wg
	$(MAKE) -C $(WG_SRC) clean >/dev/null 2>&1 || true
	$(MAKE) -C $(AWG_SRC) clean >/dev/null 2>&1 || true
	$(MAKE) -C $(AWG_SRC) $(TOOLS_ARGS) wg
	$(STRIP) -s $(AWG_SRC)/wg -o $(PKG_BIN)/usr/bin/awg
	$(MAKE) -C $(AWG_SRC) clean >/dev/null 2>&1 || true
	chmod 0755 $(PKG_BIN)/usr/bin/wg $(PKG_BIN)/usr/bin/awg
	@$(CC) --version | head -n 1
	$(call MAROC_COPY_PKG_TO_APP_OUTPUT, $(RK_APP_OUTPUT), $(PKG_BIN))

clean:
	@rm -rf $(PKG_BIN)
	@test ! -f $(WG_SRC)/Makefile || $(MAKE) -C $(WG_SRC) clean >/dev/null 2>&1 || true
	@test ! -f $(AWG_SRC)/Makefile || $(MAKE) -C $(AWG_SRC) clean >/dev/null 2>&1 || true

distclean: clean

info:
	@echo "$(PKG_NAME): wg $(notdir $(WG_SRC)) + awg amneziawg-tools-3.1.20260812"
	@echo "  sources: $(WG_SRC)"
	@echo "  sources: $(AWG_SRC)"