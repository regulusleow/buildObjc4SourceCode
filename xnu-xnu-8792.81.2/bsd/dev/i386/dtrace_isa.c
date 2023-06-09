/*
 * Copyright (c) 2005-2018 Apple Inc. All rights reserved.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_START@
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. The rights granted to you under the License
 * may not be used to create, or enable the creation or redistribution of,
 * unlawful or unlicensed copies of an Apple operating system, or to
 * circumvent, violate, or enable the circumvention or violation of, any
 * terms of an Apple operating system software license agreement.
 * 
 * Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_OSREFERENCE_LICENSE_HEADER_END@
 */

#include <kern/thread.h>
#include <mach/thread_status.h>

typedef x86_saved_state_t savearea_t;

#include <stdarg.h>
#include <string.h>
#include <sys/malloc.h>
#include <sys/time.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/proc_internal.h>
#include <sys/kauth.h>
#include <sys/dtrace.h>
#include <sys/dtrace_impl.h>
#include <libkern/OSAtomic.h>
#include <i386/x86_hypercall.h>
#include <kern/thread_call.h>
#include <kern/task.h>
#include <kern/sched_prim.h>
#include <miscfs/devfs/devfs.h>
#include <mach/vm_param.h>
#include <machine/pal_routines.h>
#include <i386/cpuid.h>
#include <i386/mp.h>
#include <machine/trap.h>

/*
 * APPLE NOTE:  The regmap is used to decode which 64bit uregs[] register
 * is being accessed when passed the 32bit uregs[] constant (based on
 * the reg.d translator file). The dtrace_getreg() is smart enough to handle
 * the register mappings.   The register set definitions are the same as
 * those used by the fasttrap_getreg code.
 */
#include "fasttrap_regset.h"
static const uint8_t regmap[19] = {
    REG_GS,		/* GS */
    REG_FS,		/* FS */
    REG_ES,		/* ES */
    REG_DS,		/* DS */
    REG_RDI,		/* EDI */
    REG_RSI,		/* ESI */
    REG_RBP,		/* EBP, REG_FP  */
    REG_RSP,		/* ESP */
    REG_RBX,		/* EBX */
    REG_RDX,		/* EDX, REG_R1  */
    REG_RCX,		/* ECX */
    REG_RAX,		/* EAX, REG_R0  */
    REG_TRAPNO,		/* TRAPNO */
    REG_ERR,		/* ERR */
    REG_RIP,		/* EIP, REG_PC  */
    REG_CS,		/* CS */
    REG_RFL,		/* EFL, REG_PS  */
    REG_RSP,		/* UESP, REG_SP */
    REG_SS		/* SS */
};    

extern dtrace_id_t      dtrace_probeid_error;   /* special ERROR probe */

void
dtrace_probe_error(dtrace_state_t *state, dtrace_epid_t epid, int which,
    int fltoffs, int fault, uint64_t illval)
{
    /*
     * For the case of the error probe firing lets
     * stash away "illval" here, and special-case retrieving it in DIF_VARIABLE_ARG.
     */
    state->dts_arg_error_illval = illval;
    dtrace_probe( dtrace_probeid_error, (uint64_t)(uintptr_t)state, epid, which, fltoffs, fault );
}

/*
 * Atomicity and synchronization
 */
void
dtrace_membar_producer(void)
{
	__asm__ volatile("sfence");
}

void
dtrace_membar_consumer(void)
{
	__asm__ volatile("lfence");
}

/*
 * Interrupt manipulation
 * XXX dtrace_getipl() can be called from probe context.
 */
int
dtrace_getipl(void)
{
	/*
	 * XXX Drat, get_interrupt_level is MACH_KERNEL_PRIVATE
	 * in osfmk/kern/cpu_data.h
	 */
	/* return get_interrupt_level(); */
	return (ml_at_interrupt_context() ? 1: 0);
}

/*
 * MP coordination
 */
typedef struct xcArg {
	processorid_t cpu;
	dtrace_xcall_t f;
	void *arg;
} xcArg_t;

static void
xcRemote( void *foo )
{
	xcArg_t *pArg = (xcArg_t *)foo;
	
	if ( pArg->cpu == CPU->cpu_id || pArg->cpu == DTRACE_CPUALL ) {
		(pArg->f)(pArg->arg);
	}
}


/*
 * dtrace_xcall() is not called from probe context.
 */
void
dtrace_xcall(processorid_t cpu, dtrace_xcall_t f, void *arg)
{
	xcArg_t xcArg;
	
	xcArg.cpu = cpu;
	xcArg.f = f;
	xcArg.arg = arg;

	if (cpu == DTRACE_CPUALL) {
		mp_cpus_call (CPUMASK_ALL, ASYNC, xcRemote, (void*)&xcArg);
	}
	else {
		mp_cpus_call (cpu_to_cpumask((cpu_t)cpu), ASYNC, xcRemote, (void*)&xcArg);
	}
}

/*
 * Runtime and ABI
 */
uint64_t
dtrace_getreg(struct regs *savearea, uint_t reg)
{
	boolean_t is64Bit = proc_is64bit(current_proc());
	x86_saved_state_t *regs = (x86_saved_state_t *)savearea;

	if (regs == NULL) {
		DTRACE_CPUFLAG_SET(CPU_DTRACE_ILLOP);
		return (0);
	}

	if (is64Bit) {
	    if (reg <= SS) {
		reg = regmap[reg];
	    } else {
		reg -= (SS + 1);
	    }

	    switch (reg) {
	    case REG_RDI:
		return (uint64_t)(regs->ss_64.rdi);
	    case REG_RSI:
		return (uint64_t)(regs->ss_64.rsi);
	    case REG_RDX:
		return (uint64_t)(regs->ss_64.rdx);
	    case REG_RCX:
		return (uint64_t)(regs->ss_64.rcx);
	    case REG_R8:
		return (uint64_t)(regs->ss_64.r8);
	    case REG_R9:
		return (uint64_t)(regs->ss_64.r9);
	    case REG_RAX:
		return (uint64_t)(regs->ss_64.rax);
	    case REG_RBX:
		return (uint64_t)(regs->ss_64.rbx);
	    case REG_RBP:
		return (uint64_t)(regs->ss_64.rbp);
	    case REG_R10:
		return (uint64_t)(regs->ss_64.r10);
	    case REG_R11:
		return (uint64_t)(regs->ss_64.r11);
	    case REG_R12:
		return (uint64_t)(regs->ss_64.r12);
	    case REG_R13:
		return (uint64_t)(regs->ss_64.r13);
	    case REG_R14:
		return (uint64_t)(regs->ss_64.r14);
	    case REG_R15:
		return (uint64_t)(regs->ss_64.r15);
	    case REG_FS:
		return (uint64_t)(regs->ss_64.fs);
	    case REG_GS:
		return (uint64_t)(regs->ss_64.gs);
	    case REG_TRAPNO:
		return (uint64_t)(regs->ss_64.isf.trapno);
	    case REG_ERR:
		return (uint64_t)(regs->ss_64.isf.err);
	    case REG_RIP:
		return (uint64_t)(regs->ss_64.isf.rip);
	    case REG_CS:
		return (uint64_t)(regs->ss_64.isf.cs);
	    case REG_SS:
		return (uint64_t)(regs->ss_64.isf.ss);
	    case REG_RFL:
		return (uint64_t)(regs->ss_64.isf.rflags);
	    case REG_RSP:
		return (uint64_t)(regs->ss_64.isf.rsp);
	    case REG_DS:
	    case REG_ES:
	    default:
		DTRACE_CPUFLAG_SET(CPU_DTRACE_ILLOP);
		return (0);
	    }
	
	} else {   /* is 32bit user */
		/* beyond register SS */
		if (reg > x86_SAVED_STATE32_COUNT - 1) {
			DTRACE_CPUFLAG_SET(CPU_DTRACE_ILLOP);
			return (0);
		}
		return (uint64_t)((unsigned int *)(&(regs->ss_32.gs)))[reg];
	}
}

uint64_t
dtrace_getvmreg(uint_t ndx)
{
	uint64_t reg = 0;
	bool failed = false;

	/* Any change in the vmread final opcode must be reflected in dtrace_handle_trap below. */
	__asm__ __volatile__(
		"vmread %2, %0\n"
		"ja 1f\n"
		"mov $1, %1\n"
		"1:\n"
	: "=a" (reg), "+r" (failed) : "D" ((uint64_t)ndx));

	/*
	 * Check for fault in vmreg first. If DTrace has recovered the fault cause by
	 * vmread above then the value in failed will be unreliable.
	 */
	if (DTRACE_CPUFLAG_ISSET(CPU_DTRACE_ILLOP)) {
		return 0;
	}

	/* If vmread succeeded but failed because CF or ZS is 1 report fail. */
	if (failed) {
		DTRACE_CPUFLAG_SET(CPU_DTRACE_BADADDR);
		cpu_core[CPU->cpu_id].cpuc_dtrace_illval = ndx;
		return 0;
	}

	return reg;
}

static void
dtrace_vmcall(x86_saved_state_t *regs, uint64_t *rflags)
{
	uint64_t flags = 0;

	/*
	 * No constraints available for r8 or r9 which means they must be
	 * handled explicitly.
	 */
	__asm__ volatile (
	     "   movq %12, %%r8  \n"
	     "   movq %13, %%r9  \n"
	     "   vmcall          \n"
	     "   movq %%r8, %5   \n"
	     "   movq %%r9, %6   \n"
	     "   pushfq          \n"
	     "   popq %7         \n"

	    : "=a" (regs->ss_64.rax),
	      "=D" (regs->ss_64.rdi),
	      "=S" (regs->ss_64.rsi),
	      "=d" (regs->ss_64.rdx),
	      "=c" (regs->ss_64.rcx),
	      "=r" (regs->ss_64.r8),  /* %5 */
	      "=r" (regs->ss_64.r9),  /* %6 */
	      "=r" (flags)            /* %7 */

	    : "a"  (regs->ss_64.rax),
	      "D"  (regs->ss_64.rdi),
	      "S"  (regs->ss_64.rsi),
	      "d"  (regs->ss_64.rdx),
	      "c"  (regs->ss_64.rcx),
	      "r"  (regs->ss_64.r8),  /* %12 */
	      "r"  (regs->ss_64.r9)   /* %13 */

	    : "memory", "r8", "r9");

	*rflags = flags;

	return;
}

static inline void
dtrace_cpuid(x86_saved_state_t *regs)
{
	__asm__ volatile (
	     "cpuid"
	    : "=a" (regs->ss_64.rax),
	      "=b" (regs->ss_64.rbx),
	      "=c" (regs->ss_64.rcx),
	      "=d" (regs->ss_64.rdx)

	    : "a"  (regs->ss_64.rax),
	      "b"  (regs->ss_64.rbx),
	      "c"  (regs->ss_64.rcx),
	      "d"  (regs->ss_64.rdx));
}

static bool
dtrace_applepv_available(uint64_t flag)
{
	static bool checked = false;
	static uint64_t features = 0;

	if (checked) {
		return (features & flag) != 0;
	}

	x86_saved_state_t regs = {0};

	regs.ss_64.rax = 1;
	dtrace_cpuid(&regs);

	/* Bit 31 - HV bit. */
	if ((regs.ss_64.rcx & _Bit(31)) != 0) {
		for (uint32_t base = 0x40000100; base < 0x40010000; base += 0x100) {
			regs.ss_64.rax = base;
			dtrace_cpuid(&regs);

			/* "apple-pv-xnu" */
			if (regs.ss_64.rbx != 0x6c707061 ||
			    regs.ss_64.rcx != 0x76702d65 ||
			    regs.ss_64.rdx != 0x756e782d) {
				continue;
			}

			uint64_t feature_leaf = regs.ss_64.rax;

			regs.ss_64.rax = base + APPLEPV_INTERFACE_LEAF_INDEX;
			dtrace_cpuid(&regs);

			/* "AH#1" */
			if (regs.ss_64.rax != 0x31234841) {
				continue;
			}

			/* Find features. */
			regs.ss_64.rax = feature_leaf;
			dtrace_cpuid(&regs);

			features = regs.ss_64.rdx;
			break;
		}
	}

	checked = true;
	return (features & flag) != 0;
}

void
dtrace_livedump(char *filename, size_t len)
{
	x86_saved_state_t regs = {
	    .ss_64.rax = HVG_HCALL_CODE(HVG_HCALL_TRIGGER_DUMP),
	    .ss_64.rdi = HVG_HCALL_DUMP_OPTION_REGULAR,
	};

	if (len > 0) {
		filename[0] = '\0';
	}

	if (!dtrace_applepv_available(CPUID_LEAF_FEATURE_COREDUMP)) {
		DTRACE_CPUFLAG_SET(CPU_DTRACE_ILLOP);
		return;
	}

	uint64_t rflags = 0;
	dtrace_vmcall(&regs, &rflags);
	if ((rflags & EFL_CF) != 0) {
		/* An empty filename indicates failure to dump. */
		return;
	}

	/* Extract the filename. */
	char str[57] = {'\0'};
	memcpy(&str[0],  &regs.ss_64.rax, 8);
	memcpy(&str[8],  &regs.ss_64.rdi, 8);
	memcpy(&str[16], &regs.ss_64.rsi, 8);
	memcpy(&str[24], &regs.ss_64.rdx, 8);
	memcpy(&str[32], &regs.ss_64.rcx, 8);
	memcpy(&str[40], &regs.ss_64.r8,  8);
	memcpy(&str[48], &regs.ss_64.r9,  8);

	(void) strlcpy(filename, str, len);
}

#define RETURN_OFFSET 4
#define RETURN_OFFSET64 8

static int
dtrace_getustack_common(uint64_t *pcstack, int pcstack_limit, user_addr_t pc,
    user_addr_t sp)
{
	volatile uint16_t *flags =
	    (volatile uint16_t *)&cpu_core[CPU->cpu_id].cpuc_dtrace_flags;

#if 0
	uintptr_t oldcontext = lwp->lwp_oldcontext; /* XXX signal stack crawl */
	size_t s1, s2;
#endif
	int ret = 0;
	boolean_t is64Bit = proc_is64bit(current_proc());

	ASSERT(pcstack == NULL || pcstack_limit > 0);
	
#if 0 /* XXX signal stack crawl */
	if (p->p_model == DATAMODEL_NATIVE) {
		s1 = sizeof (struct frame) + 2 * sizeof (long);
		s2 = s1 + sizeof (siginfo_t);
	} else {
		s1 = sizeof (struct frame32) + 3 * sizeof (int);
		s2 = s1 + sizeof (siginfo32_t);
	}
#endif

	while (pc != 0) {
		ret++;
		if (pcstack != NULL) {
			*pcstack++ = (uint64_t)pc;
			pcstack_limit--;
			if (pcstack_limit <= 0)
				break;
		}

		if (sp == 0)
			break;

#if 0 /* XXX signal stack crawl */
		if (oldcontext == sp + s1 || oldcontext == sp + s2) {
			if (p->p_model == DATAMODEL_NATIVE) {
				ucontext_t *ucp = (ucontext_t *)oldcontext;
				greg_t *gregs = ucp->uc_mcontext.gregs;

				sp = dtrace_fulword(&gregs[REG_FP]);
				pc = dtrace_fulword(&gregs[REG_PC]);

				oldcontext = dtrace_fulword(&ucp->uc_link);
			} else {
				ucontext32_t *ucp = (ucontext32_t *)oldcontext;
				greg32_t *gregs = ucp->uc_mcontext.gregs;

				sp = dtrace_fuword32(&gregs[EBP]);
				pc = dtrace_fuword32(&gregs[EIP]);

				oldcontext = dtrace_fuword32(&ucp->uc_link);
			}
		} 
		else
#endif
		{
			if (is64Bit) {
				pc = dtrace_fuword64((sp + RETURN_OFFSET64));
				sp = dtrace_fuword64(sp);
			} else {
				pc = dtrace_fuword32((sp + RETURN_OFFSET));
				sp = dtrace_fuword32(sp);
			}
		}

		/* Truncate ustack if the iterator causes fault. */
		if (*flags & CPU_DTRACE_FAULT) {
			*flags &= ~CPU_DTRACE_FAULT;
			break;
		}
	}

	return (ret);
}


/*
 * The return value indicates if we've modified the stack.
 */
static int
dtrace_adjust_stack(uint64_t **pcstack, int *pcstack_limit, user_addr_t *pc,
                    user_addr_t sp)
{
    volatile uint16_t *flags = (volatile uint16_t *) &cpu_core[CPU->cpu_id].cpuc_dtrace_flags;
    int64_t missing_tos;
    int rc = 0;
    boolean_t is64Bit = proc_is64bit(current_proc());

    ASSERT(pc != NULL);

    if (DTRACE_CPUFLAG_ISSET(CPU_DTRACE_ENTRY)) {
        /*
         * If we found ourselves in an entry probe, the frame pointer has not
         * yet been pushed (that happens in the
         * function prologue).  The best approach is to
	 * add the current pc as a missing top of stack,
         * and back the pc up to the caller, which is stored  at the
         * current stack pointer address since the call
         * instruction puts it there right before
         * the branch.
         */

        missing_tos = *pc;

        if (is64Bit)
            *pc = dtrace_fuword64(sp);
        else
            *pc = dtrace_fuword32(sp);

	/* Truncate ustack if the iterator causes fault. */
	if (*flags & CPU_DTRACE_FAULT) {
		*flags &= ~CPU_DTRACE_FAULT;
	}
    } else {
        /*
         * We might have a top of stack override, in which case we just
         * add that frame without question to the top.  This
         * happens in return probes where you have a valid
         * frame pointer, but it's for the callers frame
         * and you'd like to add the pc of the return site
         * to the frame.
         */
        missing_tos = cpu_core[CPU->cpu_id].cpuc_missing_tos;
    }

    if (missing_tos != 0) {
        if (pcstack != NULL && pcstack_limit != NULL) {
            /*
	     * If the missing top of stack has been filled out, then
	     * we add it and adjust the size.
             */
	    *(*pcstack)++ = missing_tos;
	    (*pcstack_limit)--;
	}
        /*
	 * return 1 because we would have changed the
	 * stack whether or not it was passed in.  This
	 * ensures the stack count is correct
	 */
         rc = 1;
    }
    return rc;
}

void
dtrace_getupcstack(uint64_t *pcstack, int pcstack_limit)
{
	thread_t thread = current_thread();
	x86_saved_state_t *regs;
	user_addr_t pc, sp, fp;
	volatile uint16_t *flags =
	    (volatile uint16_t *)&cpu_core[CPU->cpu_id].cpuc_dtrace_flags;
	int n;
	boolean_t is64Bit = proc_is64bit(current_proc());

	if (*flags & CPU_DTRACE_FAULT)
		return;

	if (pcstack_limit <= 0)
		return;

	/*
	 * If there's no user context we still need to zero the stack.
	 */
	if (thread == NULL)
		goto zero;

	pal_register_cache_state(thread, VALID);
	regs = (x86_saved_state_t *)find_user_regs(thread);
	if (regs == NULL)
		goto zero;
		
	*pcstack++ = (uint64_t)dtrace_proc_selfpid();
	pcstack_limit--;

	if (pcstack_limit <= 0)
		return;

	if (is64Bit) {
		pc = regs->ss_64.isf.rip;
		sp = regs->ss_64.isf.rsp;
		fp = regs->ss_64.rbp;
	} else {
		pc = regs->ss_32.eip;
		sp = regs->ss_32.uesp;
		fp = regs->ss_32.ebp;
	}

        /*
	 * The return value indicates if we've modified the stack.
	 * Since there is nothing else to fix up in either case,
	 * we can safely ignore it here.
	 */
	(void)dtrace_adjust_stack(&pcstack, &pcstack_limit, &pc, sp);

	if(pcstack_limit <= 0)
	    return;

	/*
	 * Note that unlike ppc, the x86 code does not use
	 * CPU_DTRACE_USTACK_FP. This is because x86 always
	 * traces from the fp, even in syscall/profile/fbt
	 * providers.
	 */
	n = dtrace_getustack_common(pcstack, pcstack_limit, pc, fp);
	ASSERT(n >= 0);
	ASSERT(n <= pcstack_limit);

	pcstack += n;
	pcstack_limit -= n;

zero:
	while (pcstack_limit-- > 0)
		*pcstack++ = 0;
}

int
dtrace_getustackdepth(void)
{
	thread_t thread = current_thread();
	x86_saved_state_t *regs;
	user_addr_t pc, sp, fp;
	int n = 0;
	boolean_t is64Bit = proc_is64bit(current_proc());

	if (thread == NULL)
		return 0;

	if (DTRACE_CPUFLAG_ISSET(CPU_DTRACE_FAULT))
		return (-1);

	pal_register_cache_state(thread, VALID);
	regs = (x86_saved_state_t *)find_user_regs(thread);
	if (regs == NULL)
		return 0;

	if (is64Bit) {
		pc = regs->ss_64.isf.rip;
		sp = regs->ss_64.isf.rsp;
		fp = regs->ss_64.rbp;
	} else {
		pc = regs->ss_32.eip;
		sp = regs->ss_32.uesp;
		fp = regs->ss_32.ebp;
	}

	if (dtrace_adjust_stack(NULL, NULL, &pc, sp) == 1) {
	    /*
	     * we would have adjusted the stack if we had
	     * supplied one (that is what rc == 1 means).
	     * Also, as a side effect, the pc might have
	     * been fixed up, which is good for calling
	     * in to dtrace_getustack_common.
	     */
	    n++;
	}
	
	/*
	 * Note that unlike ppc, the x86 code does not use
	 * CPU_DTRACE_USTACK_FP. This is because x86 always
	 * traces from the fp, even in syscall/profile/fbt
	 * providers.
	 */

	n += dtrace_getustack_common(NULL, 0, pc, fp);

	return (n);
}

void
dtrace_getufpstack(uint64_t *pcstack, uint64_t *fpstack, int pcstack_limit)
{
	thread_t thread = current_thread();
	savearea_t *regs;
	user_addr_t pc, sp;
	volatile uint16_t *flags =
	    (volatile uint16_t *)&cpu_core[CPU->cpu_id].cpuc_dtrace_flags;
#if 0
	uintptr_t oldcontext;
	size_t s1, s2;
#endif
	boolean_t is64Bit = proc_is64bit(current_proc());

	if (*flags & CPU_DTRACE_FAULT)
		return;

	if (pcstack_limit <= 0)
		return;

	/*
	 * If there's no user context we still need to zero the stack.
	 */
	if (thread == NULL)
		goto zero;

	regs = (savearea_t *)find_user_regs(thread);
	if (regs == NULL)
		goto zero;
		
	*pcstack++ = (uint64_t)dtrace_proc_selfpid();
	pcstack_limit--;

	if (pcstack_limit <= 0)
		return;

	pc = regs->ss_32.eip;
	sp = regs->ss_32.ebp;
	
#if 0 /* XXX signal stack crawl */
	oldcontext = lwp->lwp_oldcontext;

	if (p->p_model == DATAMODEL_NATIVE) {
		s1 = sizeof (struct frame) + 2 * sizeof (long);
		s2 = s1 + sizeof (siginfo_t);
	} else {
		s1 = sizeof (struct frame32) + 3 * sizeof (int);
		s2 = s1 + sizeof (siginfo32_t);
	}
#endif

	if(dtrace_adjust_stack(&pcstack, &pcstack_limit, &pc, sp) == 1) {
            /*
	     * we made a change.
	     */
	    *fpstack++ = 0;
	    if (pcstack_limit <= 0)
		return;
	}

	while (pc != 0) {
		*pcstack++ = (uint64_t)pc;
		*fpstack++ = sp;
		pcstack_limit--;
		if (pcstack_limit <= 0)
			break;

		if (sp == 0)
			break;

#if 0 /* XXX signal stack crawl */
		if (oldcontext == sp + s1 || oldcontext == sp + s2) {
			if (p->p_model == DATAMODEL_NATIVE) {
				ucontext_t *ucp = (ucontext_t *)oldcontext;
				greg_t *gregs = ucp->uc_mcontext.gregs;

				sp = dtrace_fulword(&gregs[REG_FP]);
				pc = dtrace_fulword(&gregs[REG_PC]);

				oldcontext = dtrace_fulword(&ucp->uc_link);
			} else {
				ucontext_t *ucp = (ucontext_t *)oldcontext;
				greg_t *gregs = ucp->uc_mcontext.gregs;

				sp = dtrace_fuword32(&gregs[EBP]);
				pc = dtrace_fuword32(&gregs[EIP]);

				oldcontext = dtrace_fuword32(&ucp->uc_link);
			}
		} 
		else
#endif
		{
			if (is64Bit) {
				pc = dtrace_fuword64((sp + RETURN_OFFSET64));
				sp = dtrace_fuword64(sp);
			} else {
				pc = dtrace_fuword32((sp + RETURN_OFFSET));
				sp = dtrace_fuword32(sp);
			}
		}

		/* Truncate ustack if the iterator causes fault. */
		if (*flags & CPU_DTRACE_FAULT) {
			*flags &= ~CPU_DTRACE_FAULT;
			break;
		}
	}

zero:
	while (pcstack_limit-- > 0)
		*pcstack++ = 0;
}

void
dtrace_getpcstack(pc_t *pcstack, int pcstack_limit, int aframes,
		  uint32_t *intrpc)
{
	struct frame *fp = (struct frame *)__builtin_frame_address(0);
	struct frame *nextfp, *minfp, *stacktop;
	int depth = 0;
	int last = 0;
	uintptr_t pc;
	uintptr_t caller = CPU->cpu_dtrace_caller;
	int on_intr;

	if ((on_intr = CPU_ON_INTR(CPU)) != 0)
		stacktop = (struct frame *)dtrace_get_cpu_int_stack_top();
	else
		stacktop = (struct frame *)(dtrace_get_kernel_stack(current_thread()) + kernel_stack_size);

	minfp = fp;

	aframes++;

	if (intrpc != NULL && depth < pcstack_limit)
		pcstack[depth++] = (pc_t)intrpc;

	while (depth < pcstack_limit) {
		nextfp = *(struct frame **)fp;
		pc = *(uintptr_t *)(((uintptr_t)fp) + RETURN_OFFSET64);

		if (nextfp <= minfp || nextfp >= stacktop) {
			if (on_intr) {
				/*
				 * Hop from interrupt stack to thread stack.
				 */
				vm_offset_t kstack_base = dtrace_get_kernel_stack(current_thread());

				minfp = (struct frame *)kstack_base;
				stacktop = (struct frame *)(kstack_base + kernel_stack_size);

				on_intr = 0;
				continue;
			}
			/*
			 * This is the last frame we can process; indicate
			 * that we should return after processing this frame.
			 */
			last = 1;
		}

		if (aframes > 0) {
			if (--aframes == 0 && caller != 0) {
				/*
				 * We've just run out of artificial frames,
				 * and we have a valid caller -- fill it in
				 * now.
				 */
				ASSERT(depth < pcstack_limit);
				pcstack[depth++] = (pc_t)caller;
				caller = 0;
			}
		} else {
			if (depth < pcstack_limit)
				pcstack[depth++] = (pc_t)pc;
		}

		if (last) {
			while (depth < pcstack_limit)
				pcstack[depth++] = 0;
			return;
		}

		fp = nextfp;
		minfp = fp;
	}
}

struct frame {
	struct frame *backchain;
	uintptr_t retaddr;
};

uint64_t
dtrace_getarg(int arg, int aframes, dtrace_mstate_t *mstate, dtrace_vstate_t *vstate)
{
	uint64_t val = 0;
	struct frame *fp = (struct frame *)__builtin_frame_address(0);
	uintptr_t *stack;
	uintptr_t pc;
	int i;


    /*
     * A total of 6 arguments are passed via registers; any argument with
     * index of 5 or lower is therefore in a register.
     */
    int inreg = 5;

	for (i = 1; i <= aframes; i++) {
		fp = fp->backchain;
		pc = fp->retaddr;

		if (dtrace_invop_callsite_pre != NULL
			&& pc  >  (uintptr_t)dtrace_invop_callsite_pre
			&& pc  <= (uintptr_t)dtrace_invop_callsite_post) {
			/*
			 * In the case of x86_64, we will use the pointer to the
			 * save area structure that was pushed when we took the
			 * trap.  To get this structure, we must increment
			 * beyond the frame structure. If the
			 * argument that we're seeking is passed on the stack,
			 * we'll pull the true stack pointer out of the saved
			 * registers and decrement our argument by the number
			 * of arguments passed in registers; if the argument
			 * we're seeking is passed in regsiters, we can just
			 * load it directly.
			 */

			/* fp points to frame of dtrace_invop() activation. */
			fp = fp->backchain; /* to fbt_perfcallback() activation. */
			fp = fp->backchain; /* to kernel_trap() activation. */
			fp = fp->backchain; /* to trap_from_kernel() activation. */
			
			x86_saved_state_t   *tagged_regs = (x86_saved_state_t *)&fp[1];
			x86_saved_state64_t *saved_state = saved_state64(tagged_regs);

			if (arg <= inreg) {
				stack = (uintptr_t *)(void*)&saved_state->rdi;
			} else {
				fp = (struct frame *)(saved_state->isf.rsp);
				stack = (uintptr_t *)&fp[1]; /* Find marshalled
								arguments */
				arg -= inreg + 1;
			}
			goto load;
		}
	}

	/*
	 * We know that we did not come through a trap to get into
	 * dtrace_probe() --  We arrive here when the provider has
	 * called dtrace_probe() directly.
	 * The probe ID is the first argument to dtrace_probe().
	 * We must advance beyond that to get the argX.
	 */
	arg++; /* Advance past probeID */

	if (arg <= inreg) {
		/*
		 * This shouldn't happen.  If the argument is passed in a
		 * register then it should have been, well, passed in a
		 * register...
		 */
		DTRACE_CPUFLAG_SET(CPU_DTRACE_ILLOP);
		return (0);
	}

	arg -= (inreg + 1);
	stack = (uintptr_t *)&fp[1]; /* Find marshalled arguments */

load:
	if (dtrace_canload((uint64_t)(stack + arg), sizeof(uint64_t),
		mstate, vstate)) {
		/* dtrace_probe arguments arg0 ... arg4 are 64bits wide */
		val = dtrace_load64((uint64_t)(stack + arg));
	}

	return (val);
}

/*
 * Load/Store Safety
 */
void
dtrace_toxic_ranges(void (*func)(uintptr_t base, uintptr_t limit))
{
	/*
	 * "base" is the smallest toxic address in the range, "limit" is the first
	 * VALID address greater than "base".
	 */
	func(0x0, VM_MIN_KERNEL_AND_KEXT_ADDRESS);
	if (VM_MAX_KERNEL_ADDRESS < ~(uintptr_t)0)
			func(VM_MAX_KERNEL_ADDRESS + 1, ~(uintptr_t)0);
}

/*
 * Trap Safety
 */
extern boolean_t dtrace_handle_trap(int, x86_saved_state_t *);

boolean_t
dtrace_handle_trap(int trapno, x86_saved_state_t *state)
{
	x86_saved_state64_t *saved_state = saved_state64(state);

	if (!DTRACE_CPUFLAG_ISSET(CPU_DTRACE_NOFAULT)) {
		return FALSE;
	}

	/*
	 * General purpose solution would require pulling in disassembler. Right now there
	 * is only one specific case to be handled so it is hardcoded here.
	 */
	if (trapno == T_INVALID_OPCODE) {
		uint8_t *inst = (uint8_t *)saved_state->isf.rip;

		/* vmread %rdi, %rax */
		if (inst[0] == 0x0f && inst[1] == 0x78 && inst[2] == 0xf8) {
			DTRACE_CPUFLAG_SET(CPU_DTRACE_ILLOP);
			saved_state->isf.rip += 3;
			return TRUE;
		}
	}

	return FALSE;
}
