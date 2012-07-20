/*
 * Copyright (C) 2001, 2002, 2003 Broadcom Corporation
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <linux/init.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/smp.h>
#include <linux/kernel_stat.h>

#include <asm/mmu_context.h>
#include <asm/io.h>
#include <asm/uasm.h>
#include <asm/r4kcache.h>

#include "smp_cp0.h"

//#define DEBUG
//#define SMP_DEBUG

#ifdef SMP_DEBUG
static void jzsoc_smp_showregs(void);
#else
#define jzsoc_smp_showregs() do { } while (0)
#endif

static DEFINE_SPINLOCK(smp_lock);
#define smp_spinlock() 		spin_lock(&smp_lock)
#define smp_spinunlock() 	spin_unlock(&smp_lock)

static int smp_flag;
#define SMP_FLAG (*(volatile int*)KSEG1ADDR(&smp_flag))

static struct cpumask cpu_ready_e, cpu_running, cpu_start;
static struct cpumask *cpu_ready;
static unsigned long boot_sp, boot_gp;

/* Special cp0 register init */
static void __cpuinit jzsoc_smp_init(void)
{
	unsigned int imask = STATUSF_IP3 | STATUSF_IP2 |
		STATUSF_IP1 | STATUSF_IP0;

	change_c0_status(ST0_IM, imask);
}

/*
 * Code to run on secondary just after probing the CPU
 */
extern void percpu_timer_setup(void);
static void __cpuinit jzsoc_init_secondary(void)
{
	jzsoc_smp_init();
	jzsoc_smp_showregs();
	percpu_timer_setup();
}

/*
 * Do any tidying up before marking online and running the idle
 * loop
 */
static void __cpuinit jzsoc_smp_finish(void)
{
	int cpu = smp_processor_id();

	local_irq_enable();
	jzsoc_smp_showregs();
	pr_info("[SMP] slave cpu%d start up finished.\n",cpu);
}

static struct bounce_addr {
	unsigned long base;
} smp_bounce;

extern void __jzsoc_secondary_start(void);
static void build_bounce_code(unsigned long *spp, unsigned long *gpp)
{
	int i;
	unsigned long base[32] = {0,};
	unsigned int pflag = (unsigned int)KSEG1ADDR(&smp_flag);
	unsigned int entry = (unsigned int)__jzsoc_secondary_start;
	unsigned int *p;

	for(i=0;i<32;i++) {
		base[i] = __get_free_pages(GFP_KERNEL, 0);
		if(!base[i] || (base[i] & 0xffff))
			continue;
		smp_bounce.base = base[i];
		break;
	}

	for(i=i-1;i>=0;i--) {
		free_pages(base[i], 0);
	}

	BUG_ON(!smp_bounce.base || (smp_bounce.base & 0xffff));

	p = (unsigned int*)smp_bounce.base;
	UASM_i_LA(&p, 26, pflag);
	UASM_i_LW(&p, 2, 0, 26);
	UASM_i_ADDIU(&p, 2, 2, 1);
	UASM_i_SW(&p, 2, 0, 26);

	/* t7: cpu_start. t8: cpu_ready. t9: cpu_running. */
	UASM_i_LA(&p, 15, (unsigned long)cpu_start.bits);
	UASM_i_LA(&p, 24, (unsigned long)cpu_ready_e.bits);
	UASM_i_LA(&p, 25, (unsigned long)cpu_running.bits);

	UASM_i_LA(&p, 29, (unsigned long)spp);
	UASM_i_LA(&p, 28, (unsigned long)gpp);
	UASM_i_LA(&p, 31, entry);
	uasm_i_jr(&p, 31);
	uasm_i_nop(&p);
}

static int cpu_boot(int cpu, unsigned long sp, unsigned long gp)
{
	if (cpumask_test_cpu(cpu, &cpu_running))
		return -EINVAL;

	if (cpumask_test_cpu(cpu, cpu_ready)) {
		boot_sp = sp;
		boot_gp = gp;

		smp_wmb();
		cpumask_set_cpu(cpu, &cpu_start);
		while (!cpumask_test_cpu(cpu, &cpu_running))
			;
		return 0;
	}

	return -EINVAL;
}

/*
 * Setup the PC, SP, and GP of a secondary processor and start it
 * running!
 */
extern void __jzsoc_secondary_start(void);
static void __cpuinit jzsoc_boot_secondary(int cpu, struct task_struct *idle)
{
	int err;
	unsigned long flags,reim,ctrl;

	/* reset register */
	set_smp_reim(0x0100);
	set_smp_ctrl(0xfffe);
	set_smp_mbox0(0);
	set_smp_mbox1(0);
	set_smp_mbox2(0);
	set_smp_mbox3(0);
	set_smp_status(0);

	reim = 0x01ff;
	reim |= smp_bounce.base & 0xffff0000;
	set_smp_reim(reim);

	local_irq_save(flags);

	/* clear reset bit! */
	ctrl = get_smp_ctrl();
	ctrl &= ~(1 << cpu);
	set_smp_ctrl(ctrl);

wait:
	if (!cpumask_test_cpu(cpu, cpu_ready))
		goto wait;

	pr_debug("[SMP] Booting CPU%d ...\n", cpu);
	err = cpu_boot(cpu_logical_map(cpu), __KSTK_TOS(idle),
			(unsigned long)task_thread_info(idle));
	if (err != 0)
		pr_err("start_cpu(%i) returned %i\n" , cpu, err);

	local_irq_restore(flags);
}

/*
 * Common setup before any secondaries are started
 */
static inline int smp_cpu_stop(int cpu)
{
	unsigned int ctrl = get_smp_ctrl();

	/* nr cpu, it depend on hardware */
	if(cpu >= 4)
		return -1;
	ctrl |= 1 << cpu;
	set_smp_ctrl(ctrl);
	return 0;
}

static void __init jzsoc_smp_setup(void)
{
	int i, num;

	cpus_clear(cpu_possible_map);
	cpus_clear(cpu_present_map);

	cpu_set(0, cpu_possible_map);
	cpu_set(0, cpu_present_map);

	__cpu_number_map[0] = 0;
	__cpu_logical_map[0] = 0;

	for (i = 1, num = 0; i < NR_CPUS; i++) {
		if (smp_cpu_stop(i) == 0) {
			cpu_set(i, cpu_possible_map);
			cpu_set(i, cpu_present_map);

			__cpu_number_map[i] = ++num;
			__cpu_logical_map[num] = i;
		}
	}
	pr_info("[SMP] Slave CPU(s) %i available.\n", num);
}

static void __init jzsoc_prepare_cpus(unsigned int max_cpus)
{
	cpu_ready = (cpumask_t*)KSEG1ADDR(&cpu_ready_e);

	pr_debug("[SMP] Prepare %d cpus.\n", max_cpus);
	/* prepare slave cpus entry code */
	build_bounce_code(&boot_sp, &boot_gp);

	/* blast all cache before booting secondary cpu */
	__flush_cache_all();
}

static void send_ipi_msg(const struct cpumask *mask, unsigned int action)
{
	unsigned int msg;
	unsigned long flags = 0;
#define SEND_MSG(CPU)	do {						\
	if(cpu_isset(CPU,*mask)) {				\
		msg = action | get_smp_mbox##CPU();		\
		set_smp_mbox##CPU(msg);				\
	} } while(0)

	spin_lock_irqsave(&smp_lock,flags);

	SEND_MSG(0);
	SEND_MSG(1);
	SEND_MSG(2);
	SEND_MSG(3);

	spin_unlock_irqrestore(&smp_lock,flags);
}

static void jzsoc_send_ipi_single(int cpu, unsigned int action)
{
	send_ipi_msg(cpumask_of(cpu), action);
}

static void jzsoc_send_ipi_mask(const struct cpumask *mask, unsigned int action)
{
	send_ipi_msg(mask, action);
}

void jzsoc_cpus_done(void)
{
}

#ifdef CONFIG_HOTPLUG_CPU
int jzsoc_cpu_disable(void)
{
	struct task_struct *p;
	unsigned int cpu = smp_processor_id();
	if (cpu == 0)		/* FIXME */
		return -EBUSY;

	spin_lock(&smp_lock);
	set_cpu_online(cpu, false);

	blast_dcache32();
	blast_icache32();
	local_flush_tlb_all();

	read_lock(&tasklist_lock);
	for_each_process(p)
		if (p->mm)
			cpumask_clear_cpu(cpu, mm_cpumask(p->mm));
	read_unlock(&tasklist_lock);
	
	spin_unlock(&smp_lock);
	return 0;
}

void jzsoc_cpu_die(unsigned int cpu)
{
	if (cpu == 0)		/* FIXME */
		return;

	printk("jzsoc_cpu_die\n");
	spin_lock(&smp_lock);

	smp_cpu_stop(cpu);

	cpumask_clear_cpu(cpu, &cpu_running);
	cpumask_clear_cpu(cpu, &cpu_start);
	cpumask_clear_cpu(cpu, cpu_ready);

	spin_unlock(&smp_lock);
}
#endif

void play_dead(void)
{
	idle_task_exit();

	while(1);
}

struct plat_smp_ops jzsoc_smp_ops = {
	.send_ipi_single	= jzsoc_send_ipi_single,
	.send_ipi_mask		= jzsoc_send_ipi_mask,
	.init_secondary		= jzsoc_init_secondary,
	.smp_finish		= jzsoc_smp_finish,
	.boot_secondary		= jzsoc_boot_secondary,
	.smp_setup		= jzsoc_smp_setup,
	.cpus_done		= jzsoc_cpus_done,
	.prepare_cpus		= jzsoc_prepare_cpus,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_disable		= jzsoc_cpu_disable,
	.cpu_die		= jzsoc_cpu_die,
#endif
};


#ifdef SMP_DEBUG
static void jzsoc_smp_showregs(void)
{
	int cpu = smp_processor_id();
	unsigned int val;
	printk("CPU%d:\n",cpu);
#define P(reg) do {				\
	val = get_smp_##reg();		\
	printk(#reg ":\t%08x\n",val);	\
} while(0)
	P(ctrl); P(status); P(reim); P(mbox0); P(mbox1);
	//P(val);  P(lock);
	printk("cp0 status:\t%08x\n",read_c0_status());
	printk("cp0 cause:\t%08x\n",read_c0_cause());
	}
#endif

extern void smp_ipi_timer_interrupt(void);
void jzsoc_mbox_interrupt(void)
{
	int cpu = smp_processor_id();
	unsigned int action = 0;

	//kstat_incr_irqs_this_cpu(IRQ_SMP_IPI, irq_to_desc(IRQ_SMP_IPI));
again:
	smp_spinlock();
	switch(cpu) {
#define	CASE_CPU(CPU) case CPU:			\
		action = get_smp_mbox##CPU();	\
		set_smp_mbox##CPU(0);		\
		break

		CASE_CPU(0);
		CASE_CPU(1);
		CASE_CPU(2);
		CASE_CPU(3);
	}
	smp_clr_pending(1<<cpu);
	smp_spinunlock();

	if(!action)
		return;

	/*
	 * Call scheduler_ipi() if SMP_RESCHEDULE_YOURSELF,
	 * we changed here in linux-3.0.8
	 */
	if (action & SMP_RESCHEDULE_YOURSELF)
		scheduler_ipi();
	if (action & SMP_CALL_FUNCTION)
		smp_call_function_interrupt();
	if (action & SMP_IPI_TIMER)
		smp_ipi_timer_interrupt();

	goto again;
}

