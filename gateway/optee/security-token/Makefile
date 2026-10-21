out-dir := $(CURDIR)/out

DEV_KIT_DIR ?= $(CURDIR)/../../plat
-include $(DEV_KIT_DIR)/../platform_config.mk
TA_DEV_KIT_DIR ?= $(DEV_KIT_DIR)/arm-plat-$(PLATFORM)/export-ta_arm32

export TA_DEV_KIT_DIR

CROSS_COMPILE_TA ?= $(CROSS_COMPILE)

.PHONY: all
ifneq ($(wildcard $(TA_DEV_KIT_DIR)/host_include/conf.mk),)
all: ta
else
all:
	$(q)echo "TA_DEV_KIT_DIR is not correctly defined" && false
endif

.PHONY: ta
ta:
	$(q)$(MAKE) -C ta CROSS_COMPILE="$(CROSS_COMPILE_TA)" \
			     --no-builtin-variables \
			     O=$(out-dir)/ta

.PHONY: clean
clean:
	-$(q)$(MAKE) -C ta O=$(out-dir)/ta $@
