/**
 * $Id$
 * Copyright (C) 2008 - 2009 Nils Asmussen
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <sys/common.h>
#include <sys/task/signals.h>
#include <sys/task/timer.h>
#include <sys/task/uenv.h>
#include <sys/mem/paging.h>
#include <sys/mem/vmm.h>
#include <sys/dbg/kb.h>
#include <sys/dbg/console.h>
#include <esc/keycodes.h>
#include <sys/cpu.h>
#include <sys/syscalls.h>
#include <sys/intrpt.h>
#include <sys/video.h>
#include <sys/util.h>

#define DEBUG_PAGEFAULTS	0

#define PSW_PUM				0x02000000	/* previous value of PSW_UM */

/* interrupt- and exception-numbers */
#define IRQ_TTY0_XMTR		0
#define IRQ_TTY0_RCVR		1
#define IRQ_TTY1_XMTR		2
#define IRQ_TTY1_RCVR		3
#define IRQ_KEYBOARD		4
#define IRQ_DISK			8
#define IRQ_TIMER			14
#define EXC_BUS_TIMEOUT		16
#define EXC_ILL_INSTRCT		17
#define EXC_PRV_INSTRCT		18
#define EXC_DIVIDE			19
#define EXC_TRAP			20
#define EXC_TLB_MISS		21
#define EXC_TLB_WRITE		22
#define EXC_TLB_INVALID		23
#define EXC_ILL_ADDRESS		24
#define EXC_PRV_ADDRESS		25

#define KEYBOARD_BASE		0xF0200000
#define KEYBOARD_CTRL		0
#define KEYBOARD_IEN		0x02

#define DISK_BASE			0xF0400000
#define DISK_CTRL			0
#define DISK_IEN			0x02

typedef void (*fIntrptHandler)(sIntrptStackFrame *stack);
typedef struct {
	fIntrptHandler handler;
	const char *name;
	tSig signal;
} sInterrupt;

static void intrpt_defHandler(sIntrptStackFrame *stack);
static void intrpt_exTrap(sIntrptStackFrame *stack);
static void intrpt_exPageFault(sIntrptStackFrame *stack);
static void intrpt_irqTimer(sIntrptStackFrame *stack);
static void intrpt_irqKB(sIntrptStackFrame *stack);
static void intrpt_irqDisk(sIntrptStackFrame *stack);

static sInterrupt intrptList[] = {
	/* 0x00: IRQ_TTY0_XMTR */	{intrpt_defHandler,	"Terminal 0 Transmitter",0},
	/* 0x01: IRQ_TTY0_RCVR */	{intrpt_defHandler,	"Terminal 0 Receiver",	0},
	/* 0x02: IRQ_TTY1_XMTR */	{intrpt_defHandler,	"Terminal 1 Transmitter",0},
	/* 0x03: IRQ_TTY1_RCVR */	{intrpt_defHandler,	"Terminal 1 Receiver",	0},
	/* 0x04: IRQ_KEYBOARD */	{intrpt_irqKB,		"Keyboard",				SIG_INTRPT_KB},
	/* 0x05: -- */				{intrpt_defHandler,	"??",					0},
	/* 0x06: -- */				{intrpt_defHandler,	"??",					0},
	/* 0x07: -- */				{intrpt_defHandler,	"??",					0},
	/* 0x08: IRQ_DISK */		{intrpt_irqDisk,	"Disk",					SIG_INTRPT_ATA1},
	/* 0x09: -- */				{intrpt_defHandler,	"??",					0},
	/* 0x0A: - */				{intrpt_defHandler,	"??",					0},
	/* 0x0B: -- */				{intrpt_defHandler,	"??",					0},
	/* 0x0C: -- */				{intrpt_defHandler,	"??",					0},
	/* 0x0D: -- */				{intrpt_defHandler,	"??",					0},
	/* 0x0E: IRQ_TIMER */		{intrpt_irqTimer,	"Timer",				SIG_INTRPT_TIMER},
	/* 0x0F: -- */				{intrpt_defHandler,	"??",					0},
	/* 0x10: EXC_BUS_TIMEOUT */	{intrpt_defHandler,	"Bus timeout exception",0},
	/* 0x11: EXC_ILL_INSTRCT */	{intrpt_defHandler,	"Ill. instr. exception",0},
	/* 0x12: EXC_PRV_INSTRCT */	{intrpt_defHandler,	"Prv. instr. exception",0},
	/* 0x13: EXC_DIVIDE */		{intrpt_defHandler,	"Divide exception",		0},
	/* 0x14: EXC_TRAP */		{intrpt_exTrap,		"Trap exception",		0},
	/* 0x15: EXC_TLB_MISS */	{intrpt_defHandler,	"TLB miss exception",	0},
	/* 0x16: EXC_TLB_WRITE */	{intrpt_exPageFault,"TLB write exception",	0},
	/* 0x17: EXC_TLB_INVALID */	{intrpt_exPageFault,"TLB invalid exception",0},
	/* 0x18: EXC_ILL_ADDRESS */	{intrpt_defHandler,	"Ill. addr. exception",	0},
	/* 0x19: EXC_PRV_ADDRESS */	{intrpt_defHandler,	"Prv. addr. exception",	0},
	/* 0x1A: -- */				{intrpt_defHandler,	"??",					0},
	/* 0x1B: -- */				{intrpt_defHandler,	"??",					0},
	/* 0x1C: -- */				{intrpt_defHandler,	"??",					0},
	/* 0x1D: -- */				{intrpt_defHandler,	"??",					0},
	/* 0x1E: -- */				{intrpt_defHandler,	"??",					0},
	/* 0x1F: -- */				{intrpt_defHandler,	"??",					0},
};
static size_t irqCount = 0;
static sIntrptStackFrame *curFrame;

void intrpt_handler(sIntrptStackFrame *stack) {
	sInterrupt *intrpt = intrptList + (stack->irqNo & 0x1F);
	curFrame = stack;
	irqCount++;

	/* call handler */
	intrpt->handler(stack);

	/* only handle signals, if we come directly from user-mode */
	/* note: we might get a kernel-miss at arbitrary places in the kernel; if we checked for
	 * signals in that case, we might cause a thread-switch. this is not always possible! */
	sThread *t = thread_getRunning();
	if(t != NULL && (t->tid == IDLE_TID || (stack->psw & PSW_PUM))) {
		uenv_handleSignal();
		if(t->tid != IDLE_TID && uenv_hasSignalToStart())
			uenv_startSignalHandler(stack);
	}
}

size_t intrpt_getCount(void) {
	return irqCount;
}

sIntrptStackFrame *intrpt_getCurStack(void) {
	return KERNEL_STACK + PAGE_SIZE - 4 - sizeof(sIntrptStackFrame);
}

static void intrpt_defHandler(sIntrptStackFrame *stack) {
	/* do nothing */
	util_panic("Got interrupt %d (%s) @ 0x%08x\n",
			stack->irqNo,intrptList[stack->irqNo & 0x1f].name,stack->r[30]);
}

static void intrpt_exTrap(sIntrptStackFrame *stack) {
	sThread *t = thread_getRunning();
	t->stats.syscalls++;
	sysc_handle(stack);
	/* skip trap-instruction */
	stack->r[30] += 4;
}

static void intrpt_exPageFault(sIntrptStackFrame *stack) {
	/* note: we have to make sure that the area at 0x80000000 is not used previously, because
	 * it might change the tlb-bad-address */
	uintptr_t pfaddr = cpu_getBadAddr();
	/* for exceptions in kernel: ensure that we have the default print-function */
	if(stack->r[30] >= DIR_MAPPED_SPACE)
		vid_unsetPrintFunc();

#if DEBUG_PAGEFAULTS
	if(pfaddr == lastPFAddr && lastPFProc == proc_getRunning()->pid) {
		exCount++;
		if(exCount >= MAX_EX_COUNT)
			util_panic("%d page-faults at the same address of the same process",exCount);
	}
	else
		exCount = 0;
	lastPFAddr = pfaddr;
	lastPFProc = proc_getRunning()->pid;
	vid_printf("Page fault for address=0x%08x @ 0x%x, process %d\n",pfaddr,
			stack->r[30],proc_getRunning()->pid);
#endif

	/* first let the vmm try to handle the page-fault (demand-loading, cow, swapping, ...) */
	if(!vmm_pagefault(pfaddr)) {
		/* ok, now lets check if the thread wants more stack-pages */
		if(thread_extendStack(pfaddr) < 0) {
			vid_printf("Page fault for address=0x%08x @ 0x%x, process %d\n",pfaddr,
								stack->r[30],proc_getRunning()->pid);
			/*proc_terminate(t->proc);
			thread_switch();*/
			/* hm...there is something wrong :) */
			/* TODO later the process should be killed here */
			util_panic("Page fault for address=0x%08x @ 0x%x",pfaddr,stack->r[30]);
		}
	}
}

static void intrpt_irqTimer(sIntrptStackFrame *stack) {
	UNUSED(stack);
	sig_addSignal(SIG_INTRPT_TIMER);
	timer_intrpt();
	timer_ackIntrpt();
}

static void intrpt_irqKB(sIntrptStackFrame *stack) {
	/* we have to disable interrupts until the device has handled the request */
	/* otherwise we would get into an interrupt loop */
	uint32_t *kbRegs = (uint32_t*)KEYBOARD_BASE;
	kbRegs[KEYBOARD_CTRL] &= ~KEYBOARD_IEN;

#if DEBUGGING
	if(proc_getByPid(KEYBOARD_PID) == NULL) {
		/* in debug-mode, start the logviewer when the keyboard is not present yet */
		/* (with a present keyboard-driver we would steal him the scancodes) */
		/* this way, we can debug the system in the startup-phase without affecting timings
		 * (before viewing the log ;)) */
		sKeyEvent ev;
		if(kb_get(&ev,KEV_PRESS,false) && ev.keycode == VK_F12)
			cons_start();
	}
#endif

	/* we can't add the signal before the kb-interrupts are disabled; otherwise a kernel-miss might
	 * call uenv_handleSignal(), which might cause a thread-switch */
	if(!sig_addSignal(SIG_INTRPT_KB)) {
		/* if there is no driver that handles the signal, reenable interrupts */
		kbRegs[KEYBOARD_CTRL] |= KEYBOARD_IEN;
	}
}

static void intrpt_irqDisk(sIntrptStackFrame *stack) {
	/* see interrupt_irqKb() */
	uint32_t *diskRegs = (uint32_t*)DISK_BASE;
	diskRegs[DISK_CTRL] &= ~DISK_IEN;
	if(!sig_addSignal(SIG_INTRPT_ATA1))
		diskRegs[DISK_CTRL] |= DISK_IEN;
}


#if DEBUGGING

void intrpt_dbg_printStackFrame(const sIntrptStackFrame *stack) {
	int i;
	vid_printf("stack-frame @ 0x%x\n",stack);
	vid_printf("\tirqNo=%d\n",stack->irqNo);
	vid_printf("\tpsw=#%08x\n",stack->psw);
	vid_printf("\tregister:\n");
	for(i = 0; i < REG_COUNT; i++)
		vid_printf("\tr[%d]=#%08x\n",i,stack->r[i]);
}

#endif