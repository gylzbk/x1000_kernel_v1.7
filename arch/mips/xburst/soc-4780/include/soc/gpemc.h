/*
 *  Copyright (C) 2013 Fighter Sun <wanmyqawdr@126.com>
 *  GPEMC(NEMC) support functions
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

#ifndef __SOC_GPEMC_H__
#define __SOC_GPEMC_H__

struct gpemc_bank_timing {

};

struct gpemc_bank {
	int cs;
	void __iomem *io_base;
	struct gpemc_bank_timing bank_timing;

	void __iomem *io_nand_dat;
	void __iomem *io_nand_addr;
	void __iomem *io_nand_cmd;
};

#define GPEMC_NAND_BANK_DATA_OFFSET	0
#define GPEMC_NAND_BANK_ADDR_OFFSET	0x800000
#define GPEMC_NAND_BANK_CMD_OFFSET	0x400000

extern struct gpemc_bank *gpemc_request_cs(int cs);
extern void gpemc_release_cs(struct gpemc_bank* bank);
extern int gpemc_config_bank_timing(struct gpemc_bank_timing *timing);

#endif
