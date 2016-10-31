BOARD ?= arduino_101_factory
MDEF_FILE = prj.mdef
CONF_FILE = prj_$(ARCH).conf

ifeq ($(ARCH), arc)
	KERNEL_TYPE = nano
else
	KERNEL_TYPE = micro
endif

include $(ZEPHYR_BASE)/Makefile.inc
