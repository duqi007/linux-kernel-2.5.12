/*
 * Copyright (C) 2002 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */
#ifndef _ASM_IA64_THREAD_INFO_H
#define _ASM_IA64_THREAD_INFO_H

#include <asm/offsets.h>
#include <asm/processor.h>
#include <asm/ptrace.h>

#define TI_EXEC_DOMAIN	0x00
#define TI_FLAGS	0x08
#define TI_CPU		0x0c
#define TI_ADDR_LIMIT	0x10
#define TI_PRE_COUNT	0x18

#define PREEMPT_ACTIVE	0x4000000

#ifndef __ASSEMBLY__

/*
 * On IA-64, we want to keep the task structure and kernel stack together, so they can be
 * mapped by a single TLB entry and so they can be addressed by the "current" pointer
 * without having to do pointer masking.
 */
struct thread_info {
	struct exec_domain *exec_domain;/* execution domain */
	__u32 flags;			/* thread_info flags (see TIF_*) */
	__u32 cpu;			/* current CPU */
	mm_segment_t addr_limit;	/* user-level address space limit */
	__s32 preempt_count;		/* 0=premptable, <0=BUG; will also serve as bh-counter */
};

#define INIT_THREAD_SIZE		/* tell sched.h not to declare the thread_union */
#define THREAD_SIZE			KERNEL_STACK_SIZE

#define INIT_THREAD_INFO(ti)			\
{						\
	exec_domain:	&default_exec_domain,	\
	flags:		0,			\
	cpu:		0,			\
	addr_limit:	KERNEL_DS,		\
	preempt_count:	0,			\
}

/* how to get the thread information struct from C */
#define current_thread_info() ((struct thread_info *) ((char *) current + IA64_TASK_SIZE))

#endif /* !__ASSEMBLY */

/*
 * thread information flags
 * - these are process state flags that various assembly files may need to access
 * - pending work-to-be-done flags are in least-significant 16 bits, other flags
 *   in top 16 bits
 */
#define TIF_NOTIFY_RESUME	0	/* resumption notification requested */
#define TIF_SIGPENDING		1	/* signal pending */
#define TIF_NEED_RESCHED	2	/* rescheduling necessary */
#define TIF_SYSCALL_TRACE	3	/* syscall trace active */
#define TIF_POLLING_NRFLAG	16	/* true if poll_idle() is polling TIF_NEED_RESCHED */

#define TIF_WORK_MASK		0x7	/* like TIF_ALLWORK_BITS but sans TIF_SYSCALL_TRACE */
#define TIF_ALLWORK_MASK	0xf	/* bits 0..3 are "work to do on user-return" bits */

#define _TIF_SYSCALL_TRACE	(1 << TIF_SYSCALL_TRACE)
#define _TIF_NOTIFY_RESUME	(1 << TIF_NOTIFY_RESUME)
#define _TIF_SIGPENDING		(1 << TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1 << TIF_NEED_RESCHED)
#define _TIF_USEDFPU		(1 << TIF_USEDFPU)
#define _TIF_POLLING_NRFLAG	(1 << TIF_POLLING_NRFLAG)

#endif /* _ASM_IA64_THREAD_INFO_H */
