/*
 * Copyright (C) 2005-2013, Ingenic Semiconductor Inc.
 * http://www.ingenic.cn/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/wakelock.h>
#include <linux/kthread.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/fs.h>

#include <soc/gpio.h>
#include <soc/irq.h>

#include <mach/jzepd.h>

#include "jz4775_android_epd.h"
#include "epdc_regs.h"
#include "epd_panel/epd_panel_info.h"

#define	CURBUF_NUM	1
#define EPD_BPP		4

#define DRIVER_NAME	"jz4775-epd"

#define	MAX_FRAME_COUNT	256
#define	SIZE_PER_FRAME	64
#define WFM_LUT_SIZE	MAX_FRAME_COUNT * SIZE_PER_FRAME

#define WAIT_INTERRUPT_STATUS(condition)	{\
	int timeout = 2000; \
	while(!(condition) && timeout){ \
		mdelay(1);\
		timeout--; \
	}; \
}

#define DEBUG 0
#if DEBUG
#define dprintk(format, arg...) printk(KERN_ALERT format, ## arg)
#else
#define dprintk(format, arg...) do {} while (0)
#endif

#define	EPDC_WFM_DEBUG	0
#define	EPDC_LUT_DEBUG	0

//#define EPDC_VSYNC_HARDWARE
#define EPDC_VSYNC_TIME

static volatile unsigned char sta_lut_done;
static volatile unsigned char sta_ref_start;
static volatile unsigned char sta_ref_stop;
static volatile unsigned char sta_pwron;
static volatile unsigned char sta_pwroff = 1;

static unsigned char *frame_buffer	= NULL;
static unsigned char *current_buffer	= NULL;
static unsigned char *snooping_buffer	= NULL;
static unsigned char *working_buffer	= NULL;
static unsigned char *wfm_lut_buf	= NULL;
extern unsigned char WAVEFORM_LUT[];

static unsigned char *wfm_buf	= NULL;				// LUT buffer
static unsigned char *snp_buf	= NULL;				// refresh command buffer
static unsigned char *wrk_buf	= NULL;				// wrk0_buf + wrk1_buf
static unsigned char *wrk0_buf	= NULL;				// New + Old pix buffer
static unsigned char *wrk1_buf	= NULL;				// Pix cnt buffer

static unsigned int frm_buf_order;
static unsigned int frm_buf_size;
static unsigned int cur_buf_size;
static unsigned int wfm_buf_size;
static unsigned int snp_buf_size;
static unsigned int wrk_buf_size;
static unsigned int wrk0_buf_size;
static unsigned int wrk1_buf_size;

static unsigned int epd_temp = 8;
static unsigned int frame_num;

static unsigned int ref_times = 0;
static unsigned int parallel_refresh_interval_times = 200;

/*static struct timer_list temp_timer;*/
static struct epd_panel_info panel_info;

typedef enum {
	MODE_INIT,
	MODE_DU,
	MODE_GC16,
	MODE_EMPTY,
	MODE_A2,
	MODE_GL16,
	MODE_A2IN,
	MODE_A2OUT,
	MODE_GC16GU,
	MODE_AUTO_DU,
	MODE_AUTO_DUGC4,
}epd_mode_e;
static epd_mode_e epd_mode_set = MODE_GC16GU;

typedef enum{
	PARALLEL_REF,
	PARTIAL_REF
}ref_mode_e;

extern int jz4775_epdce_rgb565_to_16_grayscale(struct fb_var_screeninfo *fb_var,
		unsigned char *frame_buffer, unsigned char *current_buffer);

static int jz4775fb_open(struct fb_info *info, int user)
{
	dprintk("Open the framebuffer %s\n", info->fix.id);
	return 0;
}

static int jz4775fb_release(struct fb_info *info, int user)
{
	dprintk("Close the framebuffer %s\n", info->fix.id);
	return 0;
}
/*
static void jz4775fb_print_palette(void *palette, int len)
{
	unsigned short *pal = (unsigned short *)palette;
	int i;
	dprintk("palette:\n");
	for (i = 0; i < len; i++) {
		if ((i * 8) % 256 == 0)
			dprintk("\nfrm%d:\t", i * 8 / 256);
		if (i % 16 == 0)
			dprintk("\n\t");
		dprintk("%04x ", pal[i]);
	}
	dprintk("\n");
}
*/
static void print_fb_info(struct fb_info *info)
{
	dprintk("info->node		= %d\n", info->node);
	dprintk("info->flags		= %d\n", info->flags);
	dprintk("info->screen_base	= %s\n", info->screen_base);
	dprintk("info->screen_size	= 0x%lx\n", info->screen_size);
	dprintk("info->state		= %d\n\n", info->state);

	dprintk("info->var.xres		= %d\n", info->var.xres);
	dprintk("info->var.yres		= %d\n", info->var.yres);
	dprintk("info->var.xres_virtual	= %d\n", info->var.xres_virtual);
	dprintk("info->var.yres_virtual	= %d\n", info->var.yres_virtual);
	dprintk("info->var.xoffset	= %d\n", info->var.xoffset);
	dprintk("info->var.yoffset	= %d\n", info->var.yoffset);
	dprintk("info->var.bits_per_pixel	= %d\n", info->var.bits_per_pixel);
	dprintk("info->var.grayscale	= %d\n", info->var.grayscale);
	dprintk("info->var.red.offset	= %d\n", info->var.red.offset);
	dprintk("info->var.red.length	= %d\n", info->var.red.length);
	dprintk("info->var.red.msb_right	= %d\n", info->var.red.msb_right);
	dprintk("info->var.green.offset	= %d\n", info->var.green.offset);
	dprintk("info->var.green.length	= %d\n", info->var.green.length);
	dprintk("info->var.green.msb_right	= %d\n", info->var.green.msb_right);
	dprintk("info->var.blue.offset	= %d\n", info->var.blue.offset);
	dprintk("info->var.blue.length	= %d\n", info->var.blue.length);
	dprintk("info->var.blue.msb_right	= %d\n", info->var.blue.msb_right);
	dprintk("info->var.trans.offset	= %d\n", info->var.transp.offset);
	dprintk("info->var.trans.length	= %d\n", info->var.transp.length);
	dprintk("info->var.trans.msb_right	= %d\n", info->var.transp.msb_right);
	dprintk("info->var.nonstd	= %d\n", info->var.nonstd);
	dprintk("info->var.activate	= %d\n", info->var.activate);
	dprintk("info->var.height	= %d\n", info->var.height);
	dprintk("info->var.width		= %d\n", info->var.width );
	dprintk("info->var.accel_flags	= %d\n", info->var.accel_flags);
	dprintk("info->var.pixclock	= %d\n", info->var.pixclock);
	dprintk("info->var.left_margin	= %d\n", info->var.left_margin);
	dprintk("info->var.right_margin	= %d\n", info->var.right_margin);
	dprintk("info->var.upper_margin	= %d\n", info->var.upper_margin);
	dprintk("info->var.lower_margin	= %d\n", info->var.lower_margin);
	dprintk("info->var.hsync_len	= %d\n", info->var.hsync_len);
	dprintk("info->var.vsync_len	= %d\n", info->var.vsync_len);
	dprintk("info->var.sync		= %d\n", info->var.sync);
	dprintk("info->var.vmode		= %d\n", info->var.vmode);
	dprintk("info->var.rotate	= %d\n\n", info->var.rotate);

	dprintk("info->fix.id		= %s\n", info->fix.id);
	dprintk("info->fix.smem_start	= 0x%lx\n", info->fix.smem_start);
	dprintk("info->fix.smem_len	= %d\n", info->fix.smem_len);
	dprintk("info->fix.type		= %d\n", info->fix.type);
	dprintk("info->fix.type_aux	= %d\n", info->fix.type_aux);
	dprintk("info->fix.visual	= %d\n", info->fix.visual);
	dprintk("info->fix.xpanstep	= %d\n", info->fix.xpanstep);
	dprintk("info->fix.ypanstep	= %d\n", info->fix.ypanstep);
	dprintk("info->fix.ywrapstep	= %d\n", info->fix.ywrapstep);
	dprintk("info->fix.line_length	= %d\n", info->fix.line_length);
	dprintk("info->fix.mmio_start	= 0x%lx\n", info->fix.mmio_start);
	dprintk("info->fix.mmio_len	= %d\n", info->fix.mmio_len);
	dprintk("info->fix.accel		= %d\n", info->fix.accel);
}

static void print_epdc_registers(void)
{
	dprintk("REG_EPD_CTRL		= 0x%08x\n", REG_EPD_CTRL);
	dprintk("REG_EPD_CFG 		= 0x%08x\n", REG_EPD_CFG);
	dprintk("REG_EPD_STA 		= 0x%08x\n", REG_EPD_STA);
	dprintk("REG_EPD_ISRC		= 0x%08x\n", REG_EPD_ISRC);
	dprintk("REG_EPD_DIS0		= 0x%08x\n", REG_EPD_DIS0);
	dprintk("REG_EPD_DIS1		= 0x%08x\n", REG_EPD_DIS1);
	dprintk("REG_EPD_SIZE		= 0x%08x\n", REG_EPD_SIZE);
	dprintk("REG_EPD_VAT		= 0x%08x\n", REG_EPD_VAT);
	dprintk("REG_EPD_DAV		= 0x%08x\n", REG_EPD_DAV);
	dprintk("REG_EPD_DAH		= 0x%08x\n", REG_EPD_DAH);
	dprintk("REG_EPD_VSYNC		= 0x%08x\n", REG_EPD_VSYNC);
	dprintk("REG_EPD_HSYNC		= 0x%08x\n", REG_EPD_HSYNC);
	dprintk("REG_EPD_GDCLK		= 0x%08x\n", REG_EPD_GDCLK);
	dprintk("REG_EPD_GDOE		= 0x%08x\n", REG_EPD_GDOE);
	dprintk("REG_EPD_GDSP		= 0x%08x\n", REG_EPD_GDSP);
	dprintk("REG_EPD_SDOE		= 0x%08x\n", REG_EPD_SDOE);
	dprintk("REG_EPD_SDSP		= 0x%08x\n", REG_EPD_SDSP);
	dprintk("REG_EPD_PMGR0		= 0x%08x\n", REG_EPD_PMGR0);
	dprintk("REG_EPD_PMGR1		= 0x%08x\n", REG_EPD_PMGR1);
	dprintk("REG_EPD_PMGR2		= 0x%08x\n", REG_EPD_PMGR2);
	dprintk("REG_EPD_PMGR3		= 0x%08x\n", REG_EPD_PMGR3);
	dprintk("REG_EPD_PMGR4		= 0x%08x\n", REG_EPD_PMGR4);
	dprintk("REG_EPD_LUTBF		= 0x%08x\n", REG_EPD_LUTBF);
	dprintk("REG_EPD_LUTSIZE		= 0x%08x\n", REG_EPD_LUTSIZE);
	dprintk("REG_EPD_CURBF		= 0x%08x\n", REG_EPD_CURBF);
	dprintk("REG_EPD_CURSIZE		= 0x%08x\n", REG_EPD_CURSIZE);
	dprintk("REG_EPD_WRK0BF		= 0x%08x\n", REG_EPD_WRK0BF);
	dprintk("REG_EPD_WRK0SIZE	= 0x%08x\n", REG_EPD_WRK0SIZE);
	dprintk("REG_EPD_WRK1BF		= 0x%08x\n", REG_EPD_WRK1BF);
	dprintk("REG_EPD_WRK1SIZE	= 0x%08x\n", REG_EPD_WRK1SIZE);
	dprintk("REG_EPD_PRI		= 0x%08x\n", REG_EPD_PRI);
	dprintk("REG_EPD_VCOMBD0		= 0x%08x\n", REG_EPD_VCOMBD0);
	dprintk("REG_EPD_VCOMBD1		= 0x%08x\n", REG_EPD_VCOMBD1);
	dprintk("REG_EPD_VCOMBD2		= 0x%08x\n", REG_EPD_VCOMBD2);
	dprintk("REG_EPD_VCOMBD3		= 0x%08x\n", REG_EPD_VCOMBD3);
	dprintk("REG_EPD_VCOMBD4		= 0x%08x\n", REG_EPD_VCOMBD4);
	dprintk("REG_EPD_VCOMBD5		= 0x%08x\n", REG_EPD_VCOMBD5);
	dprintk("REG_EPD_VCOMBD6		= 0x%08x\n", REG_EPD_VCOMBD6);
	dprintk("REG_EPD_VCOMBD7		= 0x%08x\n", REG_EPD_VCOMBD7);
	dprintk("REG_EPD_VCOMBD8		= 0x%08x\n", REG_EPD_VCOMBD8);
	dprintk("REG_EPD_VCOMBD9		= 0x%08x\n", REG_EPD_VCOMBD9);
	dprintk("REG_EPD_VCOMBD10	= 0x%08x\n", REG_EPD_VCOMBD10);
	dprintk("REG_EPD_VCOMBD11	= 0x%08x\n", REG_EPD_VCOMBD11);
	dprintk("REG_EPD_VCOMBD12	= 0x%08x\n", REG_EPD_VCOMBD12);
	dprintk("REG_EPD_VCOMBD13	= 0x%08x\n", REG_EPD_VCOMBD13);
	dprintk("REG_EPD_VCOMBD14	= 0x%08x\n", REG_EPD_VCOMBD14);
	dprintk("REG_EPD_VCOMBD15	= 0x%08x\n", REG_EPD_VCOMBD15);
	dprintk("REG_DDR_SNP_HIGH	= 0x%08x\n", REG_DDR_SNP_HIGH);
	dprintk("REG_DDR_SNP_LOW		= 0x%08x\n", REG_DDR_SNP_LOW);
}

/*
 * Check the ambient temperature.
 */
/*static void jz4775fb_get_epd_temp(unsigned long arg)
{
	int m;
	int sensor_temp, actual_temp;
	sensor_temp = get_lm75_temp();
	actual_temp = sensor_temp/1000;

	for(m = 0; m <= panel_info.wfm_trc; m++)
	{
		if((actual_temp > panel_info.wfm_tr[m]) && (actual_temp < panel_info.wfm_tr[m + 1]))
		{
			epd_temp = m;
			epd_temp -=1;
			if(epd_temp < 0)
				epd_temp = 0;
		}
	}
	dprintk("sensor_temp = %d actual_temp = %d epd_temp = %d\n",sensor_temp, actual_temp, epd_temp);
	mod_timer(&temp_timer, jiffies + HZ);
}
*/
/* Temperature timing check */
/*static void start_temperature_check(struct timer_list *timer, unsigned int time)
{
	init_timer(timer);
	timer->function = jz4775fb_get_epd_temp;
	timer->expires = jiffies + HZ;
	timer->data	= time;
	add_timer(timer);
}*/

static void jz4775fb_fill_wfm_lut(epd_mode_e epd_mode)
{
	int i;
#if (EPDC_WFM_DEBUG | EPDC_LUT_DEBUG)
	int j;
#endif
	unsigned int pos = 512;

	unsigned char *waveform_addr;
	unsigned int waveform_addr_index, mode_addr_index;

	unsigned char rvdata;
	unsigned char g0, g1, g2, g3, w;

	if(wfm_buf[0] == 0xc5)		//From PVI
	{
		if(epd_mode == MODE_GC16GU)
			epd_mode = MODE_GC16;

		memset(wfm_lut_buf, 0x00, WFM_LUT_SIZE);
		dma_cache_wback_inv((unsigned int)wfm_lut_buf, WFM_LUT_SIZE);

		waveform_addr_index =	(wfm_buf[pos / 2 + 64 * epd_mode + 4 * (epd_temp) + 2] << 16)	|
			(wfm_buf[pos / 2 + 64 * epd_mode + 4 * (epd_temp) + 1] << 8)	|
			(wfm_buf[pos / 2 + 64 * epd_mode + 4 * (epd_temp) + 0] << 0);

		waveform_addr = &wfm_buf[waveform_addr_index];

		frame_num = wfm_buf[pos / 2 + 64 * epd_mode + 4 * (epd_temp) + 3];

		dprintk("PVI epd_mode = %d, epd_temp = %d\t", epd_mode, epd_temp);
		dprintk("waveform_addr = 0x%x\n", (unsigned int)waveform_addr);
		dprintk("frame_num = %d\n", frame_num);

		memcpy(wfm_lut_buf, waveform_addr, SIZE_PER_FRAME * frame_num);
		dma_cache_wback_inv((unsigned int)wfm_lut_buf, WFM_LUT_SIZE);
	}
	else if(wfm_buf[0] == 0xa2)		//From OED
	{
		epd_mode = 2;
		epd_temp = 8;
		waveform_addr_index = wfm_buf[pos + 0x30 + 5 * epd_temp + 1] << 24 |
			wfm_buf[pos + 0x30 + 5 * epd_temp + 2] << 16 |
			wfm_buf[pos + 0x30 + 5 * epd_temp + 3] << 8 |
			wfm_buf[pos + 0x30 + 5 * epd_temp + 4];
		frame_num = wfm_buf[pos + waveform_addr_index + 6 * epd_mode] << 8 | wfm_buf[pos + waveform_addr_index + 6 * epd_mode + 1];
		mode_addr_index = wfm_buf[pos + waveform_addr_index + 6 * epd_mode + 2] << 24 |
			wfm_buf[pos + waveform_addr_index + 6 * epd_mode + 3] << 16 |
			wfm_buf[pos + waveform_addr_index + 6 * epd_mode + 4] << 8 |
			wfm_buf[pos + waveform_addr_index + 6 * epd_mode + 5];
		waveform_addr = &wfm_buf[pos + waveform_addr_index + mode_addr_index];

		memcpy(wfm_lut_buf, waveform_addr, SIZE_PER_FRAME * frame_num);

		for(i = 0; i < SIZE_PER_FRAME * frame_num; i++)
		{
			w = wfm_lut_buf[i];
			g0 = (w >> 6) & 0x3;
			g1 = (w >> 4) & 0x3;
			g2 = (w >> 2) & 0x3;
			g3 = (w >> 0) & 0x3;

			rvdata =  (g3 << 6) | (g2 << 4) | (g1 << 2) | (g0 << 0);

			wfm_lut_buf[i] = rvdata;
		}

		dma_cache_wback_inv((unsigned int)wfm_lut_buf, WFM_LUT_SIZE);
		dprintk("OED epd_mode = %d  epd_temp = %d\t", epd_mode, epd_temp);
		dprintk("mode_addr_index = 0x%x\n", mode_addr_index);
		dprintk("waveform_addr = 0x%p\n", waveform_addr);
		dprintk("max_frm = %d\n", frame_num);
	}
	else
	{
		dprintk("Not Ingenic waveform mode, check it with R&D\n");
		return;
	}


#if EPDC_WFM_DEBUG
	msleep(500);
	for(i = 0; i < frame_num; i++)
	{
		dprintk("Frame index: %d", i);
		for(j =0; j < 64; j++)
		{
			if(j % 16 == 0)
				dprintk("\n");
			dprintk("0x%02x ", wfm_lut_buf[i * 64 + j]);
		}
		dprintk("\n\n");
	}
#endif

#if EPDC_LUT_DEBUG
	for(i = 0; i < 64; i++)
	{
		dprintk("\nGray%d ~ Gray%d:\t", (i % 4) * 4 + 0, i / 4);
		for(j = 0; j < frame_num; j++)
		{
			if(j % 32 == 0)
				dprintk("\n");
			dprintk("%d ", (wfm_lut_buf[SIZE_PER_FRAME * j + i] & 0x3) >> 0);
		}

		dprintk("\nGray%d ~ Gray%d:\t", (i % 4) * 4 + 1, i / 4);
		for(j = 0; j < frame_num; j++)
		{
			if(j % 32 == 0)
				dprintk("\n");
			dprintk("%d ", (wfm_lut_buf[SIZE_PER_FRAME * j + i] & 0xc) >> 2);
		}

		dprintk("\nGray%d ~ Gray%d:\t", (i % 4) * 4 + 2, i / 4);
		for(j = 0; j < frame_num; j++)
		{
			if(j % 32 == 0)
				dprintk("\n");
			dprintk("%d ", (wfm_lut_buf[SIZE_PER_FRAME * j + i] & 0x30) >> 4);
		}

		dprintk("\nGray%d ~ Gray%d:\t", (i % 4) * 4 + 3, i / 4);
		for(j = 0; j < frame_num; j++)
		{
			if(j % 32 == 0)
				dprintk("\n");
			dprintk("%d ", (wfm_lut_buf[SIZE_PER_FRAME * j + i] & 0xc0) >> 6);
		}
	}
#endif
}

static int jz4775fb_epd_mode_set(epd_mode_e epd_mode)
{
	epd_mode_e mode  = epd_mode;

	if(mode == MODE_INIT)
	{
		dprintk("Set epd mode MODE_INIT\n");
	}
	else if(mode == MODE_DU)
	{
		dprintk("Set epd mode MODE_DU\n");
	}
	else if(mode == MODE_GC16GU)
	{
		dprintk("Set epd mode MODE_GC16GU\n");
	}
	else if(mode == MODE_GC16)
	{
		dprintk("Set epd mode MODE_GC16\n");
	}
	else if(mode == MODE_EMPTY)
	{
		dprintk("Set epd mode MODE_EMPTY\n");
		return -1;
	}
	else if(mode == MODE_A2)
	{
		dprintk("Set epd mode MODE_A2\n");
	}
	else if(mode == MODE_GL16)
	{
		dprintk("Set epd mode MODE_GL16\n");
	}
	else if(mode == MODE_A2IN)
	{
		dprintk("Set epd mode MODE_A2IN\n");
	}
	else if(mode == MODE_A2OUT)
	{
		dprintk("Set epd mode MODE_A2OUT\n");
	}
	else if(mode == MODE_AUTO_DU)
	{
		dprintk("Set epd mode MODE_AUTO_DU\n");
	}
	else if(mode == MODE_AUTO_DUGC4)
	{
		dprintk("Set epd mode MODE_AUTO_DUGC4\n");
	}
	else
	{
		dprintk("\nSet epd mode invalid!\n");
		return -1;
	}
	return 0;
}

static int jz4775fb_load_wfm_lut(epd_mode_e epd_mode)
{
	WAIT_INTERRUPT_STATUS(REG_EPD_STA & EPD_STA_EPD_IDLE);

	REG_EPD_CFG &= 0x1f;
	REG_EPD_CFG |= EPD_CFG_LUT_1MODE;

	jz4775fb_fill_wfm_lut(epd_mode);
	REG_EPD_LUTBF	= (unsigned int)virt_to_phys(wfm_lut_buf);
	REG_EPD_LUTSIZE	= EPD_LUT_POS0 | (frame_num * 64 / 4);
	REG_EPD_CFG	|= (frame_num - 1) << EPD_CFG_STEP;
	sta_lut_done = 0;
	REG_EPD_CTRL	|= EPD_CTRL_LUT_STRT ;
	WAIT_INTERRUPT_STATUS(sta_lut_done == 1)
	return 0;
}

static int jz4775fb_load_auto_du_wfm_lut(void)
{
	WAIT_INTERRUPT_STATUS(REG_EPD_STA & EPD_STA_EPD_IDLE);

	REG_EPD_CFG &= 0x1f;
	REG_EPD_CFG |= EPD_CFG_LUT_2MODE;

	/* 1, load du lut */
	jz4775fb_fill_wfm_lut(MODE_DU);
	REG_EPD_LUTBF	= (unsigned int)virt_to_phys(wfm_lut_buf);
	REG_EPD_LUTSIZE	= EPD_LUT_POS0 | (frame_num * 64 / 4);
	REG_EPD_CFG	|= (frame_num - 1) << EPD_CFG_STEP1;
	sta_lut_done = 0;
	REG_EPD_CTRL	|= EPD_CTRL_LUT_STRT;
	WAIT_INTERRUPT_STATUS(sta_lut_done == 1)

	/* 2, load gc16 lut */
	jz4775fb_fill_wfm_lut(MODE_GC16);
	REG_EPD_LUTBF	= (unsigned int)virt_to_phys(wfm_lut_buf);
	REG_EPD_LUTSIZE	= EPD_LUT_POS800 | (frame_num * 64 / 4);
	REG_EPD_CFG	|= (frame_num - 1) << EPD_CFG_STEP;
	sta_lut_done = 0;
	REG_EPD_CTRL	|= EPD_CTRL_LUT_STRT;
	WAIT_INTERRUPT_STATUS(sta_lut_done == 1)

	return 0;
}

static int jz4775fb_load_auto_du_gc4_wfm_lut(void)
{
	WAIT_INTERRUPT_STATUS(REG_EPD_STA & EPD_STA_EPD_IDLE);

	REG_EPD_CFG &= 0x1f;
	REG_EPD_CFG |= EPD_CFG_LUT_3MODE;

	/* 1, load du lut */
	jz4775fb_fill_wfm_lut(MODE_DU);
	REG_EPD_LUTBF	= (unsigned int)virt_to_phys(wfm_lut_buf);
	REG_EPD_LUTSIZE	= EPD_LUT_POS0 | (frame_num * 64 / 4);
	REG_EPD_CFG	|= (frame_num - 1) << EPD_CFG_STEP1;
	sta_lut_done = 0;
	REG_EPD_CTRL	|= EPD_CTRL_LUT_STRT;
	WAIT_INTERRUPT_STATUS(sta_lut_done == 1)

	/* 2, load gc4 lut */
	/* If there is not GC4 waveform, don't call this function. */
	/*
	 * jz4775fb_fill_wfm_lut(MODE_GC4);
	 * REG_EPD_LUTBF	= (unsigned int)virt_to_phys(wfm_lut_buf);
	 * REG_EPD_LUTSIZE	= EPD_LUT_POS800 | (frame_num * 64 / 4);
	 * REG_EPD_CFG	|= (frame_num - 1) << EPD_CFG_STEP2;
	 * sta_lut_done = 0;
	 * REG_EPD_CTRL	|= EPD_CTRL_LUT_STRT;
	 * WAIT_INTERRUPT_STATUS(sta_lut_done == 1)
	 */

	/* 3, load gc16 lut */
	jz4775fb_fill_wfm_lut(MODE_GC16);
	REG_EPD_LUTBF	= (unsigned int)virt_to_phys(wfm_lut_buf);
	REG_EPD_LUTSIZE	= EPD_LUT_POS1800 | (frame_num * 64 / 4);
	REG_EPD_CFG	|= (frame_num - 1) << EPD_CFG_STEP;
	sta_lut_done = 0;
	REG_EPD_CTRL	|= EPD_CTRL_LUT_STRT;
	WAIT_INTERRUPT_STATUS(sta_lut_done == 1)

	return 0;
}

static void jz4775fb_start_epd_refresh(void)
{
	*(volatile unsigned char *)snp_buf = 0xff;
}

/*
 * For Surfacefilenger provide vsync.
 */
#ifdef EPDC_VSYNC_HARDWARE
static int jzfb_vsync_timestamp_changed(struct jz_epd *jz_epd, ktime_t prev_timestamp)
{
	return !ktime_equal(prev_timestamp, jz_epd->vsync_timestamp);
}
#endif

static int jzfb_wait_for_vsync_thread(void *data)
{
	struct jz_epd *jz_epd = (struct jz_epd *)data;

	while (!kthread_should_stop()) {
		int ret = 0;
#ifdef EPDC_VSYNC_TIME
		msleep(100); // TODO:Need to follow the LCD timing calculation.
		jz_epd->vsync_timestamp = ktime_get();
		ret = jz_epd->timer_vsync;
#endif
#ifdef EPDC_VSYNC_HARDWARE
		ktime_t prev_timestamp = jz_epd->vsync_timestamp;
		ret = wait_event_interruptible_timeout(
			jz_epd->vsync_wq,
			jzfb_vsync_timestamp_changed(jz_epd, prev_timestamp),
			msecs_to_jiffies(200));
#endif
		if (ret > 0) {
			char *envp[2];
			char buf[64];

			snprintf(buf, sizeof(buf), "VSYNC=%llu", ktime_to_ns(
					 jz_epd->vsync_timestamp));
			envp[0] = buf;
			envp[1] = NULL;
			kobject_uevent_env(&jz_epd->dev->kobj, KOBJ_CHANGE, envp);
		}
	}
	return 0;
}

static irqreturn_t jz4775fb_epdc_interrupt_handler(int irq, void *data)
{
	unsigned int status = REG_EPD_STA;
	unsigned int istatus= REG_EPD_ISRC;
	struct jz_epd *jz_epd = (struct jz_epd *)data;

	if(status & EPD_STA_IFF2U)
	{
		REG_EPD_STA &= ~EPD_STA_IFF2U;
	}

	if(status & EPD_STA_IFF1U)
	{
		REG_EPD_STA &= ~EPD_STA_IFF1U;
	}

	if(status & EPD_STA_IFF0U)
	{
		REG_EPD_STA &= ~EPD_STA_IFF0U;
	}

	if(status & EPD_STA_WFF1O)
	{
		REG_EPD_STA &= ~EPD_STA_WFF1O;
	}

	if(status & EPD_STA_WFF0O)
	{
		REG_EPD_STA &= ~EPD_STA_WFF0O;
	}

	if(status & EPD_STA_OFFU)
	{
		REG_EPD_STA &= ~EPD_STA_OFFU;
	}

	if(status & EPD_STA_BDR_DONE)
	{
		REG_EPD_STA &= ~EPD_STA_BDR_DONE;
	}

	if((status & EPD_STA_PWROFF) && (istatus & EPD_ISRC_PWROFF_INT_OPEN))
	{
		REG_EPD_STA &= ~EPD_STA_PWROFF;
		sta_pwroff = 1;
		if(jz_epd->epd_power && jz_epd->pdata->epd_power_ctrl.epd_power_off) {
			jz_epd->pdata->epd_power_ctrl.epd_power_off();
			jz_epd->epd_power = 0;
		}
	}

	if((status & EPD_STA_PWRON) && (istatus & EPD_ISRC_PWRON_INT_OPEN))
	{
		REG_EPD_STA &= ~EPD_STA_PWRON;
		sta_pwron = 1;
	}

	if((status & EPD_STA_REF_STOP) && (istatus & EPD_ISRC_REF_STOP_INT_OPEN))
	{
		REG_EPD_STA &= ~EPD_STA_REF_STOP;
		sta_ref_stop = 1;
#ifdef EPDC_VSYNC_HARDWARE
		jz_epd->vsync_timestamp = ktime_get();
		wake_up_interruptible(&jz_epd->vsync_wq);
#endif
		if(jz_epd->epd_power && jz_epd->pdata->epd_power_ctrl.epd_power_off) {
			jz_epd->pdata->epd_power_ctrl.epd_power_off();
			jz_epd->epd_power = 0;
		}
	}

	if(status & EPD_STA_REF_STRT)
	{
		REG_EPD_STA &= ~EPD_STA_REF_STRT;
		sta_ref_start = 1;
	}

	if(status & EPD_STA_LUT_DONE)
	{
		REG_EPD_STA &= ~EPD_STA_LUT_DONE;
		sta_lut_done = 1;
	}

	if((status & EPD_STA_FRM_END) && (istatus & EPD_ISRC_FRM_END_INT_OPEN))
	{
		REG_EPD_STA &= ~EPD_STA_FRM_END;
	}

	return IRQ_HANDLED;
}

/*
 *  cnt = 0:Parallel never, partial always,
 *  cnt = 1:Parallel every time.
 *  cnt = 2:Parallel every 2 times.
 *  cnt = 3:Parallel every 3 times.
 *  ...
 */
int jz4775fb_set_parallel_refresh(unsigned int cnt)
{
	if(((cnt != 0) && (parallel_refresh_interval_times != 0)) || (cnt <= ref_times))
		ref_times = 0;
	parallel_refresh_interval_times = cnt;

	dprintk("Parallel refresh will occur every %d times, current counted times: %d\n", \
			parallel_refresh_interval_times, ref_times);

	return 0;
}

int jz4775fb_get_parallel_refresh(unsigned int cnt)
{
	return parallel_refresh_interval_times;
}

static int jz4775fb_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
	int next_frm;

	unsigned char *frm_buf_addr = NULL;
	unsigned char *cur_buf_addr = NULL;

	struct fb_var_screeninfo *var_ptr = var;
	struct fb_info *info_ptr = info;
	struct jz_epd *jz_epd = info->par;

	static epd_mode_e prev_epd_mode = MODE_GC16GU, cur_epd_mode = MODE_GC16GU;
	static ref_mode_e prev_ref_mode = PARALLEL_REF, cur_ref_mode = PARALLEL_REF;

	if (var_ptr->xoffset - info_ptr->var.xoffset)
	{
		dprintk("Not support X panning now!\n");
		return -EINVAL;
	}

	next_frm = var_ptr->yoffset / var_ptr->yres;
	frm_buf_addr = frame_buffer + frm_buf_size * next_frm;

	cur_epd_mode = epd_mode_set;
	if(cur_epd_mode == MODE_GC16)
	{
		cur_ref_mode = PARALLEL_REF;
		ref_times = 0;
	}
	else if(cur_epd_mode == MODE_EMPTY)
	{
		cur_epd_mode = MODE_GC16GU;
		cur_ref_mode = PARTIAL_REF;
		/*cur_ref_mode = PARALLEL_REF;*/
	}
	else
	{
		if((parallel_refresh_interval_times == 0) || (++ref_times != parallel_refresh_interval_times))
		{
			cur_ref_mode = PARTIAL_REF;
			/*cur_ref_mode = PARALLEL_REF;*/
		}
		else
		{
			cur_epd_mode = MODE_GC16;
			cur_ref_mode = PARALLEL_REF;
			ref_times = 0;
		}
	}

	cur_buf_addr = current_buffer;
	jz4775_epdce_rgb565_to_16_grayscale(var_ptr, frm_buf_addr, cur_buf_addr);

	if((cur_ref_mode != prev_ref_mode) || (cur_ref_mode == PARALLEL_REF))
	{
		/*if(sta_pwroff == 0)
		{
			WAIT_INTERRUPT_STATUS(sta_pwroff == 1);
		}*/
		if(jz_epd->epd_power == 1)
		{
			WAIT_INTERRUPT_STATUS(jz_epd->epd_power == 0);
		}

		if(cur_ref_mode == PARTIAL_REF)
		{
			REG_EPD_CFG	&= ~EPD_CFG_PARTIAL;
			REG_EPD_CFG	|= EPD_CFG_PARTIAL;
		}
		else if(cur_ref_mode == PARALLEL_REF)
		{
			REG_EPD_CFG	&= ~EPD_CFG_PARTIAL;
			REG_EPD_CFG	|= EPD_CFG_PARALLEL;
		}
	}

	if(cur_epd_mode != prev_epd_mode)
	{
		jz4775fb_epd_mode_set(cur_epd_mode);

		if(cur_epd_mode == MODE_AUTO_DUGC4)
			jz4775fb_load_auto_du_gc4_wfm_lut();
		else if(cur_epd_mode == MODE_AUTO_DU)
			jz4775fb_load_auto_du_wfm_lut();
		else
			jz4775fb_load_wfm_lut(cur_epd_mode);
	}

	if((REG_EPD_STA & EPD_STA_EPD_IDLE))
	{
		/*if(sta_pwroff == 0)
		{
			WAIT_INTERRUPT_STATUS(sta_pwroff == 1);
		}
		sta_pwroff = 0;
		sta_pwron = 0;
		mdelay(5);
		REG_EPD_CTRL |= EPD_CTRL_PWRON;

		//TODO: if the time is not well ctrl , there will X2D-timeout.
		WAIT_INTERRUPT_STATUS(sta_pwron == 1);
		mdelay(1);*/

		// NOTE:Between power-on and power-off to ensure correct logic.
		if(!jz_epd->epd_power && jz_epd->pdata->epd_power_ctrl.epd_power_on) {
			jz_epd->pdata->epd_power_ctrl.epd_power_on();
			jz_epd->epd_power = 1;
		}
		jz4775fb_start_epd_refresh();
	}

	prev_epd_mode = cur_epd_mode;
	prev_ref_mode = cur_ref_mode;

	return 0;
}

static int jz4775fb_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	unsigned int value;
	struct jz_epd *jz_epd = (struct jz_epd *)info->par;

	switch (cmd)
	{
		case FBIO_SET_EINK_WRITE_MODE:
			if (copy_from_user(&epd_mode_set, argp, sizeof(epd_mode_e)))
				return -EFAULT;
			break;
		case JZFB_SET_VSYNCINT:
			if (copy_from_user(&value, argp, sizeof(int)))
				return -EFAULT;
			if (value) {
#ifdef EPDC_VSYNC_HARDWARE
				jz_epd->vsync_timestamp = ktime_get();
				wake_up_interruptible(&jz_epd->vsync_wq);
#endif
#ifdef EPDC_VSYNC_TIME
				jz_epd->timer_vsync = 1;
#endif
			} else {
#ifdef EPDC_VSYNC_TIME
				jz_epd->timer_vsync = 0;
#endif
			}
			break;
	}
	return 0;
}

static int jz4775fb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct fb_info *fb = info;
	unsigned long start;
	unsigned long off;
	u32 len;

	off = vma->vm_pgoff << PAGE_SHIFT;

	/* frame buffer memory */
	start = fb->fix.smem_start;
	len = PAGE_ALIGN((start & ~PAGE_MASK) + fb->fix.smem_len);
	start &= PAGE_MASK;

	if ((vma->vm_end - vma->vm_start + off) > len)
		return -EINVAL;
	off += start;

	vma->vm_pgoff = off >> PAGE_SHIFT;
	vma->vm_flags |= VM_IO;

	pgprot_val(vma->vm_page_prot) &= ~_CACHE_MASK;
	pgprot_val(vma->vm_page_prot) |= _CACHE_CACHABLE_WA;

	if (io_remap_pfn_range(vma, vma->vm_start, off >> PAGE_SHIFT,
			       vma->vm_end - vma->vm_start,
			       vma->vm_page_prot)) {
		return -EAGAIN;
	}

	return 0;
}

static void jz4775fb_epd_controller_init(void)
{
	struct epd_timing etm = panel_info.epd_tm;

	REG_EPD_VAT   =  etm.vt		<< EPD_VAT_VT		|	etm.ht		<< EPD_VAT_HT;
	REG_EPD_HSYNC =  etm.hpe	<< EPD_HSYNC_HPE	|	etm.hps		<< EPD_HSYNC_HPS;
	REG_EPD_SDSP  =	 etm.sdspd	<< EPD_SDSP_DIS		|	etm.sdspe	<< EPD_SDSP_ENA;
	REG_EPD_DAH   =  etm.hde	<< EPD_DAH_HDE		|	etm.hds		<< EPD_DAH_HDS;
	REG_EPD_SDOE  =  etm.sdoed	<< EPD_SDOE_DIS		|	etm.sdoee	<< EPD_SDOE_ENA;

	REG_EPD_GDCLK =  etm.gdclkd	<< EPD_GDCLK_DIS	|	etm.gdclke	<< EPD_GDCLK_ENA;
	REG_EPD_VSYNC =  etm.vpe	<< EPD_VSYNC_VPE	|	etm.vps		<< EPD_VSYNC_VPS;
	REG_EPD_GDSP  =  etm.gdspd	<< EPD_GDSP_DIS		|	etm.gdspe	<< EPD_GDSP_ENA;
	REG_EPD_DAV   =  etm.vde	<< EPD_DAV_VDE		|	etm.vds		<< EPD_DAV_VDS;
	REG_EPD_GDOE  =  etm.gdoed	<< EPD_GDOE_DIS		|	etm.gdoee	<< EPD_GDOE_ENA;

	REG_EPD_ISRC = EPD_ISRC_PWROFF_INT_OPEN | EPD_ISRC_PWRON_INT_OPEN | EPD_ISRC_REF_STOP_INT_OPEN | EPD_ISRC_LUT_DONE_INT_OPEN | EPD_ISRC_FRM_END_INT_OPEN;
	REG_EPD_DIS0 = EPD_DIS0_SDSP_CAS | EPD_DIS0_GDCLK_MODE | EPD_DIS0_GDUD | EPD_DIS0_GDCLK_POL | EPD_DIS0_GDOE_POL | EPD_DIS0_SDCLK_POL |   EPD_DIS0_SDOE_POL | EPD_DIS0_SDLE_POL;
	REG_EPD_DIS1 = EPD_DIS1_SDDO_REV | (panel_info.epd_var.xres / 4) << EPD_DIS1_SDOS | 1 << EPD_DIS1_SDCE_NUM;
/*
	REG_EPD_PMGR0   =   0x5 << EPD_PMGR0_PWR_DLY12 | 0x5 << EPD_PMGR0_PWR_DLY01;
	REG_EPD_PMGR1   =   0x5 << EPD_PMGR1_PWR_DLY34 | 0x0 << EPD_PMGR1_PWR_DLY23;
	REG_EPD_PMGR2   =   0x0 << EPD_PMGR2_PWR_DLY56 | 0x0 << EPD_PMGR2_PWR_DLY45;
	REG_EPD_PMGR3   =   0x1 << EPD_PMGR3_PWRCOM_POL | 0x1 << EPD_PMGR3_UNIPOL | 0x1f << EPD_PMGR3_PWR_POL | 0x0 << EPD_PMGR3_PWR_DLY67;
	REG_EPD_PMGR4   =   0x1f << EPD_PMGR4_PWR_ENA | 0 << EPD_PMGR4_PWR_VAL;
*/
	dprintk("REG_EPD_PMGR0	= 0x%08x\n", REG_EPD_PMGR0);
	dprintk("REG_EPD_PMGR1	= 0x%08x\n", REG_EPD_PMGR1);
	dprintk("REG_EPD_PMGR2	= 0x%08x\n", REG_EPD_PMGR2);
	dprintk("REG_EPD_PMGR3	= 0x%08x\n", REG_EPD_PMGR3);
	dprintk("REG_EPD_PMGR4	= 0x%08x\n", REG_EPD_PMGR4);

	REG_DDR_PRI = 0x0888b888;
	REG_DDR_CONFLICT |= 0x80000000;

	REG_EPD_CTRL |= EPD_CTRL_EPD_ENA;
	REG_EPD_CFG |= EPD_CFG_AUTO_GATED | EPD_CFG_AUTO_STOP_ENA;
	/*REG_EPD_CFG |= EPD_CFG_AUTO_STOP_ENA;*/

	jz4775fb_load_wfm_lut(MODE_GC16);

	REG_EPD_SIZE = panel_info.epd_var.yres << EPD_SIZE_VSIZE | panel_info.epd_var.xres << EPD_SIZE_HSIZE;
	REG_EPD_CURBF = (unsigned int)virt_to_phys(current_buffer);
	REG_EPD_CURSIZE	= cur_buf_size / 4;
	// if this is no value, then can be did
	REG_EPD_WRK0BF	= (unsigned int)virt_to_phys(wrk0_buf);
	REG_EPD_WRK0SIZE = wrk0_buf_size / 4;
	REG_EPD_WRK1BF	= (unsigned int)virt_to_phys(wrk1_buf);
	REG_EPD_WRK1SIZE = wrk1_buf_size / 4;
	REG_EPD_CFG	|= EPD_CFG_IBPP_4BITS;
}

static int jz4775fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	/* Add for fbmem.c check var*/
	return 0;
}

static struct fb_ops jz4775fb_ops =
{
	.owner		= THIS_MODULE,
	.fb_open	= jz4775fb_open,
	.fb_release	= jz4775fb_release,
	.fb_check_var	= jz4775fb_check_var,
	.fb_pan_display	= jz4775fb_pan_display,
	.fb_ioctl	= jz4775fb_ioctl,
	.fb_mmap    = jz4775fb_mmap,
};

static int jz4775fb_set_info(struct fb_info *fbinfo)
{
	struct fb_info *fb = fbinfo;

	strcpy(fb->fix.id,"Jz4775EPD-fb0");

	fb->flags		= FBINFO_FLAG_DEFAULT;
	fb->var			= panel_info.epd_var;
	fb->var.xoffset		= 0;
	fb->var.xres_virtual	= fb->var.xres;
	fb->var.yres_virtual	= fb->var.yres * NUM_FRAME_BUFFERS;
	fb->var.pixclock	= 40000000;//KHZ2PICOS(cpm_get_clock(CGU_LPCLK0)/1000);	[> pixel clock in ps (pico seconds) <]

	/* Current framebuffer fix info*/
	fb->fix.type		= FB_TYPE_PACKED_PIXELS;
	fb->fix.type_aux	= 0;
	fb->fix.xpanstep	= 1;
	fb->fix.ypanstep	= 1;
	fb->fix.ywrapstep	= 0;
	fb->fix.accel		= FB_ACCEL_NONE;

	fb->fix.smem_start	= virt_to_phys((void *)frame_buffer);		/* Start of frame buffer mem */
	fb->fix.smem_len	= frm_buf_size * NUM_FRAME_BUFFERS;			/* Length of frame buffer mem */
	fb->fix.line_length	= panel_info.epd_var.xres * panel_info.epd_var.bits_per_pixel / 8;	/* length of a line in bytes */
	fb->screen_base		= (unsigned char *)(((unsigned int)frame_buffer & 0x1fffffff) | 0xa0000000);

	fb->fbops		= &jz4775fb_ops;

	return 0;
}

static int jz4775fb_map_smem(void)
{
	unsigned long page;

	unsigned int frm_buf_width	= panel_info.epd_var.xres;
	unsigned int frm_buf_height	= panel_info.epd_var.yres;
	unsigned int frm_buf_bpp	= panel_info.epd_var.bits_per_pixel;

	unsigned int cur_buf_width	= panel_info.epd_var.xres;
	unsigned int cur_buf_height	= panel_info.epd_var.yres;
	unsigned int cur_buf_bpp	= EPD_BPP;

	frm_buf_size = frm_buf_width * frm_buf_height * frm_buf_bpp / 8;
	cur_buf_size = cur_buf_width * cur_buf_height * cur_buf_bpp / 8;

	wrk0_buf_size	= cur_buf_size	* 2;				/* new + old pix buffer */
	wrk1_buf_size	= cur_buf_width	* cur_buf_height * 8 / 8;	/* pixcnt buffer */
	wrk_buf_size	= wrk0_buf_size	+ wrk1_buf_size;		/* new + old + pixcnt buffer */

	wfm_buf_size	= 1024 * 1024;
	snp_buf_size	= 4096 * 2;

	dprintk("frm_buf_size	= %d\n", frm_buf_size);
	dprintk("cur_buf_size	= %d\n", cur_buf_size);
	dprintk("wfm_buf_size	= %d\n", wfm_buf_size);
	dprintk("wrk_buf_size	= %d\n", wrk_buf_size);
	dprintk("wrk0_buf_size	= %d\n", wrk0_buf_size);
	dprintk("wrk1_buf_size	= %d\n", wrk1_buf_size);

	for(frm_buf_order = 0; frm_buf_order < 12; frm_buf_order++)
	{
		if((PAGE_SIZE << frm_buf_order) >= (frm_buf_size * NUM_FRAME_BUFFERS))
			break;
	}

	frame_buffer		= (unsigned char *)__get_free_pages(GFP_KERNEL, frm_buf_order);
	current_buffer		= kmalloc((cur_buf_size) * CURBUF_NUM, GFP_KERNEL);
	working_buffer		= kmalloc(wrk_buf_size, GFP_KERNEL);
	snooping_buffer		= kmalloc(snp_buf_size, GFP_KERNEL);
	wfm_lut_buf			= kmalloc(WFM_LUT_SIZE, GFP_KERNEL);

	if((!frame_buffer) || (!current_buffer) || (!working_buffer) || (!snooping_buffer) || (!wfm_lut_buf))
		return -ENOMEM;

	memset((void *)frame_buffer,	0x00, PAGE_SIZE << frm_buf_order);
	memset((void *)current_buffer,	0x00, cur_buf_size);
	memset((void *)working_buffer,	0x00, wrk_buf_size);
	memset((void *)snooping_buffer,	0x00, snp_buf_size);
	memset((void *)wfm_lut_buf,	0x00, WFM_LUT_SIZE);

	dprintk("\n");
	dprintk("frame_buffer	= 0x%08x\n", (unsigned int)frame_buffer);
	dprintk("current_buffer	= 0x%08x\n", (unsigned int)current_buffer);
	dprintk("working_buffer	= 0x%08x\n", (unsigned int)working_buffer);
	dprintk("snooping_buffer	= 0x%08x\n", (unsigned int)snooping_buffer);
	dprintk("wfm_lut_buf = %p\n", wfm_lut_buf);

	for (page = (unsigned long)frame_buffer;
			page < PAGE_ALIGN((unsigned long)frame_buffer + (PAGE_SIZE << frm_buf_order));
			page += PAGE_SIZE)
	{
		SetPageReserved(virt_to_page((void*)page));
	}

	wrk_buf = (unsigned char *)(working_buffer);
	wrk0_buf = wrk_buf;
	wrk1_buf = wrk_buf + wrk0_buf_size;
	dprintk("wrk_buf = 0x%08x\n", (unsigned int)wrk_buf);
	dprintk("wrk0_buf = 0x%08x\n", (unsigned int)wrk0_buf);
	dprintk("wrk1_buf = 0x%08x\n", (unsigned int)wrk1_buf);

	wfm_buf = WAVEFORM_LUT;
	dprintk("wfm_buf = 0x%08x\n", (unsigned int)wfm_buf);

	snp_buf = snooping_buffer;
	dprintk("snp_buf = 0x%08x\n", (unsigned int)snp_buf);

	REG_DDR_SNP_LOW = ((((unsigned int)virt_to_phys(snp_buf)) >> 12) & 0xfffff);
	REG_DDR_SNP_HIGH = ((((unsigned int)virt_to_phys(snp_buf)) >> 12) & 0xfffff);
	dprintk("REG_DDR_SNP_LOW = 0X%08X\n", REG_DDR_SNP_LOW);
	dprintk("REG_DDR_SNP_HIGH = 0X%08X\n", REG_DDR_SNP_HIGH);

	return 0;
}

static void jz4775fb_unmap_smem(void)
{
	if(wfm_lut_buf)
		kfree(wfm_lut_buf);

	if(current_buffer)
		kfree(current_buffer);

	if(working_buffer)
		kfree(working_buffer);

	if(frame_buffer)
		free_pages((int)frame_buffer, frm_buf_order);

	return;
}
/*
static void jz4775fb_rotate_logobuf_180(unsigned char * buf, unsigned int size)
{
	int i;
	unsigned char tmp;
	unsigned int half_size = size / 2;

	for(i = 0; i < half_size; i++)
	{
		tmp = buf[i];
		buf[i] = buf[size - i];
		buf[size - i] = tmp;
	}
}
*/
int jz4775fb_clear_screen(unsigned char value)
{
	if(sta_pwroff == 0)
		WAIT_INTERRUPT_STATUS(sta_pwroff == 1);
	sta_pwron = 0;
	mdelay(5);
	REG_EPD_CTRL |= EPD_CTRL_PWRON;
	WAIT_INTERRUPT_STATUS(sta_pwron == 1);
	jz4775fb_load_wfm_lut(MODE_GC16);
	memset(current_buffer, value, cur_buf_size);
	dma_cache_wback((unsigned int)current_buffer, cur_buf_size);
	sta_pwroff = 0;
	jz4775fb_start_epd_refresh();
	WAIT_INTERRUPT_STATUS(sta_pwroff == 1);
	return 0;
}

static int __init jz4775fb_probe(struct platform_device *pdev)
{
	int retval = 0;
	struct jz_epd *jz_epd;
	struct fb_info *fb0;
	struct device *device = &pdev->dev;
	struct jz_epd_platform_data *pdata = pdev->dev.platform_data;
	struct resource *mem;
	unsigned long rate = 40000000;//PICOS2KHZ(40000) * 1000;

	if(!pdata) {
		dev_err(device, "Missing platform data\n");
		return -ENXIO;
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		dev_err(device, "Failed to get register memory resource\n");
		return -ENXIO;
	}

	mem = request_mem_region(mem->start, resource_size(mem), pdev->name);
	if (!mem) {
		dev_err(device, "Failed to request register memory region\n");
		return -EBUSY;
	}

	/* Allocate space for info pointer */
	fb0 = framebuffer_alloc(sizeof(struct jz_epd), device);
	if (!fb0)
		goto failed_alloc_fbinfo;

	fb0->fbops = &jz4775fb_ops;
	fb0->flags = FBINFO_FLAG_DEFAULT;

	jz_epd = fb0->par;
	jz_epd->fb = fb0;
	jz_epd->dev = device;
	jz_epd->pdata = pdata;
	jz_epd->id = pdev->id;
	jz_epd->mem = mem;

	jz_epd->base = ioremap(mem->start, resource_size(mem));
	if (!jz_epd->base) {
		dev_err(device, "Failed to ioremap register memory region\n");
		retval = -EBUSY;
		goto failed_alloc_fbinfo;
	}

	sprintf(jz_epd->clk_name, "epdc");
	sprintf(jz_epd->pclk_name, "lcd_pclk0");
	jz_epd->epdc_clk = clk_get(device, jz_epd->clk_name);
	jz_epd->pclk = clk_get(device, jz_epd->pclk_name);
	if (IS_ERR(jz_epd->epdc_clk) || IS_ERR(jz_epd->pclk)) {
		retval = PTR_ERR(jz_epd->epdc_clk);
		dev_err(device, "Failed to get epdc clock: %d\n", retval);
		goto failed_get_clk;
	}

	clk_enable(jz_epd->epdc_clk);
	clk_set_rate(jz_epd->pclk, rate);
	dev_info(device, "EPDC: PixClock:%lu\n", rate);
	dev_info(device, "EPDC: PixClock:%lu(real)\n",
			clk_get_rate(jz_epd->pclk));
	clk_enable(jz_epd->pclk);

	if(jz_epd->pdata->epd_power_ctrl.epd_power_init)
		jz_epd->pdata->epd_power_ctrl.epd_power_init();

	panel_info = epd_panel_info_init();
	dprintk("EPD Panel: %s Resolution: %d * %d\n", panel_info.model, panel_info.epd_var.xres, panel_info.epd_var.yres);

	/* Allocate frm buf, cur buf, wrk buf, wfm buf ...*/
	retval = jz4775fb_map_smem();
	if(retval)
		goto failed_map_smem;

	retval = jz4775fb_set_info(fb0);
	if(retval)
		return -EINVAL;

	retval = register_framebuffer(fb0);
	if (retval)
		return -EINVAL;

#if defined(EPDC_VSYNC_HARDWARE) || defined(EPDC_VSYNC_TIME)
#ifdef EPDC_VSYNC_HARDWARE
	init_waitqueue_head(&jz_epd->vsync_wq);
#endif
	jz_epd->vsync_thread = kthread_run(jzfb_wait_for_vsync_thread,
					jz_epd, "jzfb-vsync");
	if (jz_epd->vsync_thread == ERR_PTR(-ENOMEM)) {
		dprintk("Failed to run vsync thread");
		return 0;
	}
#endif

	sprintf(jz_epd->irq_name, "epdc");
	retval = request_irq(IRQ_EPDC, jz4775fb_epdc_interrupt_handler,
			IRQF_DISABLED, jz_epd->irq_name, jz_epd);
	if(retval)
	{
		dprintk("%s %d request irq failed\n",__FUNCTION__,__LINE__);
	}

	if(jz_epd->pdata->epd_power_ctrl.epd_power_on) {
		jz_epd->pdata->epd_power_ctrl.epd_power_on();
		jz_epd->epd_power = 1;
	}

	jz4775fb_epd_controller_init();
	print_fb_info(fb0);
	print_epdc_registers();
	jz4775fb_clear_screen(0xff);

	return 0;

failed_map_smem:
	jz4775fb_unmap_smem();

failed_get_clk:
	if(jz_epd->pclk)
		clk_put(jz_epd->pclk);
	if(jz_epd->epdc_clk)
		clk_put(jz_epd->epdc_clk);

failed_alloc_fbinfo:
	if (fb0)
		framebuffer_release(fb0);

	return retval;
}

static int jz4775fb_remove(struct platform_device *pdev)
{
	void *drvdata = platform_get_drvdata(pdev);
	struct fb_info *fb0 = (struct fb_info *)drvdata;
	struct jz_epd *jz_epd = fb0->par;

	kthread_stop(jz_epd->vsync_thread);
	clk_disable(jz_epd->pclk);
	clk_disable(jz_epd->epdc_clk);
	if(jz_epd->pclk)
		clk_put(jz_epd->pclk);
	if(jz_epd->epdc_clk)
		clk_put(jz_epd->epdc_clk);

	if(jz_epd->pdata->epd_power_ctrl.epd_power_off) {
		jz_epd->pdata->epd_power_ctrl.epd_power_off();
		jz_epd->epd_power = 0;
	}

	if (fb0)
	{
		unregister_framebuffer(fb0);
		framebuffer_release(fb0);
	}
	return 0;
}

#ifdef CONFIG_PM
static int jz4775fb_suspend(struct platform_device *pdev, pm_message_t state)
{
	REG_EPD_CFG &= ~EPD_CFG_AUTO_STOP_ENA;
	return 0;
}

static int jz4775fb_resume(struct platform_device *pdev)
{
	REG_EPD_CFG |= EPD_CFG_AUTO_STOP_ENA;
	return 0;
}
#else
#define jz4775fb_suspend NULL
#define jz4775fb_resume NULL
#endif /* CONFIG_PM */

static struct platform_driver jz4775fb_driver =
{
	.probe	=	jz4775fb_probe,
	.remove	=	jz4775fb_remove,
	.suspend =	jz4775fb_suspend,
	.resume =	jz4775fb_resume,
	.driver =	{.name = DRIVER_NAME, },
};

static int __init jz4775fb_init(void)
{
	return platform_driver_register(&jz4775fb_driver);
}

static void __exit jz4775fb_exit(void)
{
	platform_driver_unregister(&jz4775fb_driver);
}

module_init(jz4775fb_init);
module_exit(jz4775fb_exit);

MODULE_AUTHOR("Watson Zhu <wjzhu@ingenic.cn>");
MODULE_DESCRIPTION("JZ4775 EPD Controller Driver");
MODULE_LICENSE("GPL");
