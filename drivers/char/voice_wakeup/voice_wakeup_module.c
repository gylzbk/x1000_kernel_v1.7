#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/kernel.h>	/* printk() */
#include <linux/slab.h>		/* kmalloc() */
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/types.h>	/* size_t */
#include <linux/proc_fs.h>
#include <linux/fcntl.h>	/* O_ACCMODE */
#include <linux/seq_file.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/sched.h>
#include <linux/io.h>
#include <linux/memory.h>
#include <linux/mm.h>
#include <asm/cacheops.h>
#include <linux/dma-mapping.h>

#include <linux/voice_wakeup_module.h>

#define FIRMWARE_LOAD_ADDRESS	0xaff00000

static int wakeup_firmware [] = {

#include "voice_wakeup_firmware.hex"

};


struct wakeup_module_ops {
	/*pay attention to the order*/
	/*wakeup module for host*/
	int (*open)(int mode);
	int (*handler)(int args);
	int (*close)(int mode);
	int (*cache_prefetch)(void);

	/*host for wakeup module*/
	int (*set_handler)(void *);
	dma_addr_t (*get_dma_address)(void);
	int (*ioctl)(int cmd, unsigned long args);

};

struct wakeup_module_ops *m_ops;


static void dump_firmware(void)
{
	int i;
	unsigned int *p = (unsigned int *)FIRMWARE_LOAD_ADDRESS;
	printk("###################dump_firmware begine################\n");
	for(i = 0; i < 64; i++) {
		printk("1.%p:%08x\n", &wakeup_firmware[i], wakeup_firmware[i]);
		printk("2.%p:%08x\n", p+i, *(p+i));
	}
	printk("###################dump_firmware end################\n");

}

static void setup_ops(void)
{
	printk("###############setup_ops##############\n");
	m_ops = (struct wakeup_module_ops *)FIRMWARE_LOAD_ADDRESS;
	m_ops->set_handler(printk);
	printk("open:%p\n", m_ops->open);
	printk("handler:%p\n", m_ops->handler);
	printk("close:%p\n", m_ops->close);
	printk("set_handler:%p\n", m_ops->set_handler);
	printk("###############ops end##############\n");
}

void test_ops(void)
{
	printk("printk:%p", printk);
	m_ops->set_handler(printk);
	printk("###############test_ops##############\n");
	printk("m_ops.open:%x\n", m_ops->open(1));
	printk("m_ops.handler:%x\n", m_ops->handler(1));
	printk("m_ops.close:%x\n", m_ops->close(1));
	printk("###############ops end##############\n");
}



int wakeup_module_open(int mode)
{
	return m_ops->open(mode);
}
EXPORT_SYMBOL(wakeup_module_open);

int wakeup_module_handler(int par)
{
	return m_ops->handler(par);
}
EXPORT_SYMBOL(wakeup_module_handler);

int wakeup_module_close(int mode)
{
	return m_ops->close(mode);
}
EXPORT_SYMBOL(wakeup_module_close);

void wakeup_module_cache_prefetch(void)
{
	m_ops->cache_prefetch();
}
EXPORT_SYMBOL(wakeup_module_cache_prefetch);

dma_addr_t wakeup_module_get_dma_address(void)
{
	return	m_ops->get_dma_address();
}
EXPORT_SYMBOL(wakeup_module_get_dma_address);

int wakeup_module_ioctl(int cmd, unsigned long args)
{

	return m_ops->ioctl(cmd, args);
}
EXPORT_SYMBOL(wakeup_module_ioctl);

static int __init wakeup_module_init(void)
{
	/* load voice wakeup firmware */
	memcpy(FIRMWARE_LOAD_ADDRESS, wakeup_firmware, sizeof(wakeup_firmware));
	setup_ops();
	return 0;
}
static void __exit wakeup_module_exit(void)
{


}

module_init(wakeup_module_init);
module_exit(wakeup_module_exit);

MODULE_AUTHOR("qipengzhen<aric.pzqi@ingenic.com>");
MODULE_DESCRIPTION("voice wakeup module driver");
MODULE_LICENSE("GPL");
