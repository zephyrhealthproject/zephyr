.. _nanokernel_interrupts:

Interrupt Services
##################

Concepts
********

:abbr:`ISRs (Interrupt Service Routines)` are execution threads
that run in response to a hardware or software interrupt.
They are used to preempt the execution of the
task or fiber running at the time of the interrupt,
allowing the response to occur with very low overhead.
When an ISR completes its normal task and fiber execution resumes.

Any number of ISRs can be utilized in a Zephyr project, subject to
any hardware constraints imposed by the underlying hardware.
Each ISR has the following properties:

* The :abbr:`IRQ (Interrupt ReQuest)` signal that triggers the ISR.
* The priority level associated with the IRQ.
* The address of the function that is invoked to handle the interrupt.
* The argument value that is passed to that function.

An :abbr:`IDT (Interrupt Descriptor Table)` is used to associate a given interrupt
source with a given ISR.
Only a single ISR can be associated with a specific IRQ at any given time.

Multiple ISRs can utilize the same function to process interrupts,
allowing a single function to service a device that generates
multiple types of interrupts or to service multiple devices
(usually of the same type). The argument value passed to an ISR's function
can be used to allow the function to determine which interrupt has been
signaled.

The Zephyr kernel provides a default ISR for all unused IDT entries. This ISR
generates a fatal system error if an unexpected interrupt is signaled.

The kernel supports interrupt nesting. This allows an ISR to be preempted
in mid-execution if a higher priority interrupt is signaled. The lower
priority ISR resumes execution once the higher priority ISR has completed
its processing.

The kernel allows a task or fiber to temporarily lock out the execution
of ISRs, either individually or collectively, should the need arise.
The collective lock can be applied repeatedly; that is, the lock can
be applied when it is already in effect. The collective lock must be
unlocked an equal number of times before interrupts are again processed
by the kernel.

Purpose
*******

Use an ISR to perform interrupt processing that requires a very rapid
response, and which can be done quickly and without blocking.

.. note::

   Interrupt processing that is time consuming, or which involves blocking,
   should be handed off to a fiber or task. See `Offloading ISR Work`_ for
   a description of various techniques that can be used in a Zephyr project.

Installing an ISR
*****************

It's important to note that IRQ_CONNECT() is not a C function and does
some inline assembly magic behind the scenes. All its arguments must be known
at build time. Drivers that have multiple instances may need to define
per-instance config functions to configure the interrupt for that instance.

Example
-------

.. code-block:: c

   #define MY_DEV_IRQ  24       /* device uses IRQ 24 */
   #define MY_DEV_PRIO  2       /* device uses interrupt priority 2 */
   /* argument passed to my_isr(), in this case a pointer to the device */
   #define MY_ISR_ARG  DEVICE_GET(my_device)
   #define MY_IRQ_FLAGS 0       /* IRQ flags. Unused on non-x86 */

   void my_isr(void *arg)
   {
      ... /* ISR code */
   }

   void my_isr_installer(void)
   {
      ...
      IRQ_CONNECT(MY_DEV_IRQ, MY_DEV_PRIO, my_isr, MY_ISR_ARG, MY_IRQ_FLAGS);
      irq_enable(MY_DEV_IRQ);            /* enable IRQ */
      ...
   }


Working with Interrupts
***********************

Use the following:

* `Offloading ISR Work`_
* `IDT Security`_

Offloading ISR Work
===================

Interrupt service routines should generally be kept short
to ensure predictable system operation.
In situations where time consuming processing is required
an ISR can quickly restore the kernel's ability to respond
to other interrupts by offloading some or all of the interrupt-related
processing work to a fiber or task.

Zephyr OS provides a variety of mechanisms to allow an ISR to offload work
to a fiber or task.

1. An ISR can signal a helper fiber (or task) to do interrupt-related work
   using a nanokernel object, such as a FIFO, LIFO, or semaphore.
   The :c:func:`nano_isr_XXX()` APIs should be used to notify the helper fiber
   (or task) that work is available for it.

   See :ref:`fiber_services`.

2. An ISR can signal the microkernel server fiber to do interrupt-related
   work by sending an event that has an associated event handler.

   See :ref:`microkernel_events`.

3. An ISR can signal a helper task to do interrupt-related work
   by sending an event that the helper task detects.

   See :ref:`microkernel_events`.

4. An ISR can signal a helper task to do interrupt-related work.
   by giving a semaphore that the helper task takes.

   See :ref:`microkernel_semaphores`.

5. A kernel-supplied ISR can signal a helper task to do interrupt-related work
   using a task IRQ that the helper task allocates.

   See :ref:`microkernel_task_irqs`.

When an ISR offloads work to a fiber there is typically a single
context switch to that fiber when the ISR completes.
Thus, interrupt-related processing usually continues almost immediately.
Additional intermediate context switches may be required
to execute any currently executing fiber
or any higher-priority fibers that are scheduled to run.

When an ISR offloads work to a task there is typically a context switch
to the microkernel server fiber, followed by a context switch to that task.
Thus, there is usually a larger delay before the interrupt-related processing
resumes than when offloading work to a fiber.
Additional intermediate context switches may be required
to execute any currently executing fiber or any higher-priority tasks
that are scheduled to run.

APIs
****

These are the interrupt-related Application Program Interfaces.

:c:func:`irq_enable()`
   Enables interrupts from a specific IRQ.

:c:func:`irq_disable()`
   Disables interrupts from a specific IRQ.

:c:func:`irq_lock()`
   Locks out interrupts from all sources.

:c:func:`irq_unlock()`
   Removes lock on interrupts from all sources.

Macros
******

These are the macros used to install a static ISR.

:c:macro:`IRQ_CONNECT()`
   Registers a static ISR with the IDT.

