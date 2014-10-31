#include <linux/module.h>
#include <linux/mm.h>
#include <linux/syscalls.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <soc/base.h>
#include <soc/cpm.h>
#include <mach/jzcpm_pwc.h>
#include <linux/interrupt.h>

#include "soc_vpu.h"
#include "jz_nvpu.h"
#include "jzm_vpu.h"

struct jz_vpu {
	struct vpu		vpu;
	char			name[16];
	int			irq;
	void __iomem		*iomem;
	struct clk		*clk;
	struct clk		*clk_gate;
	struct completion	done;
	spinlock_t		slock;
	struct mutex		mutex;
	pid_t			owner_pid;
	unsigned int		status;
	unsigned int		bslen;
	void*                   cpm_pwc;
};

static long vpu_reset(struct device *dev)
{
	struct jz_vpu *vpu = dev_get_drvdata(dev);
	int timeout = 0xffffff;
	unsigned int srbc = cpm_inl(CPM_SRBC);

	cpm_set_bit(CPM_VPU_STP(vpu->vpu.id), CPM_SRBC);
#ifdef CONFIG_BOARD_T10_FPGA
	timeout = 0x7fffffff;
	while (!(cpm_inl(CPM_SRBC) & (1 << CPM_VPU_ACK(vpu->vpu.id))));
#else
	while (!(cpm_inl(CPM_SRBC) & (1 << CPM_VPU_ACK(vpu->vpu.id))) && --timeout);
#endif

	if (timeout == 0) {
		dev_warn(vpu->vpu.dev, "[%d:%d] wait stop ack timeout\n",
			 current->tgid, current->pid);
		cpm_outl(srbc, CPM_SRBC);
		return -1;
	} else {
		cpm_outl(srbc | (1 << CPM_VPU_SR(vpu->vpu.id)), CPM_SRBC);
		cpm_outl(srbc, CPM_SRBC);
	}

	return 0;
}


static long vpu_open(struct device *dev)
{
	struct jz_vpu *vpu = dev_get_drvdata(dev);
	unsigned long slock_flag = 0;

	spin_lock_irqsave(&vpu->slock, slock_flag);
	if (cpm_inl(CPM_OPCR) & OPCR_IDLE)
		return -EBUSY;
	clk_enable(vpu->clk);
	clk_enable(vpu->clk_gate);
	//cpm_pwc_enable(vpu->cpm_pwc);

	__asm__ __volatile__ (
			"mfc0  $2, $16,  7   \n\t"
			"ori   $2, $2, 0x340 \n\t"
			"andi  $2, $2, 0x3ff \n\t"
			"mtc0  $2, $16,  7  \n\t"
			"nop                  \n\t");

	vpu_reset(dev);
	enable_irq(vpu->irq);
	spin_unlock_irqrestore(&vpu->slock, slock_flag);

	dev_dbg(dev, "[%d:%d] open\n", current->tgid, current->pid);

	return 0;
}

static long vpu_release(struct device *dev)
{
	struct jz_vpu *vpu = dev_get_drvdata(dev);
	unsigned long slock_flag = 0;

	spin_lock_irqsave(&vpu->slock, slock_flag);
	disable_irq_nosync(vpu->irq);

	__asm__ __volatile__ (
			"mfc0  $2, $16,  7   \n\t"
			"andi  $2, $2, 0xbf \n\t"
			"mtc0  $2, $16,  7  \n\t"
			"nop                  \n\t");

	cpm_clear_bit(CPM_VPU_SR(vpu->vpu.id),CPM_OPCR);
	clk_disable(vpu->clk);
	clk_disable(vpu->clk_gate);
	//cpm_pwc_disable(vpu->cpm_pwc);
	/* Clear completion use_count here to avoid a unhandled irq after vpu off */
	vpu->done.done = 0;
	spin_unlock_irqrestore(&vpu->slock, slock_flag);

	dev_dbg(dev, "[%d:%d] close\n", current->tgid, current->pid);

	return 0;
}

void dump_vpu_registers(struct jz_vpu *vpu)
{
	printk("REG_SCH_GLBC = %x\n", vpu_readl(vpu, REG_SCH_GLBC));
	printk("REG_SCH_TLBC = %x\n", vpu_readl(vpu, REG_SCH_TLBC));
	printk("REG_SCH_TLBV = %x\n", vpu_readl(vpu, REG_SCH_TLBV));
	printk("REG_SCH_TLBA = %x\n", vpu_readl(vpu, REG_SCH_TLBA));
	printk("REG_VDMA_TASKRG = %x\n", vpu_readl(vpu, REG_VDMA_TASKRG));
	printk("REG_EFE_GEOM = %x\n", vpu_readl(vpu, REG_EFE_GEOM));
	printk("REG_EFE_RAWV_SBA = %x\n", vpu_readl(vpu, REG_EFE_RAWV_SBA));
	printk("REG_MCE_CH1_RLUT+4 = %x\n", vpu_readl(vpu, REG_MCE_CH1_RLUT+4));
	printk("REG_MCE_CH2_RLUT+4 = %x\n", vpu_readl(vpu, REG_MCE_CH2_RLUT+4));
	printk("REG_DBLK_GPIC_YA = %x\n", vpu_readl(vpu, REG_DBLK_GPIC_YA));
	printk("REG_DBLK_GPIC_CA = %x\n", vpu_readl(vpu, REG_DBLK_GPIC_CA));
	printk("REG_EFE_RAWY_SBA = %x\n", vpu_readl(vpu, REG_EFE_RAWY_SBA));
	printk("REG_EFE_RAWU_SBA = %x\n", vpu_readl(vpu, REG_EFE_RAWU_SBA));
}

/**** tlb test ****/
int dump_tlb_info(unsigned int tlb_base)
{
	unsigned int pg1_index, pg2_index;
	unsigned int *pg1_vaddr, *pg2_vaddr;
	if(!tlb_base)
		return 0;
	pg1_vaddr = phys_to_virt(tlb_base);
	for (pg1_index=0; pg1_index<1024; pg1_index++) {
		if (!(pg1_vaddr[pg1_index] & 0x1)) {
			printk("pgd dir invalid!\n");
			continue;
		}
		printk("pg1_index:%d, content:0x%08x\n", pg1_index, pg1_vaddr[pg1_index]);
		pg2_vaddr = phys_to_virt(pg1_vaddr[pg1_index] & PAGE_MASK);
		for (pg2_index=0; pg2_index<1024; pg2_index++){
			if((pg2_index%4) == 0)
				printk("\n    pg2_index:%d",pg2_index);
			printk("  0x%08x[0x%08x]",(pg1_index << 22)|(pg2_index << 12), pg2_vaddr[pg2_index]);
		}
	}
	return 0;
}
/**** tlb test end ****/

static long vpu_start_vpu(struct device *dev, const struct channel_node * const cnode)
{
#if 1
	struct jz_vpu *vpu = dev_get_drvdata(dev);
	struct channel_list *clist = list_entry(cnode->clist, struct channel_list, list);

#ifdef DUMP_VPU_REG
	printk("----------------------0000-----------------------\n");
	dump_vpu_registers(vpu);
	printk("---------------------------------------------\n");
#endif
	if (clist->tlb_flag == true) {
#ifdef DUMP_VPU_TLB
		dump_tlb_info(clist->tlb_pidmanager->tlbbase);
#endif
		vpu_writel(vpu, REG_SCH_GLBC, SCH_GLBC_HIAXI | SCH_TLBE_JPGC | SCH_TLBE_DBLK
				| SCH_TLBE_SDE | SCH_TLBE_EFE | SCH_TLBE_VDMA | SCH_TLBE_MCE
				| SCH_INTE_ACFGERR | SCH_INTE_TLBERR | SCH_INTE_BSERR
				| SCH_INTE_ENDF);
		vpu_writel(vpu, REG_SCH_TLBC, SCH_TLBC_INVLD | SCH_TLBC_RETRY);
		vpu_writel(vpu, REG_SCH_TLBV, SCH_TLBV_RCI_MC | SCH_TLBV_RCI_EFE |
				SCH_TLBV_CNM(0x00) | SCH_TLBV_GCN(0x00));
		vpu_writel(vpu, REG_SCH_TLBA, clist->tlb_pidmanager->tlbbase);
	} else {
		vpu_writel(vpu, REG_SCH_GLBC, SCH_GLBC_HIAXI | SCH_INTE_ACFGERR
				| SCH_INTE_BSERR | SCH_INTE_ENDF);
	}
	vpu_writel(vpu, REG_VDMA_TASKRG, VDMA_ACFG_DHA(cnode->dma_addr)
			| VDMA_ACFG_RUN);

	//printk("cnode->dma_addr = %x, vpu->iomem = %p, clist->tlb_flag = %d\n", cnode->dma_addr, vpu->iomem, clist->tlb_flag);
#ifdef DUMP_VPU_REG
	printk("----------------------1111-----------------------\n");
	dump_vpu_registers(vpu);
	printk("---------------------------------------------\n");
#endif
	dev_dbg(vpu->vpu.dev, "[%d:%d] start vpu\n", current->tgid, current->pid);
#endif

	return 0;
}

static long vpu_wait_complete(struct device *dev, struct channel_node * const cnode)
{
	long ret = 0;
	struct jz_vpu *vpu = dev_get_drvdata(dev);

	ret = wait_for_completion_interruptible_timeout(&vpu->done, msecs_to_jiffies(cnode->mdelay));
	if (ret > 0) {
		ret = 0;
		if (cnode->codecdir == ENCODER_DIR) {
			CLEAR_VPU_BIT(vpu, REG_VPU_SDE_STAT, SDE_STAT_BSEND);
			CLEAR_VPU_BIT(vpu, REG_VPU_DBLK_STAT, DBLK_STAT_DOEND);
		}
		dev_dbg(vpu->vpu.dev, "[%d:%d] wait complete\n", current->tgid, current->pid);
	} else {
		dev_warn(dev, "[%d:%d] wait_for_completion timeout\n", current->tgid, current->pid);
		if (vpu_reset(dev) < 0) {
			dev_warn(dev, "vpu reset failed\n");
		}
		ret = -1;
	}
	cnode->output_len = vpu->bslen;
	cnode->status = vpu->status;

	//dev_info(dev, "[file:%s,fun:%s,line:%d] ret = %ld, status = %x, bslen = %d\n", __FILE__, __func__, __LINE__, ret, cnode->status, cnode->output_len);

	return ret;
}

static long vpu_suspend(struct device *dev)
{
	struct jz_vpu *vpu = dev_get_drvdata(dev);
	int timeout = 0xffffff;
	volatile unsigned int vpulock = vpu_readl(vpu, REG_VPU_LOCK);
		//vpu_writel(vpu, REG_SCH_TLBA, clist->tlb_pidmanager->tlbbase);

	if ( vpulock & VPU_LOCK_END_FLAG) {
		while(!(vpu_readl(vpu, REG_VPU_STAT) & VPU_STAT_ENDF) && timeout--);
		if (!timeout) {
			dev_warn(dev, "vpu suspend timeout\n");
			return -1;
		}
		SET_VPU_BIT(vpu, REG_VPU_LOCK, VPU_LOCK_WAIT_OK);
		CLEAR_VPU_BIT(vpu, REG_VPU_LOCK, VPU_LOCK_END_FLAG);
	}

	clk_disable(vpu->clk);
	clk_disable(vpu->clk_gate);

	return 0;
}

static long vpu_resume(struct device *dev)
{
	struct jz_vpu *vpu = dev_get_drvdata(dev);

	clk_set_rate(vpu->clk,300000000);
	clk_enable(vpu->clk);
	clk_enable(vpu->clk_gate);

	return 0;
}

static struct vpu_ops vpu_ops = {
	.owner		= THIS_MODULE,
	.open		= vpu_open,
	.release	= vpu_release,
	.start_vpu	= vpu_start_vpu,
	.wait_complete	= vpu_wait_complete,
	.reset		= vpu_reset,
	.suspend	= vpu_suspend,
	.resume		= vpu_resume,
};

static irqreturn_t vpu_interrupt(int irq, void *dev)
{
	struct jz_vpu *vpu = dev;
	unsigned int vpu_stat;
	unsigned long vflag = 0;

	vpu_stat = vpu_readl(vpu,REG_VPU_STAT);

	spin_lock_irqsave(&vpu->slock, vflag);
	CLEAR_VPU_BIT(vpu, REG_SCH_GLBC, SCH_INTE_MASK);
	if(vpu_stat) {
		if(vpu_stat & VPU_STAT_ENDF) {
			if(vpu_stat & VPU_STAT_JPGEND) {
				dev_dbg(vpu->vpu.dev, "JPG successfully done!\n");
				vpu->bslen = vpu_readl(vpu, REG_VPU_JPGC_STAT) & 0xffffff;
				CLEAR_VPU_BIT(vpu,REG_VPU_JPGC_STAT,JPGC_STAT_ENDF);
			} else {
				dev_dbg(vpu->vpu.dev, "SCH successfully done!\n");
				vpu->bslen = vpu_readl(vpu, REG_VPU_ENC_LEN);
				CLEAR_VPU_BIT(vpu,REG_VPU_SDE_STAT,SDE_STAT_BSEND);
				CLEAR_VPU_BIT(vpu,REG_VPU_DBLK_STAT,DBLK_STAT_DOEND);
			}
			vpu->status = vpu_stat;
			complete(&vpu->done);
		} else {
			check_vpu_status(VPU_STAT_SLDERR, "SHLD error!\n");
			check_vpu_status(VPU_STAT_TLBERR, "TLB error! Addr is 0x%08x\n",
					 vpu_readl(vpu,REG_VPU_STAT));
			check_vpu_status(VPU_STAT_BSERR, "BS error!\n");
			check_vpu_status(VPU_STAT_ACFGERR, "ACFG error!\n");
			check_vpu_status(VPU_STAT_TIMEOUT, "TIMEOUT error!\n");
			CLEAR_VPU_BIT(vpu,REG_VPU_GLBC,
				      (VPU_INTE_ACFGERR |
				       VPU_INTE_TLBERR |
				       VPU_INTE_BSERR |
				       VPU_INTE_ENDF
					      ));
			vpu->bslen = 0;
			vpu->status = vpu_stat;
		}
	} else {
		if(vpu_readl(vpu,REG_VPU_AUX_STAT) & AUX_STAT_MIRQP) {
			dev_dbg(vpu->vpu.dev, "AUX successfully done!\n");
			CLEAR_VPU_BIT(vpu,REG_VPU_AUX_STAT,AUX_STAT_MIRQP);
		} else {
			dev_dbg(vpu->vpu.dev, "illegal interrupt happened!\n");
		}
	}

	spin_unlock_irqrestore(&vpu->slock, vflag);

	return IRQ_HANDLED;
}

static int vpu_probe(struct platform_device *pdev)
{
	int ret;
	struct resource	*regs;
	struct jz_vpu *vpu;

	vpu = kzalloc(sizeof(struct jz_vpu), GFP_KERNEL);
	if (!vpu) {
		dev_err(&pdev->dev, "kzalloc vpu space failed\n");
		ret = -ENOMEM;
		goto err_kzalloc_vpu;
	}

	vpu->vpu.id = pdev->id;
	sprintf(vpu->name, "vpu%d", pdev->id);

	vpu->irq = platform_get_irq(pdev, 0);
	if(vpu->irq < 0) {
		dev_err(&pdev->dev, "get irq failed\n");
		ret = vpu->irq;
		goto err_get_vpu_irq;
	}

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs) {
		dev_err(&pdev->dev, "No iomem resource\n");
		ret = -ENXIO;
		goto err_get_vpu_resource;
	}

	vpu->iomem = ioremap(regs->start, resource_size(regs));
	if (!vpu->iomem) {
		dev_err(&pdev->dev, "ioremap failed\n");
		ret = -ENXIO;
		goto err_get_vpu_iomem;
	}

	vpu->clk_gate = clk_get(&pdev->dev, "vpu");
	if (IS_ERR(vpu->clk_gate)) {
		ret = PTR_ERR(vpu->clk_gate);
		goto err_get_vpu_clk_gate;
	}

	vpu->clk = clk_get(&pdev->dev,"cgu_vpu");
	if (IS_ERR(vpu->clk)) {
		ret = PTR_ERR(vpu->clk);
		goto err_get_vpu_clk_cgu;
	}

	clk_set_rate(vpu->clk,300000000);

	spin_lock_init(&vpu->slock);
	mutex_init(&vpu->mutex);
	init_completion(&vpu->done);

	ret = request_irq(vpu->irq, vpu_interrupt, IRQF_DISABLED, vpu->name, vpu);
	if (ret < 0) {
		dev_err(&pdev->dev, "request_irq failed\n");
		goto err_vpu_request_irq;
	}
	disable_irq_nosync(vpu->irq);
#if 0
	vpu->cpm_pwc = cpm_pwc_get(PWC_VPU);
	if(!vpu->cpm_pwc) {
		dev_err(&pdev->dev, "get %s fail!\n",PWC_VPU);
		goto err_vpu_request_power;
	}
#endif

	vpu->vpu.dev = &pdev->dev;
	vpu->vpu.ops = &vpu_ops;

	if ((ret = vpu_register(&vpu->vpu.vlist)) < 0) {
		goto err_vpu_register;
	}
	platform_set_drvdata(pdev, vpu);

	return 0;

err_vpu_register:
#if 0
	cpm_pwc_put(vpu->cpm_pwc);
err_vpu_request_power:
#endif
	free_irq(vpu->irq, vpu);
err_vpu_request_irq:
	clk_put(vpu->clk);
err_get_vpu_clk_cgu:
	clk_put(vpu->clk_gate);
err_get_vpu_clk_gate:
	iounmap(vpu->iomem);
err_get_vpu_iomem:
err_get_vpu_resource:
err_get_vpu_irq:
	kfree(vpu);
err_kzalloc_vpu:
	return ret;
}

static int vpu_remove(struct platform_device *dev)
{
	struct jz_vpu *vpu = platform_get_drvdata(dev);

	vpu_unregister(&vpu->vpu.vlist);
	//cpm_pwc_put(vpu->cpm_pwc);
	free_irq(vpu->irq, vpu);
	clk_put(vpu->clk);
	clk_put(vpu->clk_gate);
	iounmap(vpu->iomem);
	kfree(vpu);

	return 0;
}

int vpu_suspend_platform(struct platform_device *dev, pm_message_t state)
{
	struct jz_vpu *vpu = platform_get_drvdata(dev);

	return vpu_suspend(vpu->vpu.dev);
}

int vpu_resume_platform(struct platform_device *dev)
{
	struct jz_vpu *vpu = platform_get_drvdata(dev);

	return vpu_resume(vpu->vpu.dev);
}

static struct platform_driver jz_vpu_driver = {
	.probe		= vpu_probe,
	.remove		= vpu_remove,
	.driver		= {
		.name	= "jz-vpu",
	},
	.suspend	= vpu_suspend_platform,
	.resume		= vpu_resume_platform,
};

static int __init vpu_init(void)
{
	return platform_driver_register(&jz_vpu_driver);
}

static void __exit vpu_exit(void)
{
	platform_driver_unregister(&jz_vpu_driver);
}

module_init(vpu_init);
module_exit(vpu_exit);

MODULE_DESCRIPTION("JZ4780 VPU driver");
MODULE_AUTHOR("Justin <ptkang@ingenic.cn>");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("20140830");
