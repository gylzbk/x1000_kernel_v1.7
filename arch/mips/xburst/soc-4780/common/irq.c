/*
 * Interrupt controller.
 *
 * Copyright (c) 2006-2007  Ingenic Semiconductor Inc.
 * Author: <lhhuang@ingenic.cn>
 *
 *  This program is free software; you can redistribute	 it and/or modify it
 *  under  the terms of	 the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the	License, or (at your
 *  option) any later version.
 */
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/bitops.h>

#include <asm/irq_cpu.h>

#include <soc/base.h>
#include <soc/irq.h>
#include <soc/ost.h>

#include <smp_cp0.h>

#define TRACE_IRQ        1
#define PART_OFF	0x20

#define ISR_OFF		(0x00)
#define IMR_OFF		(0x04)
#define IMSR_OFF	(0x08)
#define IMCR_OFF	(0x0c)
#define IPR_OFF		(0x10)

#define regr(off) 	inl(OST_IOBASE + (off))
#define regw(val,off)	outl(val, OST_IOBASE + (off))

static void __iomem *intc_base;
static unsigned long intc_saved[2];
static unsigned long intc_wakeup[2];

extern void __enable_irq(struct irq_desc *desc, unsigned int irq, bool resume);

static void intc_irq_ctrl(struct irq_data *data, int msk, int wkup)
{
	int intc = (int)irq_data_get_irq_chip_data(data);
	void *base = intc_base + PART_OFF * (intc/32);

	if (msk == 1)
		writel(BIT(intc%32), base + IMSR_OFF);
	else if (msk == 0)
		writel(BIT(intc%32), base + IMCR_OFF);

	if (wkup == 1)
		intc_wakeup[intc / 32] |= 1 << (intc % 32);
	else if (wkup == 0)
		intc_wakeup[intc / 32] &= ~(1 << (intc % 32));
}

static void intc_irq_unmask(struct irq_data *data)
{
	intc_irq_ctrl(data, 0, -1);
}

static void intc_irq_mask(struct irq_data *data)
{
	intc_irq_ctrl(data, 1, -1);
}

static int intc_irq_set_wake(struct irq_data *data, unsigned int on)
{
	intc_irq_ctrl(data, -1, !!on);
	return 0;
}
static unsigned int cpu_irq_affinity[NR_CPUS * 2];
static unsigned int cpu_irq_unmask[NR_CPUS * 2];

static inline void set_intc_cpu(unsigned long irq_num,long cpu) {
	int mask,i;
	int num = irq_num / 32;
	mask = 1 << (irq_num % 32);
	cpu_irq_affinity[num] |= mask;
	for(i = 0;i < NR_CPUS;i++) {
		if(i != cpu)
			cpu_irq_unmask[i * NR_CPUS + num] &= ~mask;
	}
	for(i = 0;i < NR_CPUS;i++) {
		printk("cpu %d cpu_irq_unmask[%d] = 0x%08x\n",i,i * NR_CPUS + 0,cpu_irq_unmask[i * NR_CPUS + 0]);
		printk("cpu %d cpu_irq_unmask[%d] = 0x%08x\n",i,i * NR_CPUS + 1,cpu_irq_unmask[i * NR_CPUS + 1]);
	}
}
static inline void init_intc_affinity(void) {
	int i;
	for(i = 0;i < NR_CPUS;i++) {
		cpu_irq_unmask[i * NR_CPUS + 0] = 0xffffffff;
		cpu_irq_unmask[i * NR_CPUS + 1] = 0xffffffff;
		cpu_irq_affinity[i * NR_CPUS + 0] = 0;
		cpu_irq_affinity[i * NR_CPUS + 1] = 0;
	}
}
#ifdef CONFIG_SMP
static int intc_set_affinity(struct irq_data *data, const struct cpumask *dest, bool force) {
	long i,cpu;
	unsigned int irq = data->irq;
	i = cpumask_first(dest);

	/* Convert logical CPU to physical CPU */
	cpu = cpu_logical_map(i);
	printk("intc_set_affinity = %d irq: %d i:%ld cpu:%ld\n",smp_processor_id(),irq,i,cpu);
	return 0;
}
#endif
static struct irq_chip jzintc_chip = {
	.name 		= "jz-intc",
	.irq_mask	= intc_irq_mask,
	.irq_mask_ack 	= intc_irq_mask,
	.irq_unmask 	= intc_irq_unmask,
	.irq_set_wake 	= intc_irq_set_wake,
#ifdef CONFIG_SMP
	.irq_set_affinity = intc_set_affinity,
#endif
};

#ifdef CONFIG_SMP
extern void jzsoc_mbox_interrupt(void);
static irqreturn_t ipi_reschedule(int irq, void *d)
{
	scheduler_ipi();
	return IRQ_HANDLED;
}

static irqreturn_t ipi_call_function(int irq, void *d)
{
	smp_call_function_interrupt();
	return IRQ_HANDLED;
}

static int setup_ipi(void)
{
	if (request_irq(IRQ_SMP_RESCHEDULE_YOURSELF, ipi_reschedule, IRQF_DISABLED,
				"ipi_reschedule", NULL))
		BUG();
	if (request_irq(IRQ_SMP_CALL_FUNCTION, ipi_call_function, IRQF_DISABLED,
				"ipi_call_function", NULL))
		BUG();

	set_c0_status(STATUSF_IP3);
	return 0;
}


#endif

static void ost_irq_unmask(struct irq_data *data)
{
	regw(0xffffffff,  OST_TMCR);
}

static void ost_irq_mask(struct irq_data *data)
{
	regw(0xffffffff,  OST_TMSR);
}

static void ost_irq_mask_ack(struct irq_data *data)
{
	regw(0xffffffff,  OST_TMSR);
	regw(0xffffffff,  OST_TFCR);  /* clear ost flag */
}

static struct irq_chip ost_irq_type = {
	.name 		= "ost",
	.irq_mask	= ost_irq_mask,
	.irq_mask_ack 	= ost_irq_mask_ack,
	.irq_unmask 	= ost_irq_unmask,
};

void __init arch_init_irq(void)
{
	int i;

	clear_c0_status(0xff04); /* clear ERL */
	set_c0_status(0x0400);   /* set IP2 */

	/* Set up MIPS CPU irq */
	mips_cpu_irq_init();

	/* Set up INTC irq */
	intc_base = ioremap(INTC_IOBASE, 0xfff);

	writel(0xffffffff, intc_base + IMSR_OFF);
	writel(0xffffffff, intc_base + PART_OFF + IMSR_OFF);
	for (i = IRQ_INTC_BASE; i < IRQ_INTC_BASE + INTC_NR_IRQS; i++) {
		irq_set_chip_data(i, (void *)(i - IRQ_INTC_BASE));
		irq_set_chip_and_handler(i, &jzintc_chip, handle_level_irq);
	}

	for (i = IRQ_OST_BASE; i < IRQ_OST_BASE + OST_NR_IRQS; i++) {
		irq_set_chip_data(i, (void *)(i - IRQ_OST_BASE));
		irq_set_chip_and_handler(i, &ost_irq_type, handle_level_irq);
	}
	init_intc_affinity();
	set_intc_cpu(26,0);
	set_intc_cpu(27,1);
	set_intc_cpu(25,0);
	/* enable cpu interrupt mask */
	set_c0_status(IE_IRQ0 | IE_IRQ1);

#ifdef CONFIG_SMP
	setup_ipi();
#endif
	return;
}
/*
	if(cpuid == 1)
	{
		printk("cpu = %ld 0x%08x 0x%08x\n",cpuid,readl(intc_base + IPR_OFF),cpu_irq_unmask[cpuid * NR_CPUS]);
	}
*/
static void intc_irq_dispatch(void)
{
	unsigned long ipr[2];
	unsigned long cpuid = smp_processor_id();
	ipr[0] = readl(intc_base + IPR_OFF) & cpu_irq_unmask[cpuid * NR_CPUS];
	ipr[1] = readl(intc_base + PART_OFF + IPR_OFF) & cpu_irq_unmask[cpuid * NR_CPUS + 1];
	if (ipr[0]) {
		do_IRQ(ffs(ipr[0]) -1 +IRQ_INTC_BASE);
	}
	if (ipr[1]) {
		do_IRQ(ffs(ipr[1]) +31 +IRQ_INTC_BASE);
	}
#ifdef CONFIG_SMP
	switch_cpu_irq(cpuid);
#endif
}

asmlinkage void plat_irq_dispatch(void)
{
	unsigned int cause = read_c0_cause();
	unsigned int pending = cause & read_c0_status() & ST0_IM;
	if (cause & CAUSEF_IP4) {
		do_IRQ(IRQ_OST);
	}
#ifdef CONFIG_SMP
	if(pending & CAUSEF_IP3) {
		jzsoc_mbox_interrupt();
	}
#endif
	if(pending & CAUSEF_IP2)
		intc_irq_dispatch();
	cause = read_c0_cause();
	pending = cause & read_c0_status() & ST0_IM;
}

void arch_suspend_disable_irqs(void)
{
	int i,j,irq;
	struct irq_desc *desc;

	local_irq_disable();

	intc_saved[0] = readl(intc_base + IMR_OFF);
	intc_saved[1] = readl(intc_base + PART_OFF + IMR_OFF);

	writel(0xffffffff & ~intc_wakeup[0], intc_base + IMSR_OFF);
	writel(0xffffffff & ~intc_wakeup[1], intc_base + PART_OFF + IMSR_OFF);

	for(j=0;j<2;j++) {
		for(i=0;i<32;i++) {
			if(intc_wakeup[j] & (0x1<<i)) {
				irq = i + IRQ_INTC_BASE + 32*j;
				desc = irq_to_desc(irq);
				__enable_irq(desc, irq, true);
			}
		}
	}
}

void arch_suspend_enable_irqs(void)
{
	writel(0xffffffff & ~intc_saved[0], intc_base + IMCR_OFF);
	writel(0xffffffff & ~intc_saved[1], intc_base + PART_OFF + IMCR_OFF);
	local_irq_enable();
}
