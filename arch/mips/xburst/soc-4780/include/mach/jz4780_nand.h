/*
 *  Copyright (C) 2013 Fighter Sun <wanmyqawdr@126.com>
 *  JZ4780 SoC NAND controller driver
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef __MACH_JZ4780_NAND_H__
#define __MACH_JZ4780_NAND_H__

#include <linux/completion.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <soc/gpemc.h>

typedef enum {
	/* NAND_XFER_<Data path driver>_<R/B# indicator> */
	NAND_XFER_CPU_IRQ = 0,
	NAND_XFER_CPU_POLL,
	NAND_XFER_DMA_IRQ,
	NAND_XFER_DMA_POLL
} nand_xfer_type_t;

typedef enum {
	NAND_ECC_TYPE_HW = 0,
	NAND_ECC_TYPE_SW
} nand_ecc_type_t;

typedef struct {
	int bank;

	int busy_gpio;
	int busy_gpio_low_assert;

	int wp_gpio;                      /* -1 if does not exist */
	int wp_gpio_low_assert;

	gpemc_bank_t cs;
	int busy_irq;
	unsigned int curr_command;

	struct completion ready;
	unsigned int ready_timout_ms;
} nand_flash_if_t;

typedef struct {
	common_nand_timing_t common_nand_timing;
	toggle_nand_timing_t toggle_nand_timing;
} nand_timing_t;

typedef struct {
	const char *name;
	int nand_dev_id;

	bank_type_t type;

	struct {
		int data_size;
		int ecc_bits;
	} ecc_step;

	nand_timing_t nand_timing;
} nand_flash_info_t;

struct jz4780_nand_platform_data {
	struct mtd_partition *part_table; /* MTD partitions array */
	int num_part;                     /* number of partitions */

	nand_flash_if_t *nand_flash_if_table;
	int num_nand_flash_if;

	/* not NULL to override default built-in settings in driver */

	struct nand_flash_dev *nand_flash_table;
	int num_nand_flash;

	nand_flash_info_t *nand_flash_info_table;
	int num_nand_flash_info;

	nand_xfer_type_t xfer_type;  /* transfer type */

	nand_ecc_type_t ecc_type;
};

#define COMMON_NAND_CHIP_INFO(_NAME, _DEV_ID,	\
		_DATA_SIZE_PRE_ECC_STEP,	\
		_ECC_BITS_PRE_ECC_STEP,	\
		_Tcls, _Tclh, _Tals, _Talh,	\
		_Tcs, _Tch, _Tds, _Tdh, _Twp,	\
		_Twh, _Twc, _Trc, _Tadl, _Trhw, _Twhr, _Twhr2,	\
		_Trp, _Trr,	_Tcwaw, _Twb, _Tww,	\
		_Trst, _Tfeat, _Tdcbsyr, _Tdcbsyr2, _BW)	\
		.name = (_NAME),	\
		.nand_dev_id = (_DEV_ID),	\
		.type = BANK_TYPE_NAND,	\
		.ecc_step = {	\
			.data_size = (_DATA_SIZE_PRE_ECC_STEP),	\
			.ecc_bits = (_ECC_BITS_PRE_ECC_STEP),	\
		},	\
		.nand_timing = {	\
			.common_nand_timing = {	\
				.Tcls = (_Tcls),	\
				.Tclh = (_Tclh),	\
				.Tals = (_Tals),	\
				.Talh = (_Talh),	\
				.Tch = (_Tch),	\
				.Tds = (_Tds),	\
				.Tdh = (_Tdh),	\
				.Twp = (_Twp),	\
				.Twh = (_Twh),	\
				.Twc = (_Twc),	\
				.Trc = (_Trc),	\
				.Trhw = (_Trhw),	\
				.Twhr = (_Twhr),	\
				.Trp = (_Trp),	\
					\
				.busy_wait_timing = {	\
					.Tcs = (_Tcs),	\
					.Tadl = (_Tadl),	\
					.Trr = (_Trr),	\
					.Tcwaw = (_Tcwaw),	\
					.Twb = (_Twb),	\
					.Tww = (_Tww),	\
					.Trst = (_Trst),	\
					.Tfeat = (_Tfeat),	\
					.Tdcbsyr = (_Tdcbsyr),	\
					.Tdcbsyr2 = (_Tdcbsyr2),	\
					.Twhr2 = (_Twhr2),	\
				},	\
					\
				.BW = (_BW),	\
			},	\
		},

/* TODO: implement it */
#define TOGGLE_NAND_CHIP_INFO(TODO)

#define COMMON_NAND_INTERFACE(BANK,	\
		BUSY_GPIO, BUSY_GPIO_LOW_ASSERT,	\
		WP_GPIO, WP_GPIO_LOW_ASSERT)	\
		.bank = (BANK),	\
		.busy_gpio = (BUSY_GPIO),	\
		.busy_gpio_low_assert = (BUSY_GPIO_LOW_ASSERT),	\
		.wp_gpio = (WP_GPIO),	\
		.wp_gpio_low_assert = (WP_GPIO_LOW_ASSERT),	\
		.cs = {	\
			.bank_type = (BANK_TYPE_NAND),	\
		},	\

/* TODO: implement it */
#define TOGGLE_NAND_INTERFACE(TODO)

#define LP_OPTIONS NAND_SAMSUNG_LP_OPTIONS
#define LP_OPTIONS16 (LP_OPTIONS | NAND_BUSWIDTH_16)

#endif
