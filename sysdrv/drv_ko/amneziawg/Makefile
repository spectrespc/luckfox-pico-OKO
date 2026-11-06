ifeq ($(SYSDRV_PARAM), )
	SYSDRV_PARAM:=../../Makefile.param
	include $(SYSDRV_PARAM)
endif

CURRENT_DIR := $(shell pwd)
M_OUT_DIR ?= ../out

MODULE_NAME := amneziawg
AMNEZIAWG_VERSION := 3.1.20260906

ifeq ($(ENABLE_AWG),y)
	build_target := modules
endif

all: $(build_target)
	@echo "build $(MODULE_NAME) done"

.PHONY: modules clean distclean

modules:
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) \
		-C $(KERNEL_DIR) M=$(CURRENT_DIR)/src \
		WIREGUARD_VERSION="$(AMNEZIAWG_VERSION)" modules
	cp $(CURRENT_DIR)/src/$(MODULE_NAME).ko $(M_OUT_DIR)
	@$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) \
		-C $(KERNEL_DIR) M=$(CURRENT_DIR)/src clean >/dev/null 2>&1 || true
	@rm -rf $(CURRENT_DIR)/src/*.o $(CURRENT_DIR)/src/*~ \
		$(CURRENT_DIR)/src/.*.cmd $(CURRENT_DIR)/src/*.mod.c \
		$(CURRENT_DIR)/src/.tmp_versions $(CURRENT_DIR)/src/*.symvers \
		$(CURRENT_DIR)/src/modules.order $(CURRENT_DIR)/src/*.mod

clean:
	@$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) \
		-C $(KERNEL_DIR) M=$(CURRENT_DIR)/src clean >/dev/null 2>&1 || true
	@rm -rf $(CURRENT_DIR)/src/*.o $(CURRENT_DIR)/src/*.ko \
		$(CURRENT_DIR)/src/*~ $(CURRENT_DIR)/src/.*.cmd \
		$(CURRENT_DIR)/src/*.mod.c $(CURRENT_DIR)/src/*.mod \
		$(CURRENT_DIR)/src/.tmp_versions $(CURRENT_DIR)/src/*.symvers \
		$(CURRENT_DIR)/src/modules.order

distclean: clean
