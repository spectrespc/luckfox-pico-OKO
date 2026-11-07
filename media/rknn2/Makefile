ifeq ($(MEDIA_PARAM), )
    MEDIA_PARAM:=../Makefile.param
    include $(MEDIA_PARAM)
endif

export LC_ALL=C
SHELL:=/bin/bash

CURRENT_DIR := $(shell pwd)
PKG_BIN ?= out

ifeq ($(PKG_BIN),)
$(error ### $(CURRENT_DIR): PKG_BIN is NULL, Please Check !!!)
endif

# Optional second RKNN mini-runtime (2.x) for models converted by toolkit 2.x.
# The vendor file is re-stamped to SONAME librknnmrt2.so so it can coexist
# with the 1.6.0 librknnmrt.so that AIISP/rockiva are linked against.
all: rknn2-install
	$(call MAROC_COPY_PKG_TO_MEDIA_OUTPUT, $(RK_MEDIA_OUTPUT), $(PKG_BIN))
	@echo "build rknn2 done"

rknn2-install:
	@rm -rf $(PKG_BIN)
	@mkdir -p $(PKG_BIN)/lib
	cp -f lib/librknnmrt2.so $(PKG_BIN)/lib/

clean: distclean

distclean:
	-rm -rf $(PKG_BIN)