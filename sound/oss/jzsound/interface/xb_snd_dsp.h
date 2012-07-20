/**
 * xb_snd_dsp.h
 *
 * jbbi <jbbi@ingenic.cn>
 *
 * 24 APR 2012
 *
 */

#ifndef __XB_SND_DSP_H__
#define __XB_SND_DSP_H__

#include <linux/dmaengine.h>
#include <linux/wait.h>
#include <linux/scatterlist.h>
#include <linux/spinlock.h>
#include <linux/poll.h>
#include <mach/jzdma.h>
#include <mach/jzsnd.h>
#include <linux/dma-mapping.h>

/*####################################################*\
 * sound pipe and command used for dsp device
\*####################################################*/
/**
 * sound device
 **/
enum snd_device_t {
    SND_DEVICE_DEFAULT = 0,
    SND_DEVICE_CURRENT,
    SND_DEVICE_HANDSET,
    SND_DEVICE_HEADSET,
    SND_DEVICE_SPEAKER,
    SND_DEVICE_BT,
    SND_DEVICE_BT_EC_OFF,
    SND_DEVICE_HEADSET_AND_SPEAKER,
    SND_DEVICE_TTY_FULL,
    SND_DEVICE_CARKIT,
    SND_DEVICE_FM_SPEAKER,
    SND_DEVICE_FM_HEADSET,
    SND_DEVICE_NO_MIC_HEADSET,
    SND_DEVICE_HDMI,
    SND_DEVICE_COUNT
};
/**
 * extern ioctl command for dsp device
 **/
struct direct_info {
    int bytes;
    int offset;
};

enum spipe_mode_t {
    SPIPE_MODE_RECORD,
    SPIPE_MODE_REPLAY,
};

struct spipe_info {
    int spipe_id;
    enum spipe_mode_t spipe_mode;
};

#define SNDCTL_EXT_SET_DEVICE               _SIOR ('P', 99, int)
#define SNDCTL_EXT_SET_STANDBY              _SIOR ('P', 98, int)
#define SNDCTL_EXT_START_BYPASS_TRANS       _SIOW ('P', 97, struct spipe_info)
#define SNDCTL_EXT_STOP_BYPASS_TRANS        _SIOW ('P', 96, struct spipe_info)
#define SNDCTL_EXT_DIRECT_GETINODE          _SIOR ('P', 95, struct direct_info)
#define SNDCTL_EXT_DIRECT_PUTINODE          _SIOW ('P', 94, struct direct_info)
#define SNDCTL_EXT_DIRECT_GETONODE          _SIOR ('P', 93, struct direct_info)
#define SNDCTL_EXT_DIRECT_PUTONODE          _SIOW ('P', 92, struct direct_info)

/**
 * dsp device control command
 **/
enum snd_dsp_command {
	/**
	 * the command flowed is used to enable/disable
	 * replay/record.
	 **/
	SND_DSP_ENABLE_REPLAY,
	SND_DSP_DISABLE_REPLAY,
	SND_DSP_ENABLE_RECORD,
	SND_DSP_DISABLE_RECORD,
	/**
	 * the command flowed is used to enable/disable the
	 * dma transfer on device.
	 **/
	SND_DSP_ENABLE_DMA_RX,
	SND_DSP_DISABLE_DMA_RX,
	SND_DSP_ENABLE_DMA_TX,
	SND_DSP_DISABLE_DMA_TX,
	/**
	 *@SND_DSP_SET_XXXX_RATE is used to set replay/record rate
	 **/
	SND_DSP_SET_REPLAY_RATE,
	SND_DSP_SET_RECORD_RATE,
	/**
	 * @SND_DSP_SET_XXXX_CHANNELS is used to set replay/record
	 * channels, when channels changed, filter maybe also need
	 * changed to a fix value.
	 **/
	SND_DSP_SET_REPLAY_CHANNELS,
	SND_DSP_SET_RECORD_CHANNELS,
	/**
	 * @SND_DSP_GET_XXXX_FMT_CAP is used to get formats that
	 * replay/record supports.
	 * @SND_DSP_GET_XXXX_FMT used to get current replay/record
	 * format.
	 * @SND_DSP_SET_XXXX_FMT is used to set replay/record format,
	 * if the format changed, trigger,dma max_tsz and filter maybe
	 * also need changed to a fix value. and let them effect.
	 **/
	SND_DSP_GET_REPLAY_FMT_CAP,
	SND_DSP_GET_REPLAY_FMT,
	SND_DSP_SET_REPLAY_FMT,
	SND_DSP_GET_RECORD_FMT_CAP,
	SND_DSP_GET_RECORD_FMT,
	SND_DSP_SET_RECORD_FMT,
	/**
	 * @SND_DSP_SET_DEVICE is used to set audio route
	 * @SND_DSP_SET_STANDBY used to set into/release from stanby
	 **/
	SND_DSP_SET_DEVICE,
	SND_DSP_SET_STANDBY,
};

/**
 *fragsize, must be dived by PAGE_SIZE
 **/
#define FRAGSIZE_S  (PAGE_SIZE >> 1)
#define FRAGSIZE_M  (PAGE_SIZE)
#define FRAGSIZE_L  (PAGE_SIZE << 1)

#define FRAGCNT_S   2
#define FRAGCNT_M   4
#define FRAGCNT_L   6



struct dsp_node {
	struct list_head    list;
	unsigned long       pBuf;
	unsigned int        start;
	unsigned int        end;
	dma_addr_t      phyaddr;
	size_t          size;
};

struct dsp_pipe {
	/* dma */
	struct dma_chan     *dma_chan;
	struct dma_slave_config dma_config;     /* define by device */
	enum jzdma_type     dma_type;
	unsigned int        sg_len;         /* size of scatter list */
	struct scatterlist  *sg;            /* I/O scatter list */
	/* buf */
	unsigned long       vaddr;
	dma_addr_t          paddr;
	size_t              fragsize;              /* define by device */
	size_t              fragcnt;               /* define by device */
	struct list_head    free_node_list;
	struct list_head    use_node_list;
	struct dsp_node     *save_node;
	wait_queue_head_t   wq;
	int                 avialable_couter;
	/* state */
	volatile bool       is_trans;
	volatile bool       wait_stop_dma;
	volatile bool       need_reconfig_dma;
	volatile bool       is_used;
	volatile bool       is_shared;
	volatile bool       is_mmapd;
	bool            is_non_block;          /* define by device */
	bool            can_mmap;              /* define by device */
	/* callback funs */
	void (*handle)(struct dsp_pipe *endpoint); /* define by device */
	int (*filter)(void *buff, int cnt);        /* define by device */
	/* lock */
	spinlock_t          pipe_lock;
};

struct dsp_endpoints {
    struct dsp_pipe *out_endpoint;
    struct dsp_pipe *in_endpoint;
};




/**
 * filter
 **/
int convert_8bits_signed2unsigned(void *buffer, int counter);
int convert_8bits_stereo2mono(void *buff, int data_len);
int convert_8bits_stereo2mono_signed2unsigned(void *buff, int data_len);
int convert_16bits_stereo2mono(void *buff, int data_len);
int convert_16bits_stereomix2mono(void *buff, int data_len);

/**
 * functions interface
 **/
loff_t xb_snd_dsp_llseek(struct file *file,
						 loff_t offset,
						 int origin,
						 struct snd_dev_data *ddata);

ssize_t xb_snd_dsp_read(struct file *file,
						char __user *buffer,
						size_t count,
						loff_t *ppos,
						struct snd_dev_data *ddata);

ssize_t xb_snd_dsp_write(struct file *file,
						 const char __user *buffer,
						 size_t count,
						 loff_t *ppos,
						 struct snd_dev_data *ddata);

unsigned int xb_snd_dsp_poll(struct file *file,
							   poll_table *wait,
							   struct snd_dev_data *ddata);

long xb_snd_dsp_ioctl(struct file *file,
					  unsigned int cmd,
					  unsigned long arg,
					  struct snd_dev_data *ddata);

int xb_snd_dsp_mmap(struct file *file,
					struct vm_area_struct *vma,
					struct snd_dev_data *ddata);

int xb_snd_dsp_open(struct inode *inode,
					struct file *file,
					struct snd_dev_data *ddata);

int xb_snd_dsp_release(struct inode *inode,
					   struct file *file,
					   struct snd_dev_data *ddata);

int xb_snd_dsp_probe(struct snd_dev_data *ddata);

#endif //__XB_SND_DSP_H__
