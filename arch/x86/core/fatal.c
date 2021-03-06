/*
 * Copyright (c) 2013-2014 Wind River Systems, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @file
 * @brief Nanokernel fatal error handler
 *
 * This module provides the _NanoFatalErrorHandler() routine.
 */

#include <toolchain.h>
#include <sections.h>

#include <nanokernel.h>
#include <nano_private.h>
#include <misc/printk.h>
#include <asmPrv.h>
#include <drivers/loapic.h>

__weak void _debug_fatal_hook(const NANO_ESF *esf) { ARG_UNUSED(esf); }

/*
 * Define a default ESF for use with _NanoFatalErrorHandler() in the event
 * the caller does not have a NANO_ESF to pass
 */
const NANO_ESF _default_esf = {
	0xdeaddead, /* ESP */
	0xdeaddead, /* EBP */
	0xdeaddead, /* EBX */
	0xdeaddead, /* ESI */
	0xdeaddead, /* EDI */
	0xdeaddead, /* EDX */
	0xdeaddead, /* ECX */
	0xdeaddead, /* EAX */
	0xdeaddead, /* error code */
	0xdeaddead, /* EIP */
	0xdeaddead, /* CS */
	0xdeaddead, /* EFLAGS */
};

/**
 *
 * @brief Nanokernel fatal error handler
 *
 * This routine is called when a fatal error condition is detected by either
 * hardware or software.
 *
 * The caller is expected to always provide a usable ESF.  In the event that the
 * fatal error does not have a hardware generated ESF, the caller should either
 * create its own or use a pointer to the global default ESF <_default_esf>.
 *
 * @param reason the reason that the handler was called
 * @param pEsf pointer to the exception stack frame
 *
 * @return This function does not return.
 */
FUNC_NORETURN void _NanoFatalErrorHandler(unsigned int reason,
					  const NANO_ESF *pEsf)
{
	_debug_fatal_hook(pEsf);

#ifdef CONFIG_PRINTK

	/* Display diagnostic information about the error */

	switch (reason) {
	case _NANO_ERR_CPU_EXCEPTION:
		break;

	case _NANO_ERR_SPURIOUS_INT:
		printk("***** Unhandled interrupt vector %d occurred! "
		       "*****\n", _loapic_isr_vector_get());
		break;

	case _NANO_ERR_INVALID_TASK_EXIT:
		printk("***** Invalid Exit Software Error! *****\n");
		break;

#if defined(CONFIG_STACK_CANARIES)
	case _NANO_ERR_STACK_CHK_FAIL:
		printk("***** Stack Check Fail! *****\n");
		break;
#endif /* CONFIG_STACK_CANARIES */


	case _NANO_ERR_ALLOCATION_FAIL:
		printk("**** Kernel Allocation Failure! ****\n");
		break;

	default:
		printk("**** Unknown Fatal Error %d! ****\n", reason);
		break;
	}

	printk("Current thread ID = 0x%x\n"
	       "Faulting segment:address = 0x%x:0x%x\n"
	       "eax: 0x%x, ebx: 0x%x, ecx: 0x%x, edx: 0x%x\n"
	       "esi: 0x%x, edi: 0x%x, ebp: 0%x, esp: 0x%x\n"
	       "eflags: 0x%x\n",
	       sys_thread_self_get(),
	       pEsf->cs & 0xFFFF, pEsf->eip,
	       pEsf->eax, pEsf->ebx, pEsf->ecx, pEsf->edx,
	       pEsf->esi, pEsf->edi, pEsf->ebp, pEsf->esp,
	       pEsf->eflags);
#endif /* CONFIG_PRINTK */


	/*
	 * Error was fatal to a kernel task or a fiber; invoke the system
	 * fatal error handling policy defined for the platform.
	 */

	_SysFatalErrorHandler(reason, pEsf);
}

#if CONFIG_EXCEPTION_DEBUG

static FUNC_NORETURN void generic_exc_handle(unsigned int vector,
					     const NANO_ESF *pEsf)
{
	printk("***** CPU exception %d\n", vector);
	if ((1 << vector) & _EXC_ERROR_CODE_FAULTS) {
		printk("***** Exception code: 0x%x\n", pEsf->errorCode);
	}
	_NanoFatalErrorHandler(_NANO_ERR_CPU_EXCEPTION, pEsf);
}

#define EXC_FUNC(vector) \
FUNC_NORETURN void handle_exc_##vector(const NANO_ESF *pEsf) \
{ \
	generic_exc_handle(vector, pEsf); \
}

EXC_FUNC(IV_DIVIDE_ERROR);
EXC_FUNC(IV_NON_MASKABLE_INTERRUPT);
EXC_FUNC(IV_OVERFLOW);
EXC_FUNC(IV_BOUND_RANGE);
EXC_FUNC(IV_INVALID_OPCODE);
#ifndef CONFIG_FP_SHARING
EXC_FUNC(IV_DEVICE_NOT_AVAILABLE);
#endif
EXC_FUNC(IV_DOUBLE_FAULT);
EXC_FUNC(IV_INVALID_TSS);
EXC_FUNC(IV_SEGMENT_NOT_PRESENT);
EXC_FUNC(IV_STACK_FAULT);
EXC_FUNC(IV_GENERAL_PROTECTION);
EXC_FUNC(IV_PAGE_FAULT);
EXC_FUNC(IV_X87_FPU_FP_ERROR);
EXC_FUNC(IV_ALIGNMENT_CHECK);
EXC_FUNC(IV_MACHINE_CHECK);

#endif /* CONFIG_EXCEPTION_DEBUG */

