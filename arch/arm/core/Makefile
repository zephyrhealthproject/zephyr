ccflags-y += -I$(srctree)/kernel/nanokernel/include
ccflags-y += -I$(srctree)/kernel/microkernel/include

asflags-y := ${ccflags-y}

obj-y = atomic.o exc_exit.o irq_init.o \
	fiber_abort.o swap.o \
	fault.o gdb_stub_irq_vector_table.o \
	irq_manage.o thread.o cpu_idle.o \
	fault_s.o gdb_stub.o isr_wrapper.o \
	fatal.o sys_fatal_error_handler.o

obj-$(CONFIG_CPLUSPLUS) += __aeabi_atexit.o
obj-$(CONFIG_MICROKERNEL) += task_abort.o
obj-$(CONFIG_IRQ_OFFLOAD) += irq_offload.o
obj-$(CONFIG_CPU_CORTEX_M) += cortex_m/
