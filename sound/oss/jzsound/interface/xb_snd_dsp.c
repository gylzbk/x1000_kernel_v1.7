/**
 * xb_snd_dsp.c
 *
 * jbbi <jbbi@ingenic.cn>
 *
 * 24 APR 2012
 *
 */
#include <linux/soundcard.h>
#include <linux/proc_fs.h>
#include <linux/dma-mapping.h>
#include <linux/vmalloc.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include "xb_snd_dsp.h"

static bool spipe_is_init = 0;
#define DEBUG_REPLAY  0
//#define AIC_DEBUG
#if DEBUG_REPLAY                                                                                                  
struct file *f_test = NULL;
static loff_t f_test_offset = 0;
static mm_segment_t old_fs;
#endif

static int first_write = 0;

#ifdef AIC_DEBUG 
	#define debug_print(fmt,args...)	\
		do {	\
			printk("#######(%s:%d):",__func__,__LINE__);	\
			printk(fmt".\n",##args);\
		} while (0)
#else 
	#define debug_print(fmt,args...)  do {} while(0)
#endif 
/*###########################################################*\
 * sub functions
\*###########################################################*/
/********************************************************\
 * buffer
\********************************************************/
static struct dsp_node* get_free_dsp_node(struct dsp_pipe *dp)
{
	unsigned long lock_flags;
	struct dsp_node *node = NULL;
	struct list_head *phead = &dp->free_node_list;

	spin_lock_irqsave(&dp->pipe_lock, lock_flags);
	{
		if (list_empty(phead)) {
			spin_unlock_irqrestore(&dp->pipe_lock, lock_flags);
			return NULL;
		}
		node = list_entry(phead->next, struct dsp_node, list);
		list_del(phead->next);
	}
	spin_unlock_irqrestore(&dp->pipe_lock, lock_flags);

	return node;
}

static void put_use_dsp_node(struct dsp_pipe *dp, struct dsp_node *node)
{
	unsigned long lock_flags;
	struct list_head *phead = &dp->use_node_list;

	spin_lock_irqsave(&dp->pipe_lock, lock_flags);
	{
		list_add_tail(&node->list, phead);
	}
	spin_unlock_irqrestore(&dp->pipe_lock, lock_flags);
}

static struct dsp_node *get_use_dsp_node(struct dsp_pipe *dp)
{
	unsigned long lock_flags;
	struct dsp_node *node = NULL;
	struct list_head *phead = &dp->use_node_list;

	spin_lock_irqsave(&dp->pipe_lock, lock_flags);
	{
		if (list_empty(phead)) {
			spin_unlock_irqrestore(&dp->pipe_lock, lock_flags);
			return NULL;
		}
		node = list_entry(phead->next, struct dsp_node, list);
		list_del(phead->next);
	}
	spin_unlock_irqrestore(&dp->pipe_lock, lock_flags);

	return node;
}

static void put_free_dsp_node(struct dsp_pipe *dp, struct dsp_node *node)
{
	unsigned long lock_flags;
	struct list_head *phead = &dp->free_node_list;

	spin_lock_irqsave(&dp->pipe_lock, lock_flags);
	{
		list_add_tail(&node->list, phead);
	}
	spin_unlock_irqrestore(&dp->pipe_lock, lock_flags);
}

static int get_free_dsp_node_count(struct dsp_pipe *dp)
{
	int count = 0;
	unsigned long lock_flags;
	struct list_head *tmp = NULL;
	struct list_head *phead = &dp->free_node_list;

	spin_lock_irqsave(&dp->pipe_lock, lock_flags);
	{
		list_for_each(tmp, phead)
			count ++;
	}
	spin_unlock_irqrestore(&dp->pipe_lock, lock_flags);

	return count;
}

static int get_use_dsp_node_count(struct dsp_pipe *dp)
{
	int count = 0;
	unsigned long lock_flags;
	struct list_head *tmp = NULL;
	struct list_head *phead = &dp->use_node_list;

	spin_lock_irqsave(&dp->pipe_lock, lock_flags);
	{
		list_for_each(tmp, phead)
			count ++;
	}
	spin_unlock_irqrestore(&dp->pipe_lock, lock_flags);

	return count;
}

/********************************************************\
 * dma
\********************************************************/

static void snd_dma_callback(void *arg);

static void snd_reconfig_dma(struct dsp_pipe *dp)
{
	if (dp->dma_chan == NULL)
		return;

	dmaengine_slave_config(dp->dma_chan,&dp->dma_config);
}
static bool dma_chan_filter(struct dma_chan *chan, void *filter_param)
{
	struct dsp_pipe *dp = filter_param;

	return (void*)dp->dma_type == chan->private;
}

static int snd_reuqest_dma(struct dsp_pipe *dp)
{
	dma_cap_mask_t mask;

	/* Try to grab a DMA channel */
	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	dp->dma_chan = dma_request_channel(mask, dma_chan_filter, (void*)dp);

	if (dp->dma_chan == NULL)
		return -ENXIO;

	snd_reconfig_dma(dp);

	/* alloc one scatterlist */
	dp->sg_len = 1;

	dp->sg = vmalloc(sizeof(struct scatterlist) * dp->sg_len);
	if (!dp->sg)
		return -ENOMEM;

	return 0;
}

static void snd_release_dma(struct dsp_pipe *dp)
{
	if (dp->dma_chan) {
		dmaengine_terminate_all(dp->dma_chan);
	}
	dma_release_channel(dp->dma_chan);
}

static int snd_prepare_dma_desc(struct dsp_pipe *dp)
{
	struct dsp_node *node = NULL;
	struct dma_async_tx_descriptor *desc = NULL;

	/* turn the dsp_node to dma desc */
	if (dp->dma_config.direction == DMA_TO_DEVICE) {
		node = get_use_dsp_node(dp);
		if (!node)
			return -ENOMEM;

		dp->save_node = node;
		dma_cache_sync(NULL, (void *)node->pBuf, node->size, dp->dma_config.direction);

		/* config sg */
		sg_dma_address(dp->sg) = node->phyaddr;
		sg_dma_len(dp->sg) = node->end - node->start;

		desc = dp->dma_chan->device->device_prep_slave_sg(dp->dma_chan,
														  dp->sg,
														  dp->sg_len,
														  dp->dma_config.direction,
														  DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
		if (!desc) {
			put_use_dsp_node(dp, node);
			return -EFAULT;
		}
	} else if (dp->dma_config.direction == DMA_FROM_DEVICE) {
		node = get_free_dsp_node(dp);
		if (!node)
			return -ENOMEM;

		dp->save_node = node;
		dma_cache_sync(NULL, (void *)node->pBuf, node->size, dp->dma_config.direction);

		/* config sg */
		sg_dma_address(dp->sg) = node->phyaddr;
		sg_dma_len(dp->sg) = node->size;

		desc = dp->dma_chan->device->device_prep_slave_sg(dp->dma_chan,
														  dp->sg,
														  dp->sg_len,
														  dp->dma_config.direction,
														  DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
		if (!desc) {
			put_free_dsp_node(dp, node);
			return -EFAULT;
		}
	} else {
		return -EFAULT;
	}

	/* set desc callback */
	desc->callback = snd_dma_callback;
	desc->callback_param = (void *)dp;

	/* tx_submit */
	dmaengine_submit(desc);

	return 0;
}

static void snd_start_dma_transfer(struct dsp_pipe *dp)
{
	dp->is_trans = true;
	dma_async_issue_pending(dp->dma_chan);
}

static void snd_dma_callback(void *arg)
{
	struct dsp_pipe *dp = (struct dsp_pipe *)arg;

	/* unmap the node->phaddr */
	if (dp->dma_config.direction == DMA_TO_DEVICE) {
		put_free_dsp_node(dp, dp->save_node);
	} else if (dp->dma_config.direction == DMA_FROM_DEVICE) {
		put_use_dsp_node(dp, dp->save_node);
	} else {
		return;
	}
	dp->save_node =	NULL;
	dp->avialable_couter ++;
	debug_print("avialable_counter %d",dp->avialable_couter);
	if (dp->is_non_block == false)
		wake_up_interruptible(&dp->wq);

	/* if device closed, release the dma */
	if (dp->wait_stop_dma == true) {
		snd_release_dma(dp);
		dp->is_trans = false;
		dp->wait_stop_dma = false;
		wake_up_interruptible(&dp->wq);
		return;
	}

	if (dp->need_reconfig_dma == true) {
		snd_reconfig_dma(dp);
		dp->need_reconfig_dma = false;
	}

	/* start a new transfer */
	if (!snd_prepare_dma_desc(dp))
		snd_start_dma_transfer(dp);
	else
		dp->is_trans = false;
}

/********************************************************\
 * bypass
\********************************************************/
#define SND_SHARED_PIPE_CNT 	2
#define SPIPE_DEF_FRAGSIZE		1024
#define SPIPE_DEF_FRAGCNT		4
#define SPIPE_DEF_RATE			8000
#define SPIPE_DEF_SAMPLESIZE	16

static struct snd_dsp_bypss_pipe {
	struct dsp_pipe *volatile src_dp;
	struct dsp_pipe *volatile dst_dp;
	volatile bool src_start;
	volatile bool dst_start;
	int rate;
	int samplesize;
	int sfragcnt;
	int sfragsize;
	dma_addr_t spaddr;
} spipe[SND_SHARED_PIPE_CNT];

static int request_shared_pipe_buf(struct dsp_pipe *dp, int spipe_id)
{
	int i = 0;

	if (dp->is_shared)
		return -1;

	if (spipe_id == -1) {
		/* get a new shared buf */
		for (i = 0; i < SND_SHARED_PIPE_CNT; i++)
		{
			if (!spipe[i].src_dp && !spipe[i].dst_dp) {
				if (dp->dma_config.direction == DMA_TO_DEVICE) {
					spipe[i].dst_dp = dp;
					spipe[i].dst_start = false;
				} else if (dp->dma_config.direction == DMA_FROM_DEVICE) {
					spipe[i].src_dp = dp;
					spipe[i].src_start = false;
				}

				dp->is_shared = true;
				return i;
			}
		}
		return -1;
	} else {
		/* share an exit shared buf */
		if ((spipe_id < 0) || (spipe_id >= SND_SHARED_PIPE_CNT))
			return -1;

		if (spipe[spipe_id].dst_dp && !spipe[spipe_id].src_dp) {
			if (dp->dma_config.direction == DMA_FROM_DEVICE) {
				spipe[spipe_id].src_dp = dp;
				spipe[spipe_id].src_start = false;
				dp->is_shared = true;
				return spipe_id;
			}
		} else if (spipe[spipe_id].src_dp && !spipe[spipe_id].dst_dp) {
			if (dp->dma_config.direction == DMA_TO_DEVICE) {
				spipe[spipe_id].dst_dp = dp;
				spipe[spipe_id].dst_start = false;
				dp->is_shared = true;
				return spipe_id;
			}
		} else
			return -1;
	}
	return -1;
}

static int release_shared_pipe_buf(struct dsp_pipe *dp, int spipe_id)
{
	if ((spipe_id < 0) || (spipe_id >= SND_SHARED_PIPE_CNT))
		return -1;

	if (spipe[spipe_id].src_dp == dp) {
		spipe[spipe_id].src_dp = NULL;
	} else if (spipe[spipe_id].dst_dp == dp) {
		spipe[spipe_id].dst_dp = NULL;
	} else
		return -1;

	dp->is_shared = false;

	return spipe_id;
}

/**
 * the first dsp device should use as
 * vol = start_bypass_trans(dp, -1),
 * the second dsp device shoudl use as
 * vol = start_bypass_trans(dp, vol)
 **/
static int start_bypass_trans(struct dsp_pipe *dp, int spipe_id)
{
	struct dma_async_tx_descriptor *desc_in;
	struct dma_async_tx_descriptor *desc_out;

	spipe_id = request_shared_pipe_buf(dp, spipe_id);

	if ((spipe_id < 0) || (spipe_id >= SND_SHARED_PIPE_CNT))
		return -1;

	if (spipe[spipe_id].src_dp == dp)
		spipe[spipe_id].src_start = true;
	else if (spipe[spipe_id].dst_dp == dp)
		spipe[spipe_id].dst_start = true;
	else
		return -1;

	if (!spipe[spipe_id].src_start || !spipe[spipe_id].dst_start)
		return spipe_id;

	/* wait dma trans stop */
	if (spipe[spipe_id].src_dp->is_trans == true)
		spipe[spipe_id].src_dp->wait_stop_dma = true;

	if (spipe[spipe_id].dst_dp->is_trans == true)
		spipe[spipe_id].dst_dp->wait_stop_dma = true;

	if (spipe[spipe_id].src_dp->wait_stop_dma == true) {
		wait_event_interruptible(spipe[spipe_id].src_dp->wq,
				spipe[spipe_id].src_dp->is_trans == false);
		if (spipe[spipe_id].src_dp->sg) {
			vfree(spipe[spipe_id].src_dp->sg);
			spipe[spipe_id].src_dp->sg = NULL;
		}
	}
	if (spipe[spipe_id].dst_dp->wait_stop_dma == true) {
		wait_event_interruptible(spipe[spipe_id].dst_dp->wq,
								 spipe[spipe_id].dst_dp->is_trans == false);
		if (spipe[spipe_id].dst_dp->sg) {
			vfree(spipe[spipe_id].dst_dp->sg);
			spipe[spipe_id].dst_dp->sg = NULL;
		}
	}
	/* enable record dma */
	desc_in = dp->dma_chan->device->device_prep_dma_cyclic(dp->dma_chan,
														   spipe[spipe_id].spaddr,
														   spipe[spipe_id].sfragcnt * spipe[spipe_id].sfragsize,
														   spipe[spipe_id].sfragsize,
														   DMA_FROM_DEVICE);
	dmaengine_submit(desc_in);
	snd_start_dma_transfer(dp);

	/* sleep for one fragment full */
	msleep((spipe[spipe_id].sfragsize / spipe[spipe_id].samplesize / spipe[spipe_id].rate) * 1000);

	/* enable replay dma */
	desc_out = dp->dma_chan->device->device_prep_dma_cyclic(dp->dma_chan,
															spipe[spipe_id].spaddr,
															spipe[spipe_id].sfragcnt * spipe[spipe_id].sfragsize,
															spipe[spipe_id].sfragsize,
															DMA_TO_DEVICE);
	dmaengine_submit(desc_out);
	snd_start_dma_transfer(dp);

	return 0;
}

static int stop_bypass_trans(struct dsp_pipe *dp, int spipe_id)
{
	if ((spipe_id < 0) || (spipe_id >= SND_SHARED_PIPE_CNT))
		return -1;

	/* set no link to stop the dma */
	if (dp->dma_chan) {
		dmaengine_terminate_all(dp->dma_chan);
	}

	spipe_id = release_shared_pipe_buf(dp, spipe_id);

	/* set status */
	if (spipe[spipe_id].src_dp == dp)
		spipe[spipe_id].src_start = false;
	else if (spipe[spipe_id].dst_dp == dp)
		spipe[spipe_id].dst_start = false;
	else
		return -1;

	if (spipe[spipe_id].src_start || spipe[spipe_id].dst_start)
		return spipe_id;

	return 0;
}

static int spipe_init(struct device *dev)
{
	int i = 0;

	dmam_alloc_noncoherent(dev,
						   SPIPE_DEF_FRAGSIZE * SPIPE_DEF_FRAGCNT * SND_SHARED_PIPE_CNT,
						   &spipe[0].spaddr,
						   GFP_KERNEL | GFP_DMA);

	if (spipe[0].spaddr == 0) {
		printk("SOUDND ERROR: %s(line:%d) alloc memory error!\n",
			   __func__, __LINE__);
		return -ENOMEM;
	}

	for (i = 0 ; i < SND_SHARED_PIPE_CNT; i++) {
		spipe[i].src_dp = NULL;
		spipe[i].dst_dp = NULL;
		spipe[i].src_start = false;
		spipe[i].dst_start = false;
		spipe[i].rate = SPIPE_DEF_RATE;
		spipe[i].samplesize = SPIPE_DEF_SAMPLESIZE;
		spipe[i].sfragcnt = SPIPE_DEF_FRAGCNT;
		spipe[i].sfragsize = SPIPE_DEF_FRAGSIZE;
		spipe[i].spaddr = spipe[0].spaddr + (SPIPE_DEF_FRAGCNT * SPIPE_DEF_FRAGSIZE * i);
	}

	return 0;
}

/********************************************************\
 * filter
\********************************************************/
/*
 * Convert signed byte to unsiged byte
 *
 * Mapping:
 * 	signed		unsigned
 *	0x00 (0)	0x80 (128)
 *	0x01 (1)	0x81 (129)
 *	......		......
 *	0x7f (127)	0xff (255)
 *	0x80 (-128)	0x00 (0)
 *	0x81 (-127)	0x01 (1)
 *	......		......
 *	0xff (-1)	0x7f (127)
 */
int convert_8bits_signed2unsigned(void *buffer, int counter)
{
	int i;
	int counter_8align	= counter & ~0x7;
	unsigned char *ucsrc	= buffer;
	unsigned char *ucdst	= buffer;

	for (i = 0; i < counter_8align; i+=8) {
		*(ucdst + i + 0) = *(ucsrc + i + 0) + 0x80;
		*(ucdst + i + 1) = *(ucsrc + i + 1) + 0x80;
		*(ucdst + i + 2) = *(ucsrc + i + 2) + 0x80;
		*(ucdst + i + 3) = *(ucsrc + i + 3) + 0x80;
		*(ucdst + i + 4) = *(ucsrc + i + 4) + 0x80;
		*(ucdst + i + 5) = *(ucsrc + i + 5) + 0x80;
		*(ucdst + i + 6) = *(ucsrc + i + 6) + 0x80;
		*(ucdst + i + 7) = *(ucsrc + i + 7) + 0x80;
	}

	BUG_ON(i != counter_8align);

	for (i = counter_8align; i < counter; i++) {
		*(ucdst + i) = *(ucsrc + i) + 0x80;
	}

	return counter;
}

/*
 * Convert stereo data to mono data, data width: 8 bits/channel
 *
 * buff:	buffer address
 * data_len:	data length in kernel space, the length of stereo data
 *		calculated by "node->end - node->start"
 */
int convert_8bits_stereo2mono(void *buff, int data_len)
{
	/* stride = 16 bytes = 2 channels * 1 byte * 8 pipelines */
	int data_len_16aligned = data_len & ~0xf;
	int mono_cur, stereo_cur;
	unsigned char *uc_buff = buff;

	/* copy 8 times each loop */
	for (stereo_cur = mono_cur = 0;
	     stereo_cur < data_len_16aligned;
	     stereo_cur += 16, mono_cur += 8) {

		uc_buff[mono_cur + 0] = uc_buff[stereo_cur + 0];
		uc_buff[mono_cur + 1] = uc_buff[stereo_cur + 2];
		uc_buff[mono_cur + 2] = uc_buff[stereo_cur + 4];
		uc_buff[mono_cur + 3] = uc_buff[stereo_cur + 6];
		uc_buff[mono_cur + 4] = uc_buff[stereo_cur + 8];
		uc_buff[mono_cur + 5] = uc_buff[stereo_cur + 10];
		uc_buff[mono_cur + 6] = uc_buff[stereo_cur + 12];
		uc_buff[mono_cur + 7] = uc_buff[stereo_cur + 14];
	}

	BUG_ON(stereo_cur != data_len_16aligned);

	/* remaining data */
	for (; stereo_cur < data_len; stereo_cur += 2, mono_cur++) {
		uc_buff[mono_cur] = uc_buff[stereo_cur];
	}

	return (data_len >> 1);
}

/*
 * Convert stereo data to mono data, and convert signed byte to unsigned byte.
 *
 * data width: 8 bits/channel
 *
 * buff:	buffer address
 * data_len:	data length in kernel space, the length of stereo data
 *		calculated by "node->end - node->start"
 */
int convert_8bits_stereo2mono_signed2unsigned(void *buff, int data_len)
{
	/* stride = 16 bytes = 2 channels * 1 byte * 8 pipelines */
	int data_len_16aligned = data_len & ~0xf;
	int mono_cur, stereo_cur;
	unsigned char *uc_buff = buff;

	/* copy 8 times each loop */
	for (stereo_cur = mono_cur = 0;
	     stereo_cur < data_len_16aligned;
	     stereo_cur += 16, mono_cur += 8) {

		uc_buff[mono_cur + 0] = uc_buff[stereo_cur + 0] + 0x80;
		uc_buff[mono_cur + 1] = uc_buff[stereo_cur + 2] + 0x80;
		uc_buff[mono_cur + 2] = uc_buff[stereo_cur + 4] + 0x80;
		uc_buff[mono_cur + 3] = uc_buff[stereo_cur + 6] + 0x80;
		uc_buff[mono_cur + 4] = uc_buff[stereo_cur + 8] + 0x80;
		uc_buff[mono_cur + 5] = uc_buff[stereo_cur + 10] + 0x80;
		uc_buff[mono_cur + 6] = uc_buff[stereo_cur + 12] + 0x80;
		uc_buff[mono_cur + 7] = uc_buff[stereo_cur + 14] + 0x80;
	}

	BUG_ON(stereo_cur != data_len_16aligned);

	/* remaining data */
	for (; stereo_cur < data_len; stereo_cur += 2, mono_cur++) {
		uc_buff[mono_cur] = uc_buff[stereo_cur] + 0x80;
	}

	return (data_len >> 1);
}

/*
 * Convert stereo data to mono data, data width: 16 bits/channel
 *
 * buff:	buffer address
 * data_len:	data length in kernel space, the length of stereo data
 *		calculated by "node->end - node->start"
 */
int convert_16bits_stereo2mono(void *buff, int data_len)
{
	/* stride = 32 bytes = 2 channels * 2 byte * 8 pipelines */
	int data_len_32aligned = data_len & ~0x1f;
	int data_cnt_ushort = data_len_32aligned >> 1;
	int mono_cur, stereo_cur;
	unsigned short *ushort_buff = (unsigned short *)buff;

	/* copy 8 times each loop */
	for (stereo_cur = mono_cur = 0;
	     stereo_cur < data_cnt_ushort;
	     stereo_cur += 16, mono_cur += 8) {

		ushort_buff[mono_cur + 0] = ushort_buff[stereo_cur + 0];
		ushort_buff[mono_cur + 1] = ushort_buff[stereo_cur + 2];
		ushort_buff[mono_cur + 2] = ushort_buff[stereo_cur + 4];
		ushort_buff[mono_cur + 3] = ushort_buff[stereo_cur + 6];
		ushort_buff[mono_cur + 4] = ushort_buff[stereo_cur + 8];
		ushort_buff[mono_cur + 5] = ushort_buff[stereo_cur + 10];
		ushort_buff[mono_cur + 6] = ushort_buff[stereo_cur + 12];
		ushort_buff[mono_cur + 7] = ushort_buff[stereo_cur + 14];
	}

	BUG_ON(stereo_cur != data_cnt_ushort);

	/* remaining data */
	for (; stereo_cur < data_cnt_ushort; stereo_cur += 2, mono_cur++) {
		ushort_buff[mono_cur] = ushort_buff[stereo_cur];
	}

	return (data_len >> 1);
}

/*
 * convert normal 16bit stereo data to mono data
 *
 * buff:	buffer address
 * data_len:	data length in kernel space, the length of stereo data
 *
 */
int convert_16bits_stereomix2mono(void *buff, int data_len)
{
	/* stride = 32 bytes = 2 channels * 2 byte * 8 pipelines */
	int data_len_32aligned = data_len & ~0x1f;
	int data_cnt_ushort = data_len_32aligned >> 1;
	int left_cur, right_cur, mono_cur;
	short *ushort_buff = (short *)buff;
	/*init*/
	left_cur = 0;
	right_cur = left_cur + 1;
	mono_cur = 0;
	/*because the buff's size is always 4096 bytes,so it will not lost data*/
	while (right_cur < data_cnt_ushort)
	{
		ushort_buff[mono_cur + 0] = ((ushort_buff[left_cur + 0]) + (ushort_buff[right_cur + 0]));
		ushort_buff[mono_cur + 1] = ((ushort_buff[left_cur + 2]) + (ushort_buff[right_cur + 2]));
		ushort_buff[mono_cur + 2] = ((ushort_buff[left_cur + 4]) + (ushort_buff[right_cur + 4]));
		ushort_buff[mono_cur + 3] = ((ushort_buff[left_cur + 6]) + (ushort_buff[right_cur + 6]));
		ushort_buff[mono_cur + 4] = ((ushort_buff[left_cur + 8]) + (ushort_buff[right_cur + 8]));
		ushort_buff[mono_cur + 5] = ((ushort_buff[left_cur + 10]) + (ushort_buff[right_cur + 10]));
		ushort_buff[mono_cur + 6] = ((ushort_buff[left_cur + 12]) + (ushort_buff[right_cur + 12]));
		ushort_buff[mono_cur + 7] = ((ushort_buff[left_cur + 14]) + (ushort_buff[right_cur + 14]));

		left_cur += 16;
		right_cur = left_cur + 1;
		mono_cur += 8;
	}

	return (data_len >> 1);
}

/********************************************************\
 * others
\********************************************************/
static int init_pipe(struct dsp_pipe *dp,struct device *dev,enum dma_data_direction direction)
{
	int i = 0;
	struct dsp_node *node;

	if ((dp->fragsize != FRAGSIZE_S) &&
		(dp->fragsize != FRAGSIZE_M) &&
		(dp->fragsize != FRAGSIZE_L))
	{
		return -1;
	}

	if ((dp->fragcnt != FRAGCNT_S) &&
		(dp->fragcnt != FRAGCNT_M) &&
		(dp->fragcnt != FRAGCNT_L))
	{
		return -1;
	}

	/* alloc memory */
	dp->vaddr = (unsigned long)dmam_alloc_noncoherent(dev,
													  PAGE_ALIGN(dp->fragsize * dp->fragcnt),
													  &dp->paddr,
													  GFP_KERNEL | GFP_DMA);
	if ((void*)dp->vaddr == NULL)
		return -ENOMEM;

	/* init dsp nodes */
	for (i = 0; i < dp->fragcnt; i++) {
		node = vmalloc(sizeof(struct dsp_node));
		if (!node)
			goto init_pipe_error;

		node->pBuf = dp->vaddr + dp->fragsize * i;
		node->phyaddr = dp->paddr + dp->fragsize * i;
		node->start = 0;
		node->end = 0;
		node->size = dp->fragsize;

		list_add(&node->list, &dp->free_node_list);
	}

	/* init others */
	if (direction == DMA_TO_DEVICE)
		dp->avialable_couter = dp->fragcnt;
	else if (direction == DMA_FROM_DEVICE)
		dp->avialable_couter = 0;
	dp->dma_chan = NULL;
	dp->save_node = NULL;
	init_waitqueue_head(&dp->wq);
	dp->is_trans = false;
	dp->is_used = false;
	dp->is_mmapd = false;
	dp->wait_stop_dma = false;
	dp->need_reconfig_dma = false;
	dp->sg = NULL;
	dp->sg_len = 0;

	spin_lock_init(&dp->pipe_lock);

	return 0;
init_pipe_error:
	/* free all the node in free_node_list */
	list_for_each_entry(node, &dp->free_node_list, list)
		vfree(node);
	/* free memory */
	dmam_free_noncoherent(dev,
						  dp->fragsize * dp->fragcnt,
						  (void*)dp->vaddr,
						  dp->paddr);
	return -1;
}

static void deinit_pipe(struct dsp_pipe *dp,struct device *dev)
{
	struct dsp_node *node;

	/* free all the node in free_node_list */
	list_for_each_entry(node, &dp->free_node_list, list)
		vfree(node);
	/* free memory */
	dmam_free_noncoherent(dev,
						  dp->fragsize * dp->fragcnt,
						  (void*)dp->vaddr,
						  dp->paddr);
}

static int mmap_pipe(struct dsp_pipe *dp, struct vm_area_struct *vma)
{
	unsigned long start = 0;
	unsigned long off = 0;
	unsigned long len = 0;

	off = vma->vm_pgoff << PAGE_SHIFT;
	start = dp->paddr;
	len = PAGE_ALIGN(dp->fragcnt * dp->fragsize);
	start &= PAGE_MASK;
	if ((vma->vm_end - vma->vm_start + off) > len)
		return -EINVAL;
	off += start;

	vma->vm_pgoff = off >> PAGE_SHIFT;
	vma->vm_flags |= VM_IO;

	pgprot_val(vma->vm_page_prot) &= ~_CACHE_MASK;
	pgprot_val(vma->vm_page_prot) |= _CACHE_CACHABLE_NONCOHERENT;

	if (io_remap_pfn_range(vma, vma->vm_start, off >> PAGE_SHIFT,
			       vma->vm_end - vma->vm_start,
			       vma->vm_page_prot)) {
		return -EAGAIN;
	}

	return 0;
}

/*###########################################################*\
 * interfacees
\*###########################################################*/
/********************************************************\
 * llseek
\********************************************************/
loff_t xb_snd_dsp_llseek(struct file *file,
						 loff_t offset,
						 int origin,
						 struct snd_dev_data *ddata)
{
	return 0;
}

/********************************************************\
 * read
\********************************************************/
ssize_t xb_snd_dsp_read(struct file *file,
						char __user *buffer,
						size_t count,
						loff_t *ppos,
						struct snd_dev_data *ddata)
{
	int	mcount = count;
	int ret = -EINVAL;
	int fixed_buff_cnt = 0;
	int node_buff_cnt = 0;
	struct dsp_node *node = NULL;
	struct dsp_pipe *dp = NULL;
	struct dsp_endpoints *endpoints = NULL;

	if (!(file->f_mode & FMODE_READ))
		return -EPERM;

	if (ddata == NULL)
		return -ENODEV;

	endpoints = (struct dsp_endpoints *)ddata->ext_data;
	if (endpoints == NULL)
		return -ENODEV;

	dp = endpoints->in_endpoint;

	if (dp == NULL)
		return -ENODEV;

	if (dp->is_mmapd && dp->is_shared)
		return -EBUSY;

	do {
		while(1) {
			node = get_use_dsp_node(dp);
			if (!node) {
				if (dp->is_trans == false) {
					ret = snd_prepare_dma_desc(dp);
					if (!ret) {
						if (ddata && ddata->dev_ioctl)
							ddata->dev_ioctl(SND_DSP_ENABLE_DMA_RX, 0);
						snd_start_dma_transfer(dp);
					} else {
						return -EFAULT;
					}
				}
				if (dp->is_non_block)
					return count - mcount;
				wait_event_interruptible(dp->wq, dp->avialable_couter >= 1);
			} else {
				dp->avialable_couter --;
				break;
			}
		}

		if ((node_buff_cnt = node->end - node->start) > 0) {
			if (dp->filter)
				fixed_buff_cnt = dp->filter((void *)(node->pBuf + node->start), node_buff_cnt);
			else
				fixed_buff_cnt = node_buff_cnt;

			if (mcount >= fixed_buff_cnt) {
				ret = copy_to_user((void *)buffer, (void *)(node->pBuf + node->start), fixed_buff_cnt);
				if (!ret) {
					buffer += fixed_buff_cnt;
					mcount -= fixed_buff_cnt;
				}
			} else {
				ret = copy_to_user((void *)buffer,(void *)(node->pBuf + node->start), mcount);
				break;
			}

			put_free_dsp_node(dp, node);

			if (ret)
				return -EFAULT;
		}
	} while (mcount > 0);

	return count - mcount;
}

/********************************************************\
 * write
\********************************************************/
ssize_t xb_snd_dsp_write(struct file *file,
						 const char __user *buffer,
						 size_t count,
						 loff_t *ppos,
						 struct snd_dev_data *ddata)
{
	int	mcount = count;
	int copy_size = 0;
	int ret = -EINVAL;
	struct dsp_node *node = NULL;
	struct dsp_pipe *dp = NULL;
	struct dsp_endpoints *endpoints = NULL;

	if (!(file->f_mode & FMODE_WRITE))
		return -EPERM;

	if (ddata == NULL)
		return -ENODEV;

	endpoints = (struct dsp_endpoints *)ddata->ext_data;
	if (endpoints == NULL)
		return -ENODEV;

	dp = endpoints->out_endpoint;

	if (dp == NULL)
		return -ENODEV;

	if (dp->is_mmapd && dp->is_shared)
		return -EBUSY;

	while (mcount > 0) {
		while (1) {
			node = get_free_dsp_node(dp);
			if (!node) {
				if (dp->is_trans == false)
					return -1;
				if (dp->is_non_block == true)
					return count - mcount;
				wait_event_interruptible(dp->wq, dp->avialable_couter >= 1);
			} else {
				dp->avialable_couter --;
				break;
			}
		}

		if (mcount >= node->size)
			copy_size = node->size;
		else
			copy_size = mcount;

		if (copy_size == copy_from_user((void *)node->pBuf, buffer, copy_size)) {
			dp->avialable_couter ++;
			put_free_dsp_node(dp,node);
			return -EFAULT;
		}
#if DEBUG_REPLAY
		old_fs = get_fs();
		set_fs(KERNEL_DS);

		if (!IS_ERR(f_test)) {
			vfs_write(f_test, (void*)node->pBuf ,count, &f_test_offset);
			f_test_offset = f_test->f_pos;
		}   
		set_fs(old_fs);
#endif
		buffer += copy_size;
		mcount -= copy_size;

		node->start = 0;
		node->end = copy_size;
		put_use_dsp_node(dp, node);

		debug_print("dp->is_trans = %d.\n",dp->is_trans == true);
		if (dp->is_trans == false) {
			ret = snd_prepare_dma_desc(dp);
			if (!ret) {
				if (ddata && ddata->dev_ioctl && (!first_write)) {
					ddata->dev_ioctl(SND_DSP_ENABLE_DMA_TX, 0);
				}
				snd_start_dma_transfer(dp);
			}
		}
	}

	return count;
}

/********************************************************\
 * ioctl
\********************************************************/
unsigned int xb_snd_dsp_poll(struct file *file,
							 poll_table *wait,
							 struct snd_dev_data *ddata)
{
	return -EINVAL;
}

/********************************************************\
 * ioctl
\********************************************************/
/**
 * only the dsp device opend as O_RDONLY or O_WRONLY, ioctl
 * works, if a dsp device opend as O_RDWR, it will return -1
 **/
long xb_snd_dsp_ioctl(struct file *file,
					  unsigned int cmd,
					  unsigned long arg,
					  struct snd_dev_data *ddata)
{
	long ret = -EINVAL;
	struct dsp_pipe *dp = NULL;
	struct dsp_endpoints *endpoints = NULL;

	if (ddata == NULL)
		return -ENODEV;

	endpoints = (struct dsp_endpoints *)ddata->ext_data;
	if (endpoints == NULL)
		return -ENODEV;

	/* O_RDWR mode operation, do not allowed */
	if ((file->f_mode & FMODE_READ) && (file->f_mode & FMODE_WRITE))
		return -EPERM;

	switch (cmd) {
		//case SNDCTL_DSP_BIND_CHANNEL:
		/* OSS 4.x: Route setero output to the specified channels(obsolete) */
		/* we do't support here */
		//break;

	case SNDCTL_DSP_CHANNELS: {
		/* OSS 4.x: set the number of audio channels */
		int channels = -1;

		if (get_user(channels, (int *)arg)) {
			return -EFAULT;
		}

		/* fatal: this command can be well used in O_RDONLY and O_WRONLY mode,
		   if opend as O_RDWR, only replay channels will be set, if record
		   channels also want to be set, use cmd SOUND_PCM_READ_CHANNELS instead*/
		if (file->f_mode & FMODE_WRITE) {
			if (ddata->dev_ioctl)
				ret = (int)ddata->dev_ioctl(SND_DSP_SET_REPLAY_CHANNELS, (unsigned long)&channels);
			if (!ret)
				break;
		} else if (file->f_mode & FMODE_READ) {
			if (ddata->dev_ioctl)
				ret = (int)ddata->dev_ioctl(SND_DSP_SET_RECORD_CHANNELS, (unsigned long)&channels);
			if (!ret)
				break;
		} else
			return -EPERM;

		ret = put_user(channels, (int *)arg);
		break;
	}

		//case SNDCTL_DSP_COOKEDMODE:
		/* OSS 4.x: Disable/enable the "on fly" format conversions made by the OSS software */
		/* we do't support here */
		//break;

		//case SNDCTL_DSP_CURRENT_IPTR:
		/* OSS 4.x: Returns the current recording position */
		/* we do't support here */
		//break;

		//case SNDCTL_DSP_CURRENT_OPTR:
		/* OSS 4.x: Returns the current playback position */
		/* we do't support here */
		//break;

	case SNDCTL_DSP_GETBLKSIZE: {
		/* OSS 4.x: Get the current fragment size (obsolete) */
		int blksize = 0;

		if (file->f_mode & FMODE_WRITE) {
			dp = endpoints->in_endpoint;
		} else if (file->f_mode & FMODE_READ) {
			dp = endpoints->in_endpoint;
		} else
			return -EPERM;

		blksize = dp->fragsize * dp->fragcnt;

		ret = put_user(blksize, (int *)arg);
		break;
	}

		//case SNDCTL_DSP_GETCAPS:
		/* OSS 4.x: Returns the capabilities of an audio device */
		/* we do't support here */
		//break;

		//case SNDCTL_DSP_GETCHANNELMASK:
		/* OSS 4.x: Retruns the bindings supported by the device (obsolete) */
		/* we do't support here */
		//break;

		//case SNDCTL_DSP_GET_CHNORDER:
		/* OSS 4.x: Get the channel ordering of a muti channel device */
		/* we do't support here */
		//break;

		//case SNDCTL_DSP_GETERROR:
		/* OSS 4.x: Returns audio device error infomation */
		/* we do't support here */
		//break;

	case SNDCTL_DSP_GETFMTS: {
		/* OSS 4.x: Returns a list of natively supported sample formats */
		int mask = -1;

		/* fatal: this command can be well used in O_RDONLY and O_WRONLY mode,
		   if opend as O_RDWR, only replay supported formats will be return */
		if (file->f_mode & FMODE_WRITE) {
			if (ddata->dev_ioctl)
				ret = (int)ddata->dev_ioctl(SND_DSP_GET_REPLAY_FMT_CAP, (unsigned long)&mask);
			if (!ret)
				break;
		} else if (file->f_mode & FMODE_READ) {
			if (ddata->dev_ioctl)
				ret = (int)ddata->dev_ioctl(SND_DSP_GET_RECORD_FMT_CAP, (unsigned long)&mask);
			if (!ret)
				break;
		} else
			return -EPERM;

		ret = put_user(mask, (int *)arg);
		break;
	}

		//case SNDCTL_DSP_GETIPEAKS:
		/* OSS 4.x: The peak levels for all recording channels */
		/* we do't support here */
		//break;

		//case SNDCTL_DSP_GETIPTR:
		/* OSS 4.x: Returns the current recording pointer (obsolete) */
		/* we do't support here */
		//break;

	case SNDCTL_DSP_GETISPACE: {
		/* OSS 4.x: Returns the amount of recorded data that can be read without blocking */
		int amount = 0;

		if (file->f_mode & FMODE_READ) {
			dp = endpoints->in_endpoint;
			amount = get_use_dsp_node_count(dp) * dp->fragsize;
			ret = put_user(amount, (int *)arg);
		}

		break;
	}

		//case SNDCTL_DSP_GETODELAY:
		/* OSS 4.x: Returns the playback buffering delay */
		/* we do't support here */
		//break;

		//case SNDCTL_DSP_GETOPEAKS:
		/* OSS 4.x: The peak levels for all playback channels */
		/* we do't support here */
		//break;

		//case SNDCTL_DSP_GETOPTR:
		/* OSS 4.x: Returns the current playback pointer (obsolete) */
		/* we do't support here */
		//break;

	case SNDCTL_DSP_GETOSPACE: {
		/* OSS 4.x: Returns the amount of playback data that can be written without blocking */
		int amount = 0;

		if (file->f_mode & FMODE_WRITE) {
			dp = endpoints->out_endpoint;
			amount = get_free_dsp_node_count(dp) * dp->fragsize;
			ret = put_user(amount, (int *)arg);
		}

		break;
	}

		//case SNDCTL_DSP_GET_PLAYTGT_NAMES:
		/* OSS 4.x: Returns labels for the currently available output routings */
		//break;

		//case SNDCTL_DSP_GET_PLAYTGT:
		/* OSS 4.x: Returns the current output routing */
		//break;

		//case SNDCTL_DSP_GETPLAYVOL:
		/* OSS 4.x: Returns the current audio playback volume */
		/* we do't support here */
		//break;

		//case SNDCTL_DSP_GET_RECSRC_NAMES:
		/* OSS 4.x: Returns labels for the currently available recoring sources */
		//break;

		//case SNDCTL_DSP_GET_RECSRC:
		/* OSS 4.x: Returns the current recording source */
		//break;

		//case SNDCTL_DSP_GET_RECVOL:
		/* OSS 4.x: Returns the current audio recording level */
		/* we do't support here */
		//break;

		//case SNDCTL_DSP_GETTRIGGER:
		/* OSS 4.x: Returns the current trigger bits (obsolete) */
		/* we do't support here */
		//break;

		//case SNDCTL_DSP_HALT_INPUT:
		/* OSS 4.x: Aborts audio recording operation */
		/* we do't support here */
		//break;

		//case SNDCTL_DSP_HALT_OUTPUT:
		/* OSS 4.x: Aborts audio playback operation */
		/* we do't support here */
		//break;

		//case SNDCTL_DSP_HALT:
		/* OSS 4.x: Aborts audio recording and/or playback operation */
		/* we do't support here */
		//break;

		//case SNDCTL_DSP_LOW_WATER:
		/* OSS 4.x: Sets the trigger treshold for select() */
		/* we do't support here */
		//break;

		//case SNDCTL_DSP_NONBLOCK:
		/* OSS 4.x: Force non-blocking mode */
		/* we do't support here */
		//break;

		//case SNDCTL_DSP_POLICY:
		/* OSS 4.x: Sets the timing policy of an audio device */
		/* we do't support here */
		//break;

		//case SNDCTL_DSP_POST:
		/* OSS 4.x: Forces audio playback to start (obsolete) */
		/* we do't support here */
		//break;

		//case SNDCTL_DSP_READCTL:
		/* OSS 4.x: Reads the S/PDIF interface status */
		/* we do't support here */
		//break;

		//case SNDCTL_DSP_SAMPLESIZE:
		/* OSS 4.x: Sets the sample size (obsolete) */
		/* we do't support here */
		//break;

		//case SNDCTL_DSP_SETDUPLEX:
		/* OSS 4.x: Truns on the duplex mode */
		/* we do't support here */
		//break;

	case SNDCTL_DSP_SETFMT: {
		/* OSS 4.x: Select the sample format */
		int fmt = -1;

		if (get_user(fmt, (int *)arg)) {
			return -EFAULT;
		}

		if (fmt == AFMT_QUERY) {
			/* fatal: this command can be well used in O_RDONLY and O_WRONLY mode,
			   if opend as O_RDWR, only replay current format will be return */
			if (file->f_mode & FMODE_WRITE) {
				if (ddata->dev_ioctl)
					ret = (int)ddata->dev_ioctl(SND_DSP_GET_REPLAY_FMT, (unsigned long)&fmt);
				if (!ret)
					break;
			} else if (file->f_mode & FMODE_READ) {
				if (ddata->dev_ioctl)
					ret = (int)ddata->dev_ioctl(SND_DSP_GET_RECORD_FMT, (unsigned long)&fmt);
				if (!ret)
					break;
			} else
				return -EPERM;
		}

		/* fatal: this command can be well used in O_RDONLY and O_WRONLY mode,
		   if opend as O_RDWR, only replay format will be set */
		if (file->f_mode & FMODE_WRITE) {
			/* set format */
			if (ddata->dev_ioctl)
				ret = (int)ddata->dev_ioctl(SND_DSP_SET_REPLAY_FMT, (unsigned long)&fmt);
			if (!ret)
				break;
		} else if (file->f_mode & FMODE_READ) {
			/* set format */
			if (ddata->dev_ioctl)
				ret = (int)ddata->dev_ioctl(SND_DSP_SET_RECORD_FMT, (unsigned long)&fmt);
			if (!ret)
				break;
		} else
			return -EPERM;

		ret = put_user(fmt, (int *)arg);
		break;
	}

		//case SNDCTL_DSP_SETFRAGMENT:
		/* OSS 4.x: Sets the buffer size hint */
		/* we do't support here */
		//break;

		//case SNDCTL_DSP_SET_PLAYTGT:
		/* OSS 4.x: Sets the current output routing */
		//break;

		//case SNDCTL_DSP_SETPLAYVOL:
		/* OSS 4.x: Changes the current audio playback volume */
		/* we do't support here */
		//break;

		//case SNDCTL_DSP_SET_RECSRC:
		/* OSS 4.x: Sets the current recording source */
		//break;

		//case SNDCTL_DSP_SETRECVOL:
		/* OSS 4.x: Changes the current audio recording level */
		/* we do't support here */
		//break;

		//case SNDCTL_DSP_SETSYNCRO:
		/* OSS 4.x: Slaves the audio device to the /dev/sequencer driver (obsolete) */
		/* we do't support here */
		//break;

		//case SNDCTL_DSP_SETTRIGGER:
		/* OSS 4.x: Starts audio recording and/or playback in sync */
		/* we do't support here */
		//break;

		//case SNDCTL_DSP_SILENCE:
		/* OSS 4.x: clears the playback buffer with silence */
		/* we do't support here */
		//break;

		//case SNDCTL_DSP_SKIP:
		/* OSS 4.x: Discards all samples in the playback buffer */
		/* we do't support here */
		//break;

	case SNDCTL_DSP_SPEED: {
		/* OSS 4.x: Set the sampling rate */
		int rate = -1;

		if (get_user(rate, (int *)arg)) {
			return -EFAULT;
		}

		/* fatal: this command can be well used in O_RDONLY and O_WRONLY mode,
		   if opend as O_RDWR, only replay rate will be set, if record rate
		   also need to be set, use cmd SOUND_PCM_READ_RATE instead */
		if (file->f_mode & FMODE_WRITE) {
			if (ddata->dev_ioctl)
				ret = (int)ddata->dev_ioctl(SND_DSP_SET_REPLAY_RATE, (unsigned long)&rate);
			if (!ret)
				break;
		} else if (file->f_mode & FMODE_READ) {
			if (ddata->dev_ioctl)
				ret = (int)ddata->dev_ioctl(SND_DSP_SET_RECORD_RATE, (unsigned long)&rate);
			if (!ret)
				break;
		} else
			return -EPERM;

		ret = put_user(rate, (int *)arg);

		break;
	}

		//case SNDCTL_DSP_SUBDIVIDE:
		/* OSS 4.x: Requests the device to use smaller fragments (obsolete) */
		/* we do't support here */
		//break;

		//case SNDCTL_DSP_SYNCGROUP:
		/* OSS 4.x: Creates a synchronization group */
		/* we do't support here */
		//break;

		//case SNDCTL_DSP_SYNC:
		/* OSS 4.x: Suspend the application until all samples have been played */
		/* we do't support here */
		//break;

		//case SNDCTL_DSP_SYNCSTART:
		/* OSS 4.x: Starts all devices added to a synchronization group */
		/* we do't support here */
		//break;

		//case SNDCTL_DSP_WRITECTL:
		/* OSS 4.x: Alters the S/PDIF interface setup */
		/* we do't support here */
		//break;

	case SNDCTL_EXT_SET_DEVICE: {
		/* extention: used for set audio route */
		int device = -1;

		if (get_user(device, (int *)arg)) {
			return -EFAULT;
		}

		if (ddata->dev_ioctl) {
			ret = (int)ddata->dev_ioctl(SND_DSP_SET_DEVICE, (unsigned long)&device);
			if (!ret)
				break;
		}

		ret = put_user(device, (int *)arg);
		break;
	}

	case SNDCTL_EXT_SET_STANDBY: {
		/* extention: used for set standby and resume from standby */
		int mode = -1;

		if (get_user(mode, (int *)arg)) {
			return -EFAULT;
		}

		if (ddata->dev_ioctl) {
			ret = (int)ddata->dev_ioctl(SND_DSP_SET_STANDBY, (unsigned long)&mode);
			if (!ret)
				break;
		}

		ret = put_user(mode, (int *)arg);
		break;
	}

	case SNDCTL_EXT_START_BYPASS_TRANS: {
		/* extention: used for start a bypass transfer */
		struct spipe_info info;
		int spipe_id = -1;

		ret = copy_from_user((void *)&info, (void *)arg, sizeof(info)) ? -EFAULT : 0;
		if (!ret) {
			if (info.spipe_mode == SPIPE_MODE_RECORD)
				dp = endpoints->in_endpoint;
			else if (info.spipe_mode == SPIPE_MODE_REPLAY)
				dp = endpoints->out_endpoint;
			else
				return -EFAULT;

			spipe_id = start_bypass_trans(dp, info.spipe_id);

			ret = put_user(spipe_id, (int *)arg);
		} else
			return ret;
		break;
	}

	case SNDCTL_EXT_STOP_BYPASS_TRANS: {
		/* extention: used for stop a bypass transfer */
		struct spipe_info info;
		int spipe_id = -1;

		ret = copy_from_user((void *)&info, (void *)arg, sizeof(info)) ? -EFAULT : 0;
		if (!ret) {
			if (info.spipe_mode == SPIPE_MODE_RECORD)
				dp = endpoints->in_endpoint;
			else if (info.spipe_mode == SPIPE_MODE_REPLAY)
				dp = endpoints->out_endpoint;
			else
				return -EFAULT;

			spipe_id = stop_bypass_trans(dp, info.spipe_id);

			ret = put_user(spipe_id, (int *)arg);
		} else
			return ret;
		break;
	}

	case SNDCTL_EXT_DIRECT_GETINODE: {
		/* extention: used to get used input node, used for mmapd mode */
		struct direct_info info;
		struct dsp_node *node = NULL;

		if (file->f_mode & FMODE_READ) {
			dp = endpoints->in_endpoint;
			while(1) {
				node = get_use_dsp_node(dp);
				if (!node) {
					if (dp->is_trans == false) {
						ret = snd_prepare_dma_desc(dp);
						if (!ret) {
							snd_start_dma_transfer(dp);
						} else {
							return -EFAULT;
						}
					}
					if (dp->is_non_block) {
						info.bytes = 0;
						info.offset = -1;
						ret = copy_to_user((void *)arg, &info, sizeof(info)) ? -EFAULT : 0;
					} else {
						wait_event_interruptible(dp->wq, dp->avialable_couter >= 1);
					}
				} else {
					dp->save_node = node;
					info.bytes = node->size;
					info.offset = node->pBuf - dp->vaddr;
					ret = copy_to_user((void *)arg, &info, sizeof(info)) ? -EFAULT : 0;
					dp->avialable_couter --;
				}
			}
		} else
			return -EPERM;
		break;
	}

	case SNDCTL_EXT_DIRECT_PUTINODE: {
		/* extention: used to put free input node, used for mmapd mode */
		if (file->f_mode & FMODE_READ) {
			dp = endpoints->in_endpoint;
			dp->save_node->start = 0;
			dp->save_node->end = 0;
			put_free_dsp_node(dp, dp->save_node);
		} else
			return -EPERM;
		break;
	}

	case SNDCTL_EXT_DIRECT_GETONODE: {
		/* extention: used to get free output node, used for mmapd mode */
		struct direct_info info;
		struct dsp_node *node = NULL;

		if (file->f_mode & FMODE_WRITE) {
			dp = endpoints->out_endpoint;
			node = get_free_dsp_node(dp);
			if (node) {
				dp->save_node = node;
				info.bytes = node->size;
				info.offset = node->pBuf - dp->vaddr;
			} else {
				info.bytes = 0;
				info.offset = -1;
			}
			ret = copy_to_user((void *)arg, &info, sizeof(info)) ? -EFAULT : 0;
		} else
			return -EPERM;
		break;
	}

	case SNDCTL_EXT_DIRECT_PUTONODE: {
		/* extention: used to put used input node, used for mmapd mode */
		struct direct_info info;

		if (file->f_mode & FMODE_WRITE) {
			dp = endpoints->out_endpoint;
			ret = copy_from_user((void *)&info, (void *)arg, sizeof(info)) ? -EFAULT : 0;
			if (!ret) {
				/* put node to use list */
				dp->save_node->start = 0;
				dp->save_node->end = info.bytes;
				put_use_dsp_node(dp, dp->save_node);
				/* start dma transfer if dma is stopped */
				if (dp->is_trans == false) {
					ret = snd_prepare_dma_desc(dp);
					if (!ret) {
						snd_start_dma_transfer(dp);
					}
				}
			}
		} else
			return -EPERM;
		break;
	}

	default:
		printk("SOUDND ERROR: %s(line:%d) ioctl command %d is not supported\n",
			   __func__, __LINE__, cmd);
		return -1;
	}

	/* some operation may need to reconfig the dma, such as,
	 if we reset the format, it may cause reconfig the dma */
	if (file->f_mode & FMODE_READ) {
		dp = endpoints->in_endpoint;
		if (dp && dp->need_reconfig_dma == true) {
			if (dp->is_trans == false) {
				snd_reconfig_dma(dp);
				dp->need_reconfig_dma = false;
			}
		}
	}
	if (file->f_mode & FMODE_WRITE) {
		dp = endpoints->out_endpoint;
		if (dp && dp->need_reconfig_dma == true) {
			if (dp->is_trans == false) {
				snd_reconfig_dma(dp);
				dp->need_reconfig_dma = false;
			}
		}
	}

	return ret;
}

/********************************************************\
 * mmap
\********************************************************/
int xb_snd_dsp_mmap(struct file *file,
					struct vm_area_struct *vma,
					struct snd_dev_data *ddata)
{
	int ret = -ENODEV;
	struct dsp_pipe *dp = NULL;
	struct dsp_endpoints *endpoints = NULL;

	if (!((file->f_mode & FMODE_READ) && (file->f_mode & FMODE_WRITE)))
		return -EPERM;

	if (ddata == NULL)
		return ret;

	endpoints = (struct dsp_endpoints *)ddata->ext_data;
	if (endpoints == NULL)
		return ret;

	if (vma->vm_flags & VM_READ) {
		dp = endpoints->in_endpoint;
		if (dp->is_used) {
			ret = mmap_pipe(dp, vma);
			if (ret)
				return ret;
			dp->is_mmapd = true;
		}
	}

	if (vma->vm_flags & VM_WRITE) {
		dp = endpoints->out_endpoint;
		if (dp->is_used) {
			ret = mmap_pipe(dp, vma);
			if (ret)
				return ret;
			dp->is_mmapd = true;
		}
	}

	return ret;
}

/********************************************************\
 * open
\********************************************************/
int xb_snd_dsp_open(struct inode *inode,
					struct file *file,
					struct snd_dev_data *ddata)
{
	int ret = -ENXIO;
	struct dsp_pipe *dpi = NULL;
	struct dsp_pipe *dpo = NULL;
	struct dsp_endpoints *endpoints = NULL;
	first_write = 0;
	if (ddata == NULL) {
		return -ENODEV;
	}

	endpoints = (struct dsp_endpoints *)ddata->ext_data;
	if (endpoints == NULL) {
		return -ENODEV;
	}

	dpi = endpoints->in_endpoint;
	dpo = endpoints->out_endpoint;

	/* O_RDWR mode, if used for mmap, should open O_RDONLY or
	   O_WRONLY first, and then open as O_RDWR, you can only
	   map the mode(VM_WRITE / VM_READ) corresponding your
	   first opend */
	if ((file->f_mode & FMODE_READ) && (file->f_mode & FMODE_READ))
		if ((dpi && dpi->is_used) || (dpo && dpo->is_used))
			return 0;

	if (file->f_mode & FMODE_READ) {
		if (dpi == NULL)
			return -ENODEV;

		if (dpi->is_used) {
			printk("\nAudio read device is busy!\n");
			return -EBUSY;
		}

		dpi->is_non_block = file->f_flags & O_NONBLOCK;

		/* enable dsp device record */
		if (ddata->dev_ioctl) {
			ret = (int)ddata->dev_ioctl(SND_DSP_ENABLE_RECORD, 0);
			if (ret < 0)
				return -EIO;
		}
		dpi->is_used = true;
		/* request dma for record */
		ret = snd_reuqest_dma(dpi);
		if (ret) {
			printk("AUDIO ERROR, can't get dma!\n");
		}
	}

	if (file->f_mode & FMODE_WRITE) {
		if (dpo == NULL)
			return -ENODEV;

		if (dpo->is_used) {
			printk("\nAudio write device is busy!\n");
			return -EBUSY;
		}

		dpo->is_non_block = file->f_flags & O_NONBLOCK;

		/* enable dsp device replay */
		if (ddata->dev_ioctl) {
			ret = (int)ddata->dev_ioctl(SND_DSP_ENABLE_REPLAY, 0);
			if (ret < 0)
				return -EIO;
		}
		dpo->is_used = true;

		/* request dma for replay */
		ret = snd_reuqest_dma(dpo);
		if (ret) {
			printk("AUDIO ERROR, can't get dma!\n");
		}
	}

#if DEBUG_REPLAY
	printk("DEBUG:----open /data/audio.pcm.pcm-%s\tline:%d\n",__func__,__LINE__);
	f_test = filp_open("/data/audio.pcm", O_RDWR | O_APPEND, S_IRUSR | S_IWUSR);
	printk ("f_test is error %d.\n",f_test);
	if (!IS_ERR(f_test)) {
		printk("open debug audio sussess %p.\n",f_test);
		f_test_offset = f_test->f_pos;
	}
#endif 
	return ret;
}

/********************************************************\
 * release
\********************************************************/
int xb_snd_dsp_release(struct inode *inode,
					   struct file *file,
					   struct snd_dev_data *ddata)
{
	int ret = -EINVAL;
	struct dsp_pipe *dpi = NULL;
	struct dsp_pipe *dpo = NULL;
	struct dsp_node *node = NULL;
	struct dsp_endpoints *endpoints = NULL;

	if (ddata == NULL)
		return -1;

	endpoints = (struct dsp_endpoints *)ddata->ext_data;
	if (endpoints == NULL)
		return -1;

	if (file->f_mode & FMODE_READ) {
		dpi = endpoints->in_endpoint;
		if (dpi->is_trans == true)
			dpi->wait_stop_dma = true;
	}

	if (file->f_mode & FMODE_WRITE) {
		dpo = endpoints->out_endpoint;
		if (dpo->is_trans == true)
			dpo->wait_stop_dma = true;
	}

	if (dpi && dpi->wait_stop_dma == true) {
		wait_event_interruptible(dpi->wq, dpi->is_trans == false);
		if (dpi->sg) {
			vfree(dpi->sg);
			dpi->sg = NULL;
		}
	}
	if (dpo && dpo->wait_stop_dma == true) {
		wait_event_interruptible(dpo->wq, dpo->is_trans == false);
		if (dpo->sg) {
		vfree(dpo->sg);
		dpo->sg = NULL;
	}
	}
	if (dpi) {
		/* put all used node to free node list */
		while(1) {
			node = NULL;
			node = get_use_dsp_node(dpi);
			if (!node)
				break;
			put_free_dsp_node(dpi, node);
			dpi->avialable_couter--;
		};

		if (ddata->dev_ioctl) {
			ret = (int)ddata->dev_ioctl(SND_DSP_DISABLE_DMA_RX, 0);
			ret |= (int)ddata->dev_ioctl(SND_DSP_DISABLE_RECORD, 0);
			if (ret)
				return -EFAULT;
		}
		dpi->is_used = false;
	}

	if (dpo) {
		/* put all used node to free node list */
		while(1) {
			node = NULL;
			node = get_use_dsp_node(dpo);
			if (!node)
				break;
			put_free_dsp_node(dpo, node);
			dpo->avialable_couter++;
		};

		if (ddata->dev_ioctl) {
			ret = (int)ddata->dev_ioctl(SND_DSP_DISABLE_DMA_TX, 0);
			ret |= (int)ddata->dev_ioctl(SND_DSP_DISABLE_REPLAY, 0);
			if (ret)
				return -EFAULT;
		}
		dpo->is_used = false;
	}
#if DEBUG_REPLAY 
	printk("DEBUG:----close /data/record_test.pcm-%s\tline:%d\n",__func__,__LINE__);
	if (!IS_ERR(f_test))
		filp_close(f_test, NULL);
#endif   
	return 0;
}

/********************************************************\
 * xb_snd_probe
\********************************************************/
int xb_snd_dsp_probe(struct snd_dev_data *ddata)
{
	int ret = -1;
	struct dsp_pipe *dp = NULL;
	struct dsp_endpoints *endpoints = NULL;

	if (ddata == NULL)
		return -1;

	endpoints = (struct dsp_endpoints *)ddata->ext_data;
	if (endpoints == NULL)
		return -1;

	/* out_endpoint init */
	if ((dp = endpoints->out_endpoint) != NULL) {
		ret = init_pipe(dp , ddata->dev,DMA_TO_DEVICE);
		if (ret)
			goto error1;
	}

	/* in_endpoint init */
	if ((dp = endpoints->in_endpoint) != NULL) {
		ret = init_pipe(dp , ddata->dev,DMA_FROM_DEVICE);
		if (ret)
			goto error2;
	}
	if (!spipe_is_init)
		spipe_init(ddata->dev);
	spipe_is_init = 1;

	return 0;

error2:
	deinit_pipe(endpoints->out_endpoint, ddata->dev);
error1:
	return ret;
}
