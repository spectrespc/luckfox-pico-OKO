
MAKEFILE_DIR := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
export LC_ALL=C
SHELL:=/bin/bash
MAKE=make
AT?=@

################################################################################
##   Platform Specified  Configuraton
################################################################################
include $(MAKEFILE_DIR)/Makefile.param

################################################################################
##   Public Configuraton
################################################################################

app_src := $(wildcard ./*/Makefile ./component/fastboot_server/Makefile)
app_src := $(dir $(app_src))
# OKO: the vendor rkipc is not built any more; the application launcher moved
# to project/app/oko-lunch.  The rkipc sources stay in the tree for reference
# and are built only for the experimental thunder-boot wakeup-IPC line
# (RK_APP_TYPE=RKIPC_RV1106_WAKEUP_IPC), which has no OKO launcher.
app_src := $(filter-out ./rkipc/,$(app_src))
ifneq (, $(filter RKIPC_RV1106_WAKEUP_IPC,$(RK_APP_TYPE)))
app_src += ./rkipc/
endif

all: $(pkg-build)
	$(foreach target,$(app_src),make -C $(target)||exit -1;)
	$(call MAROC_COPY_PKG_TO_APP_OUTPUT, $(RK_PROJECT_PATH_APP), $(RK_APP_OUTPUT))

distclean: clean

clean:
	$(foreach target,$(app_src),make clean -C $(target)||exit -1;)
	@rm -rf $(RK_APP_OUTPUT)

info:
	@$(foreach target,$(app_src),make -C $(target) info||exit 0;)
