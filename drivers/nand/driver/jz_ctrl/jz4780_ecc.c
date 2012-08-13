/*
 *  lib_nand/nand_ecc.c
 *
 *  ECC utils.
 */

#include "nand_api.h"

extern JZ_ECC jz_default_ecc;
extern JZ_ECC jz_nand_dma_ecc;

static inline void jz4780_bch_init(void *nand_ecc, void *flash_type)
{
        JZ_ECC *pjz4780_nand_ecc = (JZ_ECC *)nand_ecc;
        /*Init clock, start bch clock*/
        //	__cpm_set_bchdiv(3);
        //	cpm_start_clock(CGM_BCH);

        dbg_line();

        /*Inherit the default ecc operation*/

        jz_default_ecc.ecc_init(pjz4780_nand_ecc,flash_type);

        /*You can define the derived operations here*/
}

/*
 * jznand_ecc
 */
JZ_ECC jznand_ecc = 
{
        .ecc_init = jz4780_bch_init,
};

//EXPORT_SYMBOL(jznand_ecc);
