ifeq ($(APP_PARAM), )
    APP_PARAM:=../Makefile.param
    include $(APP_PARAM)
endif

export LC_ALL=C
SHELL:=/bin/bash

CURRENT_DIR := $(shell pwd)

PKG_NAME := divinus
PKG_BIN ?= out

# Pinned upstream commit (see README.md for the full hash)
DIVINUS_REV := bca1932e

ifeq ($(PKG_BIN),)
$(error ### $(CURRENT_DIR): PKG_BIN is NULL, Please Check !!!)
endif

all: divinus-build
	@echo "build $(PKG_NAME) done"

# The upstream src/Makefile links ../divinus, i.e. $(CURRENT_DIR)/divinus.
# Vendor libraries (librockit.so, librkaiq.so) are dlopen()ed at runtime,
# so no -lrockit/-lrkaiq and no media include paths are needed here.
divinus-build:
	@echo "RK_APP_CHIP is $(RK_APP_CHIP)"
	@echo "RK_APP_CROSS is $(RK_APP_CROSS)"
	$(MAKE) -C source/src clean
	$(MAKE) -C source/src \
		CC="$(RK_APP_CROSS)-gcc" \
		OPT="-Os -s $(RK_APP_OPTS) $(RK_APP_CROSS_CFLAGS)" \
		GIT_REV=$(DIVINUS_REV)
	@test -f source/divinus || (echo "### $(PKG_NAME): source/divinus was not produced" && exit 1)
	rm -rf $(PKG_BIN)
	mkdir -p $(PKG_BIN)/usr/bin $(PKG_BIN)/usr/share/divinus
	cp -f source/divinus $(PKG_BIN)/usr/bin/divinus
	cp -f files/divinus.yaml $(PKG_BIN)/usr/share/divinus/divinus.yaml
	$(call MAROC_COPY_PKG_TO_APP_OUTPUT, $(RK_APP_OUTPUT), $(PKG_BIN))

clean:
	$(MAKE) -C source/src clean
	@rm -rf $(PKG_BIN)

distclean: clean

info:
	@echo "PKG_NAME=$(PKG_NAME)"
	@echo "PKG_BIN=$(PKG_BIN)"
	@echo "DIVINUS_REV=$(DIVINUS_REV)"