/*
 * nand_mcu.c
 */
#include <common.h>
#include <nand.h>
#include <message.h>

#include <asm/jzsoc.h>

static inline void nand_wait_rb(void)
{
	volatile unsigned int timeout = 1000;
	while ((REG_GPIO_PXPIN(0) & 0x00100000) && timeout--);
	while (!(REG_GPIO_PXPIN(0) & 0x00100000));
}

static void nand_send_addr(struct nand_chip *nand, int col_addr, int row_addr)
{
	u32 nandport = (u32)nand->nand_io;
	int i;

	if (col_addr >= 0) {
		if (nand->pagesize != 512) {
			__nand_addr(col_addr & 0xFF, nandport);
			col_addr >>= 8;
		}
		__nand_addr(col_addr & 0xFF, nandport);
	}

	if (row_addr >= 0) {
		for (i = 0; i < nand->rowcycle; i++) {
			__nand_addr(row_addr & 0xFF, nandport);
			row_addr >>= 8;
		}
	}
}

void inline pdma_nand_status(struct nand_chip *nand, u8 *status)
{
	u32 nandport = (u32)nand->nand_io;

	__nand_cmd(NAND_CMD_STATUS, nandport);
	__nand_status(*status, nandport);
}

void pdma_nand_read_ctrl(struct nand_chip *nand, int page, const int ctrl)
{
	u32 nandport = (u32)nand->nand_io;

	if (ctrl == CTRL_READ_OOB) {
		__nand_cmd(NAND_CMD_READ0, nandport);
		nand_send_addr(nand, nand->pagesize, page);

		if (nand->pagesize != 512)
			__nand_cmd(NAND_CMD_READSTART, nandport);

		nand_wait_rb();

		*(unsigned int *)(nand->mcu_test) = nand->pipe_cnt | (2<<16);
	} else { /* CTRL_READ_DATA */
		__nand_cmd(NAND_CMD_RNDOUT, nandport);
		nand_send_addr(nand, 0x00, -1);

		__nand_cmd(NAND_CMD_RNDOUTSTART, nandport);
		__pn_enable();
	}
}

int pdma_nand_write_ctrl(struct nand_chip *nand, int page, const int ctrl)
{
	u32 nandport = (u32)nand->nand_io;
	u8 state;

	if (ctrl == CTRL_WRITE_DATA) {
		__nand_cmd(NAND_CMD_SEQIN, nandport);
		nand_send_addr(nand, 0x00, page);
		__pn_enable();
			*(unsigned int *)(nand->mcu_test) = nand->pipe_cnt | (3<<16);
	} else if (ctrl == CTRL_WRITE_OOB) {
		__pn_disable();
		__nand_cmd(NAND_CMD_RNDIN, nandport);
		nand_send_addr(nand, nand->pagesize, -1);
	} else { /* CTRL_WRITE_CONFIRM */
		__nand_cmd(NAND_CMD_PAGEPROG, nandport);

		nand_wait_rb();
		pdma_nand_status(nand, &state);

		if (state & NAND_STATUS_FAIL)
			return -1;
	}

	return 0;
}

int pdma_nand_erase(struct nand_chip *nand, int page)
{
	u32 nandport = (u32)nand->nand_io;
	u8 state;

	__nand_enable(nand->chipsel);

	__nand_cmd(NAND_CMD_ERASE1, nandport);
	nand_send_addr(nand, -1, page);
	__nand_cmd(NAND_CMD_ERASE2, nandport);

	nand_wait_rb();
	pdma_nand_status(nand, &state);

	__nand_disable();

	return state & NAND_STATUS_FAIL ? -1 : 0;
}

void pdma_nand_init(struct nand_chip *nand, struct nand_pipe_buf *pipe_buf,
		unsigned int *info, unsigned char *oob_buf)
{
	nand->nandtype = info[MSG_NANDTYPE];
	nand->pagesize = info[MSG_PAGESIZE];
	nand->oobsize = info[MSG_OOBSIZE];
	nand->rowcycle = info[MSG_ROWCYCLE];

	nand->ecclevel = info[MSG_ECCLEVEL];
	nand->eccsize = info[MSG_ECCSIZE];
	nand->eccbytes = info[MSG_ECCBYTES];
	nand->eccsteps = info[MSG_ECCSTEPS];
	nand->ecctotal = info[MSG_ECCTOTAL];
	nand->eccpos = info[MSG_ECCPOS];

	pipe_buf[0].pipe_par = pipe_buf[0].pipe_data + nand->eccsize;
	pipe_buf[1].pipe_par = pipe_buf[1].pipe_data + nand->eccsize;

	nand->mode = PNAND_HALT;
}
