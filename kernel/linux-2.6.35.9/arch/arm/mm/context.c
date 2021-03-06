/*
 *  linux/arch/arm/mm/context.c
 *
 *  Copyright (C) 2002-2003 Deep Blue Solutions Ltd, all rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/percpu.h>

#include <asm/mmu_context.h>
#include <asm/tlbflush.h>

static IPIPE_DEFINE_SPINLOCK(cpu_asid_lock);
unsigned int cpu_last_asid = ASID_FIRST_VERSION;
#ifdef CONFIG_SMP
DEFINE_PER_CPU(struct mm_struct *, current_mm);
#endif

#if defined(CONFIG_IPIPE) && defined(CONFIG_SMP)
/*
 * We shall be able to serve interrupts while attempting to grab the
 * ASID lock on entry to __new_context(). This is a prerequisite for
 * broadcasting VNMIs to other CPUs later on, to have them reset their
 * current ASID, without risking deadlocks. I.e. each CPU shall be
 * able to reset the current ASID upon a remote request, while trying
 * to get a new ASID.
 */
#define asid_lock(flags)						\
	do {								\
		WARN_ON_ONCE(irqs_disabled_hw());			\
		while (!spin_trylock_irqsave(&cpu_asid_lock, (flags)))	\
			cpu_relax();					\
	} while (0)							\

#define asid_unlock(flags)	\
	spin_unlock_irqrestore(&cpu_asid_lock, flags)

#define asid_broadcast_reset()	\
	__ipipe_send_vnmi(reset_context, cpu_online_map, NULL);

#else /* !(CONFIG_IPIPE && CONFIG_SMP) */

#define asid_lock(flags)	\
	spin_lock_irqsave_cond(&cpu_asid_lock, flags)

#define asid_unlock(flags)	\
	spin_unlock_irqrestore_cond(&cpu_asid_lock, flags)

#define asid_broadcast_reset()	\
	smp_call_function(reset_context, NULL, 1);

#endif /* !(CONFIG_IPIPE && CONFIG_SMP) */

/*
 * We fork()ed a process, and we need a new context for the child
 * to run in.  We reserve version 0 for initial tasks so we will
 * always allocate an ASID. The ASID 0 is reserved for the TTBR
 * register changing sequence.
 */
void __init_new_context(struct task_struct *tsk, struct mm_struct *mm)
{
	mm->context.id = 0;
	spin_lock_init(&mm->context.id_lock);
}

static void flush_context(void)
{
	/* set the reserved ASID before flushing the TLB */
	asm("mcr	p15, 0, %0, c13, c0, 1\n" : : "r" (0));
	isb();
	local_flush_tlb_all();
	if (icache_is_vivt_asid_tagged()) {
		__flush_icache_all();
		dsb();
	}
}

#ifdef CONFIG_SMP

static void set_mm_context(struct mm_struct *mm, unsigned int asid)
{
	unsigned long flags;

	/*
	 * Locking needed for multi-threaded applications where the
	 * same mm->context.id could be set from different CPUs during
	 * the broadcast. This function is also called via IPI so the
	 * mm->context.id_lock has to be IRQ-safe.
	 */
	spin_lock_irqsave(&mm->context.id_lock, flags);
	if (likely((mm->context.id ^ cpu_last_asid) >> ASID_BITS)) {
		/*
		 * Old version of ASID found. Set the new one and
		 * reset mm_cpumask(mm).
		 */
		mm->context.id = asid;
		cpumask_clear(mm_cpumask(mm));
	}
	spin_unlock_irqrestore(&mm->context.id_lock, flags);

	/*
	 * Set the mm_cpumask(mm) bit for the current CPU.
	 */
	cpumask_set_cpu(ipipe_processor_id(), mm_cpumask(mm));
}

/*
 * Reset the ASID on the current CPU. This function call is broadcast
 * from the CPU handling the ASID rollover and holding cpu_asid_lock.
 */
static void reset_context(void *info)
{
	unsigned int asid;
	unsigned int cpu = ipipe_processor_id();
	struct mm_struct *mm = per_cpu(current_mm, cpu);

	/*
	 * Check if a current_mm was set on this CPU as it might still
	 * be in the early booting stages and using the reserved ASID.
	 */
	if (!mm)
		return;

	smp_rmb();
	asid = cpu_last_asid + cpu + 1;

	flush_context();
	set_mm_context(mm, asid);

	/* set the new ASID */
	asm("mcr	p15, 0, %0, c13, c0, 1\n" : : "r" (mm->context.id));
	isb();
}

#else

static inline void set_mm_context(struct mm_struct *mm, unsigned int asid)
{
	mm->context.id = asid;
	cpumask_copy(mm_cpumask(mm), cpumask_of(ipipe_processor_id()));
}

#endif

void __new_context(struct mm_struct *mm)
{
	int cpu = ipipe_processor_id();
	unsigned long flags;
	unsigned int asid;

	asid_lock(flags);
#ifdef CONFIG_SMP
	/*
	 * Check the ASID again, in case the change was broadcast from
	 * another CPU before we acquired the lock.
	 */
	if (unlikely(((mm->context.id ^ cpu_last_asid) >> ASID_BITS) == 0)) {
		cpumask_set_cpu(cpu, mm_cpumask(mm));
		asid_unlock(flags);
		return;
	}
#endif
	/*
	 * At this point, it is guaranteed that the current mm (with
	 * an old ASID) isn't active on any other CPU since the ASIDs
	 * are changed simultaneously via IPI.
	 */
	asid = ++cpu_last_asid;
	if (asid == 0)
		asid = cpu_last_asid = ASID_FIRST_VERSION;

	/*
	 * If we've used up all our ASIDs, we need
	 * to start a new version and flush the TLB.
	 */
	if (unlikely((asid & ~ASID_MASK) == 0)) {
		asid = cpu_last_asid + cpu + 1;
		flush_context();
#ifdef CONFIG_SMP
		smp_wmb();
		asid_broadcast_reset();
#endif
		cpu_last_asid += NR_CPUS;
	}

	set_mm_context(mm, asid);
	asid_unlock(flags);
}
