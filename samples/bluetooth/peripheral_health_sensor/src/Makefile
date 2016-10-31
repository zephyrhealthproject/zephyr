ccflags-y +=-I${ZEPHYR_BASE}/samples/bluetooth \
			-I$(ZEPHYR_BASE)/include/drivers \
			-I$(ZEPHYR_BASE)/drivers

ifeq ($(ARCH), arc)
	obj-y = main_arc.o heartrate.o
else
	obj-y = main_x86.o ../../gatt/gap.o ../../gatt/hrs.o ../../gatt/dis.o \
		../../gatt/bas.o ../../gatt/hts.o ../../gatt/spo2.o
endif