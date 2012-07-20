/**
 * xb_snd_pcm0.c
 *
 * jbbi <jbbi@ingenic.cn>
 *
 * 24 APR 2012
 *
 */

#include <linux/init.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/clk.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/sound.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <linux/switch.h>
#include <linux/dma-mapping.h>
#include <linux/soundcard.h>
#include <linux/earlysuspend.h>
#include <mach/jzdma.h>
#include <mach/jzsnd.h>
#include <soc/irq.h>
#include <soc/base.h>
#include "xb47xx_pcm0.h"
/**
 * global variable
 **/
void volatile __iomem *volatile pcm0_iomem;

static spinlock_t pcm0_irq_lock;
static struct dsp_endpoints pcm0_endpoints;

#define JZ4780_CPM_PCM_SYSCLK 12000000
static struct pcm0_board_info
{
	unsigned long rate;
	unsigned long replay_format;
	unsigned long record_format;
	unsigned long pcmclk;
	unsigned int irq;
	struct dsp_endpoints *endpoint;
}*pcm_priv;

/*##################################################################*\
 |* suspand func
\*##################################################################*/
static int pcm0_suspend(struct platform_device *, pm_message_t state);
static int pcm0_resume(struct platform_device *);
static void pcm0_shutdown(struct platform_device *);

#ifdef CONFIG_ANDROID
static int is_pcm0_suspended = 0;

static void pcm0_late_resume(struct early_suspend *h)
{
	is_pcm0_suspended = 0;
}

static struct early_suspend jz_i2s_early_suspend = {
	.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN,
	.resume = pcm0_late_resume,
};
#endif
/*##################################################################*\
|* dev_ioctl
\*##################################################################*/
static int pcm0_set_fmt(unsigned long *format,int mode)
{/*??*/
	int ret = 0;
	int data_width = 0;

    /*
	 * The value of format reference to soundcard.
	 * AFMT_MU_LAW      0x00000001
	 * AFMT_A_LAW       0x00000002
	 * AFMT_IMA_ADPCM   0x00000004
	 * AFMT_U8			0x00000008
	 * AFMT_S16_LE      0x00000010
	 * AFMT_S16_BE      0x00000020
	 * AFMT_S8			0x00000040
	 */

	switch (*format) {

	case AFMT_U8:
	case AFMT_S8:
		*format = AFMT_U8;
		data_width = 8;
		if (mode & CODEC_WMODE)
			__pcm0_set_oss_sample_size(0);
		if (mode & CODEC_RMODE)
			__pcm0_set_iss_sample_size(0);
		break;
	case AFMT_S16_LE:
	case AFMT_S16_BE:
		data_width = 16;
		*format = AFMT_S16_LE;
		if (mode & CODEC_WMODE)
			__pcm0_set_oss_sample_size(1);
		if (mode & CODEC_RMODE)
			__pcm0_set_iss_sample_size(1);
		break;
	default :
		printk("PCM0: there is unknown format 0x%x.\n",(unsigned int)*format);
		return -EINVAL;
	}
	if (mode & CODEC_WMODE)
		if (pcm_priv->replay_format != *format) {
			pcm_priv->replay_format = *format;
			ret |= NEED_RECONF_TRIGGER | NEED_RECONF_FILTER;
			ret |= NEED_RECONF_DMA;
		}

	if (mode & CODEC_RMODE)
		if (pcm_priv->record_format != *format) {
			pcm_priv->record_format = *format;
			ret |= NEED_RECONF_TRIGGER | NEED_RECONF_FILTER;
			ret |= NEED_RECONF_DMA;
		}

	return ret;
}

static int pcm0_set_channel(int* channel,int mode)
{
	return 0;
}

static int pcm0_set_rate(unsigned long *rate)
{
	unsigned long div;
	if (!pcm_priv)
		return -ENODEV;
	div = pcm_priv->pcmclk/(8*(*rate)) - 1;
	if (div >= 0 && div < 32) {
		__pcm0_set_syndiv(div);
		*rate = pcm_priv->pcmclk/(8*(div+1));
		pcm_priv->rate = *rate;
	} else
		*rate = pcm_priv->rate;
	return 0;
}

static int pcm0_set_pcmclk(unsigned long pcmclk)
{
	unsigned long div;
	if (!pcm_priv)
		return -ENODEV;
	div = JZ4780_CPM_PCM_SYSCLK/pcmclk - 1;
	if (div >= 0 && div < 64) {
		__pcm0_set_clkdiv(div);
		pcm_priv->pcmclk = JZ4780_CPM_PCM_SYSCLK/(div + 1);
	} else
		return -EINVAL;
	return 0;
}

#define PCM0_FIFO_DEPTH		16

static int get_burst_length(unsigned long val)
{
	/* burst bytes for 1,2,4,8,16,32,64 bytes */
	int ord;

	ord = ffs(val) - 1;
	if (ord < 0)
		ord = 0;
	else if (ord > 6)
		ord = 6;

	/* if tsz == 8, set it to 4 */
	return (ord == 3 ? 4 : 1 << ord)*8;
}

static void pcm0_set_trigger(int mode)
{
	int data_width = 0;
	struct dsp_pipe *dp = NULL;
	int burst_length = 0;

	if (!pcm_priv)
		return;

	if (mode & CODEC_WMODE) {
		switch(pcm_priv->replay_format) {
		case AFMT_U8:
			data_width = 8;
			break;
		case AFMT_S16_LE:
			data_width = 16;
			break;
		}
		dp = pcm_priv->endpoint->out_endpoint;
		burst_length = get_burst_length((int)dp->paddr|(int)dp->fragsize|
				dp->dma_config.src_maxburst|dp->dma_config.dst_maxburst);
		if (PCM0_FIFO_DEPTH - burst_length * 8/data_width >= 12)
			__pcm0_set_transmit_trigger(6);
		else
			__pcm0_set_transmit_trigger((PCM0_FIFO_DEPTH - burst_length/data_width) >> 1);

	}
	if (mode &CODEC_RMODE) {
		switch(pcm_priv->record_format) {
		case AFMT_U8:
			data_width = 8;
			break;
		case AFMT_S16_LE:
			data_width = 16;
			break;
		}
		dp = pcm_priv->endpoint->in_endpoint;
		burst_length = get_burst_length((int)dp->paddr|(int)dp->fragsize|
				dp->dma_config.src_maxburst|dp->dma_config.dst_maxburst);
		__pcm0_set_receive_trigger(((PCM0_FIFO_DEPTH - burst_length/data_width) >> 1) - 1);
	}

	return;
}

static int pcm0_enable(int mode)
{
	unsigned long rate = 44100;
	unsigned long replay_format = 16;
	unsigned long record_format = 16;
	int replay_channel = 2;
	int record_channel = 2;
	struct dsp_pipe *dp_other = NULL;
	if (!pcm_priv)
			return -ENODEV;

	pcm0_set_rate(&rate);
	if (mode & CODEC_WMODE) {
		dp_other = pcm_priv->endpoint->in_endpoint;
		pcm0_set_fmt(&replay_format,mode);
		pcm0_set_channel(&replay_channel,mode);
		pcm0_set_trigger(mode);
	}
	if (mode & CODEC_RMODE) {
		dp_other = pcm_priv->endpoint->out_endpoint;
		if (!dp_other->is_used) {
			pcm0_set_fmt(&record_format,mode);
			pcm0_set_channel(&record_channel,mode);
			pcm0_set_trigger(mode);
		}
	}

	if (!dp_other->is_used) {
		/*avoid pop FIXME*/
		if (mode & CODEC_WMODE)
			__pcm0_flush_fifo();
		__pcm0_enable();
		__pcm0_clock_enable();
	}
	return 0;
}

static int pcm0_disable(int mode)			//CHECK codec is suspend?
{
	if (mode & CODEC_WMODE) {
		__pcm0_disable_transmit_dma();
		__pcm0_disable_replay();
	}
	if (mode & CODEC_RMODE) {
		__pcm0_disable_receive_dma();
		__pcm0_disable_record();
	}
	__pcm0_disable();
	__pcm0_clock_disable();
	return 0;
}


static int pcm0_dma_enable(int mode)		//CHECK
{
	int val;
	if (!pcm_priv)
			return -ENODEV;
	if (mode & CODEC_WMODE) {
		__pcm0_disable_transmit_dma();
		__pcm0_enable_transmit_dma();
		__pcm0_enable_replay();
	}
	if (mode & CODEC_RMODE) {
		__pcm0_flush_fifo();
		__pcm0_enable_record();
		/* read the first sample and ignore it */
		val = __pcm0_read_fifo();
		__pcm0_enable_receive_dma();
	}

	return 0;
}

static int pcm0_dma_disable(int mode)		//CHECK seq dma and func
{
	if (mode & CODEC_WMODE) {
		__pcm0_disable_transmit_dma();
		__pcm0_disable_replay();
	}
	if (mode & CODEC_RMODE) {
		__pcm0_disable_receive_dma();
		__pcm0_disable_record();
	}
	return 0;
}

static int pcm0_get_fmt_cap(unsigned long *fmt_cap)
{
	*fmt_cap |= AFMT_S16_LE|AFMT_U8;
	return 0;
}


static int pcm0_get_fmt(unsigned long *fmt, int mode)
{
	if (!pcm_priv)
			return -ENODEV;

	if (mode & CODEC_WMODE)
		*fmt = pcm_priv->replay_format;
	if (mode & CODEC_RMODE)
		*fmt = pcm_priv->record_format;

	return 0;
}

static void pcm0_dma_need_reconfig(int mode)
{
	struct dsp_pipe	*dp = NULL;

	if (!pcm_priv)
			return;
	if (mode & CODEC_WMODE) {
		dp = pcm_priv->endpoint->out_endpoint;
		dp->need_reconfig_dma = true;
	}
	if (mode & CODEC_RMODE) {
		dp = pcm_priv->endpoint->in_endpoint;
		dp->need_reconfig_dma = true;
	}
	return;
}

/********************************************************\
 * dev_ioctl
\********************************************************/
static long pcm0_ioctl(unsigned int cmd, unsigned long arg)
{
	long ret = 0;

	switch (cmd) {
	case SND_DSP_ENABLE_REPLAY:
		/* enable pcm0 record */
		/* set pcm0 default record format, channels, rate */
		/* set default replay route */
		ret = pcm0_enable(CODEC_WMODE);
		break;

	case SND_DSP_DISABLE_REPLAY:
		/* disable pcm0 replay */
		ret = pcm0_disable(CODEC_WMODE);
		break;

	case SND_DSP_ENABLE_RECORD:
		/* enable pcm0 record */
		/* set pcm0 default record format, channels, rate */
		/* set default record route */
		ret = pcm0_enable(CODEC_RMODE);
		break;

	case SND_DSP_DISABLE_RECORD:
		/* disable pcm0 record */
		ret = pcm0_disable(CODEC_RMODE);
		break;

	case SND_DSP_ENABLE_DMA_RX:
		ret = pcm0_dma_enable(CODEC_RMODE);
		break;

	case SND_DSP_DISABLE_DMA_RX:
		ret = pcm0_dma_disable(CODEC_RMODE);
		break;

	case SND_DSP_ENABLE_DMA_TX:
		ret = pcm0_dma_enable(CODEC_WMODE);
		break;

	case SND_DSP_DISABLE_DMA_TX:
		ret = pcm0_dma_disable(CODEC_WMODE);
		break;

	case SND_DSP_SET_REPLAY_RATE:
	case SND_DSP_SET_RECORD_RATE:
		ret = pcm0_set_rate((unsigned long *)arg);
		break;

	case SND_DSP_SET_REPLAY_CHANNELS:
	case SND_DSP_SET_RECORD_CHANNELS:
		/* set record channels */
		ret = pcm0_set_channel((int*)arg,CODEC_RMODE);
		break;

	case SND_DSP_GET_REPLAY_FMT_CAP:
	case SND_DSP_GET_RECORD_FMT_CAP:
		/* return the support record formats */
		ret = pcm0_get_fmt_cap((unsigned long *)arg);
		break;

	case SND_DSP_GET_REPLAY_FMT:
		/* get current replay format */
		pcm0_get_fmt((unsigned long *)arg,CODEC_WMODE);
		break;

	case SND_DSP_SET_REPLAY_FMT:
		/* set replay format */
		ret = pcm0_set_fmt((unsigned long *)arg,CODEC_WMODE);
		if (ret < 0)
			break;
		/* if need reconfig the trigger, reconfig it */
		if (ret & NEED_RECONF_TRIGGER)
			pcm0_set_trigger(CODEC_WMODE);
		/* if need reconfig the dma_slave.max_tsz, reconfig it and
		   set the dp->need_reconfig_dma as true */
		if (ret & NEED_RECONF_DMA)
			pcm0_dma_need_reconfig(CODEC_WMODE);
		/* if need reconfig the filter, reconfig it */
		if (ret & NEED_RECONF_FILTER)
			;
		ret = 0;
		break;

	case SND_DSP_GET_RECORD_FMT:
		/* get current record format */
		pcm0_get_fmt((unsigned long *)arg,CODEC_RMODE);

		break;

	case SND_DSP_SET_RECORD_FMT:
		/* set record format */
		ret = pcm0_set_fmt((unsigned long *)arg,CODEC_RMODE);
		if (ret < 0)
			break;
		/* if need reconfig the trigger, reconfig it */
		if (ret & NEED_RECONF_TRIGGER)
			pcm0_set_trigger(CODEC_RMODE);
		/* if need reconfig the dma_slave.max_tsz, reconfig it and
		   set the dp->need_reconfig_dma as true */
		if (ret & NEED_RECONF_DMA)
			pcm0_dma_need_reconfig(CODEC_RMODE);
		/* if need reconfig the filter, reconfig it */
		if (ret & NEED_RECONF_FILTER)
			;
		ret = 0;
		break;

	default:
		printk("SOUND_ERROR: %s(line:%d) unknown command!",
				__func__, __LINE__);
		ret = -EINVAL;
	}

	return ret;
}

/*##################################################################*\
|* functions
\*##################################################################*/
static irqreturn_t pcm0_irq_handler(int irq, void *dev_id)
{
	unsigned long flags;
	irqreturn_t ret = IRQ_HANDLED;

	spin_lock_irqsave(&pcm0_irq_lock,flags);

	spin_unlock_irqrestore(&pcm0_irq_lock,flags);

	return ret;
}

static int pcm0_init_pipe(struct dsp_pipe **dp , enum dma_data_direction direction,unsigned long iobase)
{
	if (*dp != NULL || dp == NULL)
		return 0;
	*dp = vmalloc(sizeof(struct dsp_pipe));
	if (*dp == NULL) {
		printk("pcm0 : init pipe fail vmalloc ");
		return -ENOMEM;
	}

	(*dp)->dma_config.direction = direction;
	(*dp)->dma_config.src_addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
	(*dp)->dma_config.dst_addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
	(*dp)->dma_type = JZDMA_REQ_PCM0;

	(*dp)->fragsize = FRAGSIZE_M;
	(*dp)->fragcnt = FRAGCNT_M;
	(*dp)->is_non_block = true;
	(*dp)->is_used = false;
	(*dp)->can_mmap =true;
	INIT_LIST_HEAD(&((*dp)->free_node_list));
	INIT_LIST_HEAD(&((*dp)->use_node_list));

	if (direction == DMA_TO_DEVICE) {
		(*dp)->dma_config.src_maxburst = 16;
		(*dp)->dma_config.dst_maxburst = 16;
		(*dp)->dma_config.dst_addr = iobase + PCMDP0;
		(*dp)->dma_config.src_addr = 0;
	} else if (direction == DMA_FROM_DEVICE) {
		(*dp)->dma_config.src_maxburst = 16;
		(*dp)->dma_config.dst_maxburst = 16;
		(*dp)->dma_config.src_addr = iobase + PCMDP0;
		(*dp)->dma_config.dst_addr = 0;
	} else
		return -1;

	return 0;
}


static int pcm0_global_init(struct platform_device *pdev)
{
	int ret = 0;
	struct resource *pcm0_resource = NULL;
	struct dsp_pipe *pcm0_pipe_out = NULL;
	struct dsp_pipe *pcm0_pipe_in = NULL;
	struct clk *pcm_sysclk = NULL;
	struct clk *pcmclk = NULL;

	pcm0_resource = platform_get_resource(pdev,IORESOURCE_MEM,0);
	if (pcm0_resource == NULL) {
		printk("pcm0: platform_get_resource fail.\n");
		return -1;
	}

	/* map io address */
	if (!request_mem_region(pcm0_resource->start, resource_size(pcm0_resource), pdev->name)) {
		printk("pcm0: mem region fail busy .\n");
		return -EBUSY;
	}
	pcm0_iomem = ioremap(pcm0_resource->start, resource_size(pcm0_resource));
	if (!pcm0_iomem) {
		printk ("pcm0: ioremap fail.\n");
		ret =  -ENOMEM;
		goto __err_ioremap;
	}

	ret = pcm0_init_pipe(&pcm0_pipe_out,DMA_TO_DEVICE,pcm0_resource->start);
	if (ret < 0)
		goto __err_init_pipeout;
	ret = pcm0_init_pipe(&pcm0_pipe_in,DMA_FROM_DEVICE,pcm0_resource->start);
	if (ret < 0)
		goto __err_init_pipein;

	pcm0_endpoints.out_endpoint = pcm0_pipe_out;
	pcm0_endpoints.in_endpoint = pcm0_pipe_in;

	/* request aic clk FIXME*/
	pcm_sysclk = clk_get(&pdev->dev, "pcm");
	if (!IS_ERR(pcm_sysclk))
		clk_enable(pcm_sysclk);

	spin_lock_init(&pcm0_irq_lock);
	/* request irq */
	pcm0_resource = platform_get_resource(pdev,IORESOURCE_IRQ,0);
	if (pcm0_resource == NULL) {
		ret = -1;
		goto __err_irq;
	}
	/*FIXME share irq*/
	pcm_priv->irq = pcm0_resource->start;
	ret = request_irq(pcm0_resource->start, pcm0_irq_handler,
					  IRQF_SHARED, "pcm_irq", "pcm0");
	if (ret < 0)
		goto __err_irq;


	/*FIXME set sysclk output for codec*/
	pcmclk = clk_get(&pdev->dev,"cgu_pcm");
	if (!IS_ERR(pcmclk)) {
		clk_set_rate(pcmclk,JZ4780_CPM_PCM_SYSCLK);
		if (clk_get_rate(pcmclk) > JZ4780_CPM_PCM_SYSCLK) {
			printk("codec interface set rate fail.\n");
			goto __err_pcmclk;
		}
		clk_enable(pcmclk);
	}

	__pcm0_as_slaver();

	pcm0_set_pcmclk(12000000);
	__pcm0_disable_receive_dma();
	__pcm0_disable_transmit_dma();
	__pcm0_disable_record();
	__pcm0_disable_replay();
	__pcm0_flush_fifo();
	__pcm0_clear_ror();
	__pcm0_clear_tur();
	__pcm0_set_receive_trigger(3);
	__pcm0_set_transmit_trigger(4);
	__pcm0_disable_overrun_intr();
	__pcm0_disable_underrun_intr();
	__pcm0_disable_transmit_intr();
	__pcm0_disable_receive_intr();
	/* play zero or last sample when underflow */
	__pcm0_play_lastsample();
	//__pcm0_enable();

	return 0;

__err_pcmclk:
	clk_put(pcmclk);
	free_irq(pcm_priv->irq,NULL);
__err_irq:
	clk_disable(pcmclk);
	clk_put(pcm_sysclk);
	vfree(pcm0_pipe_in);
__err_init_pipein:
	vfree(pcm0_pipe_out);
__err_init_pipeout:
	iounmap(pcm0_iomem);
__err_ioremap:
	release_mem_region(pcm0_resource->start,resource_size(pcm0_resource));
	return ret;
}

static int pcm0_init(struct platform_device *pdev)
{
	int ret = -EINVAL;

#ifdef CONFIG_ANDROID
	register_early_suspend(&jz_i2s_early_suspend);
#endif
	ret = pcm0_global_init(pdev);

	return ret;
}

static void pcm0_shutdown(struct platform_device *pdev)
{
	/* close pcm0 and current codec */
	free_irq(pcm_priv->irq,NULL);
	return;
}

static int pcm0_suspend(struct platform_device *pdev, pm_message_t state)
{
	return 0;
}

static int pcm0_resume(struct platform_device *pdev)
{
	return 0;
}


struct snd_dev_data pcm0_data = {
	.dev_ioctl	   	= pcm0_ioctl,
	.ext_data		= &pcm0_endpoints,
	.minor			= SND_DEV_DSP2,
	.init			= pcm0_init,
	.shutdown		= pcm0_shutdown,
	.suspend		= pcm0_suspend,
	.resume			= pcm0_resume,
};

static int __init init_pcm0(void)
{
	pcm_priv = (struct pcm0_board_info *)vmalloc(sizeof(struct pcm0_board_info));
	if (!pcm_priv)
		return -1;
	pcm_priv->rate = 0;
	pcm_priv->replay_format = 0;
	pcm_priv->record_format = 0;
	pcm_priv->endpoint= &pcm0_endpoints;
	return 0;
}
device_initcall(init_pcm0);
